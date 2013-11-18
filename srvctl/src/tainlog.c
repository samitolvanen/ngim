/*
 * tainlog.c
 *
 * Copyright © 2005, 2006  Sami Tolvanen <sami@ngim.org>
 */

#include "common.h"
#include "srvctl.h"
#include <apr_general.h>
#include <apr_file_info.h>
#include <apr_file_io.h>
#include <apr_signal.h>
#include <apr_strings.h>
#include <ngim/base.h>

/* If zero, insecure permissions for files and directories are ignored */
#define TAINLOG_SET_PERMS_FOR_EXISTING 0

/* Input buffer parameters */
#define DEFAULT_BUFSIZE		148	/* Line buffer size, including stamp text */
#define MAX_BUFSIZE			4096
#define MIN_BUFSIZE			60
#define BUFFER_START		(NGIM_TAIN_FORMAT + 1) /* Input start position */
#define BUFFER_SEPARATOR	NGIM_TAIN_FORMAT

/* Log file size and rotation */
#define DEFAULT_FILESIZE	100000 /* Default size for a log file */
#define MIN_FILESIZE		1000 /* 1k */
#define MAX_FILESIZE		100000000 /* 100M */
#define DEFAULT_KEEPNUM		10 /* Default number of old log files to keep */
#define MAX_KEEPNUM			100000 /* Maximum number of old log files */

/* Pauses */
#define PAUSE_READLINE		2 /* Pause in case of read failure */

/* Flags and current */
static int flag_eof = 0;
static apr_file_t *current = NULL;
static apr_size_t current_size = 0;

/* Variables for command line arguments */
static const char *arg_root = NULL; /* Root directory */
static const char *arg_logdir = NULL; /* Log subdirectory */
static const char *arg_keep = NULL;
static int arg_keepnum = DEFAULT_KEEPNUM;
static const char *arg_user = NULL;
static const char *arg_group = NULL;
static int arg_bufsize = DEFAULT_BUFSIZE;
static const char *arg_buffer = NULL;
static int arg_filesize = DEFAULT_FILESIZE;
static const char *arg_file = NULL;

/* Bitmasks for command line parameters */
enum {
	cmd_help	= 1 << 0,
	cmd_keep	= 1 << 1,
	cmd_keepall = 1 << 2,
	cmd_logdir	= 1 << 3,
	cmd_user	= 1 << 4,
	cmd_group	= 1 << 5,
	cmd_buffer	= 1 << 6,
	cmd_file	= 1 << 7
};

/* Command line parameters and arguments */
static ngim_cmdline_params_t logger_params[] = {
	{ "--help",			cmd_help,		NULL },
	{ "-h",				cmd_help,		NULL },
	{ "--keep",			cmd_keep,		&arg_keep },
	{ "-k",				cmd_keep,		&arg_keep },
	{ "--keep-all",		cmd_keepall,	NULL },
	{ "-a",				cmd_keepall,	NULL },
	{ "--logdir",		cmd_logdir,		&arg_logdir },
	{ "-l",				cmd_logdir,		&arg_logdir },
	{ "--user",			cmd_user,		&arg_user },
	{ "-u",				cmd_user,		&arg_user },
	{ "--group",		cmd_group,		&arg_group },
	{ "-g",				cmd_group,		&arg_group },
	{ "--logsize",		cmd_file,		&arg_file },
	{ "-s",				cmd_file,		&arg_file },
	{ "--line-buffer",	cmd_buffer,		&arg_buffer },
	{ "-b",				cmd_buffer,		&arg_buffer },
	{ NULL,				0,				NULL }
};
static ngim_cmdline_args_t logger_args[] = {
	{ &arg_root },
	{ NULL }
};

#define CMDLINE_USAGE \
	"--help | [--user name] [--group name] [--keep num_files | --keep-all] " \
	"[--logdir subdir] [--logsize file_bytes ] [--line-buffer size] directory"


/* Validates command line. Present parameters are specified in selected.
 * Prints an error message and returns <0 if command line is invalid. */
static int validate_cmdline(const apr_uint32_t selected)
{
	if (selected & cmd_help) {
		return -1;
	}
	
	if (!arg_root) {
		/* Must always have root directory */
		warn_error1("missing argument");
		return -1;
	}

	if (selected & cmd_keep && selected & cmd_keepall) {
		warn_error1("invalid arguments");
		return -1;
	} else if (selected & cmd_keep) {
		/* Optional argument */
		apr_int64_t num;

		die_assert(arg_keep);
		num = apr_atoi64(arg_keep);

		/* Make sure we have a sane value */
		if (num > MAX_KEEPNUM) {
			warn_error1("argument too big, using maximum ("
					APR_STRINGIFY(MAX_KEEPNUM) ")");
			arg_keepnum = MAX_KEEPNUM;
		} else {
			if (num < 0) {
				arg_keepnum = -1;
			} else {
				arg_keepnum = (int)num;
			}
		}
	} else if (selected & cmd_keepall) {
		arg_keepnum = -1;
	}

	if (selected & cmd_logdir) {
		die_assert(arg_logdir);
	} else {
		arg_logdir = DIR_TAINLOG;
	}

	/* Log file size */
	if (selected & cmd_file) {
		apr_int64_t num;

		die_assert(arg_file);
		num = apr_atoi64(arg_file);
		
		/* Make sure we have a sane value */
		if (num > MAX_FILESIZE) {
			warn_error1("argument too big, using maximum ("
					APR_STRINGIFY(MAX_FILESIZE) ")");
			arg_filesize = MAX_FILESIZE;
		} else if (num < MIN_BUFSIZE) {
			warn_error1("argument too small, using minimum ("
					APR_STRINGIFY(MIN_FILESIZE) ")");
			arg_filesize = MIN_FILESIZE;
		} else {
			arg_filesize = (int)num;
		}
	}

	/* Line buffer size */
	if (selected & cmd_buffer) {
		apr_int64_t num;

		die_assert(arg_buffer);
		num = apr_atoi64(arg_buffer);
		
		/* Make sure we have a sane value */
		if (num > MAX_BUFSIZE) {
			warn_error1("argument too big, using maximum ("
					APR_STRINGIFY(MAX_BUFSIZE) ")");
			arg_bufsize = MAX_BUFSIZE;
		} else if (num < MIN_BUFSIZE) {
			warn_error1("argument too small, using minimum ("
					APR_STRINGIFY(MIN_BUFSIZE) ")");
			arg_bufsize = MIN_BUFSIZE;
		} else {
			arg_bufsize = (int)num;
		}
	}

	return 0;
}

/* Waits for input from g_apr_stdin, reads an entire line to the buffer. Starts
 * filling the buffer at position start and reads at most start - len bytes. If
 * the line fits in the buffer, the buffer is terminated with '\n'. If stamp is
 * given, it is filled with a TAI64N label when the first byte is read. Ignores
 * interrupts, keeps reading until receives EOF, '\n', or the buffer fills.
 */
static int readline(char *buffer, apr_size_t *pos, apr_size_t start,
		apr_size_t len, ngim_tain_t *stamp)
{
	apr_status_t status;

	die_assert(buffer);
	die_assert(pos);
	die_assert(start < len);

	/* Input starts here */
	*pos = start;

	do {
		/* This never returns APR_EINTR */
		if (APR_FAIL(status, apr_file_getc(&buffer[*pos], g_apr_stdin))) {
			if (APR_STATUS_IS_EOF(status)) {
				flag_eof = 1;
			} else {
				warn_aprerror1(status, "failed to read from stdin");
				apr_sleep(apr_time_from_sec(PAUSE_READLINE));
			}
		} else {
			/* The timestamp at the start of the buffer indicates the time
			 * the first character of the line was read */
			if (*pos == start && stamp) {
				ngim_tain_now(stamp);
			}

			/* Done if we have an entire line, only increment after a
			 * successful read */
			if (buffer[(*pos)++] == '\n') {
				break;
			}
		}
	} while (*pos < len && !flag_eof);

	/* Return non-zero if something was read */
	return (*pos > start);
}

/* Tries to gain an exclusive lock for FILE_CURRENT. Closes the file if
 * fails. */
static void lock_tainlog()
{
	apr_status_t status;

	die_assert(current);
	
	if (APR_FAIL(status, apr_file_lock(current,
			APR_FLOCK_EXCLUSIVE | APR_FLOCK_NONBLOCK))) {
		/* Close the file and try again later */
		warn_aprerror1(status, "failed to lock " FILE_CURRENT);
		apr_file_close(current);
		current = NULL;
	}
}

/* Opens FILE_CURRENT for writing. If it doesn't exist, creates a new file. If
 * there was an error, current is set to NULL. The apr_file_t pointed to by
 * current is stored in pool, which is cleared before the file is opened. The
 * pool should not be cleared again until close_tainlog has been called. */
static void open_tainlog(apr_pool_t *pool)
{
	apr_status_t status;
	apr_finfo_t info;

	die_assert(pool);
	
	if (current) {
		/* The file is already open */
		return;
	}

	/* Clear the pool before opening, while we still can */
	apr_pool_clear(pool);
	
	/* Initialize */
	current_size = 0;

	/* Open the current log file */
	if (APR_FAIL(status,
			apr_stat(&info, FILE_CURRENT, APR_FINFO_NORM, pool))) {
		if (APR_STATUS_IS_ENOENT(status)) {
			/* Create a new file */
			if (APR_FAIL(status, apr_file_open(&current, FILE_CURRENT,
					APR_FOPEN_WRITE | APR_FOPEN_CREATE,
					FPROT_FILE_CURRENT, pool))) {
				warn_aprerror1(status, "failed to create " FILE_CURRENT);
			} else {
				/* Lock the file */
				lock_tainlog();
			}
		} else {
			warn_aprerror1(status, "stat failed for " FILE_CURRENT);
		}
	} else if (info.filetype != APR_REG) {
		warn_error1("failed to open " FILE_CURRENT ": Not a file");
	}
#if TAINLOG_SET_PERMS_FOR_EXISTING
	else if (APR_FAIL(status,
				apr_file_perms_set(FILE_CURRENT, FPROT_FILE_CURRENT))) {
		warn_aprerror1(status, "failed to set permissions for " FILE_CURRENT);
	}
#endif
	else {
		/* Open the existing file */
		if (APR_FAIL(status,
				apr_file_open(&current, FILE_CURRENT, APR_FOPEN_WRITE |
					APR_FOPEN_APPEND, FPROT_FILE_CURRENT, pool))) {
			warn_aprerror1(status, "failed to open " FILE_CURRENT);
		} else {
			current_size = info.size;
			/* Lock the file */
			lock_tainlog();
		}
	}
}

/* Creates the arg_logdir subdirectory and chdirs to it. Opens FILE_CURRENT. */
static void setup_tainlog(apr_pool_t *pool)
{
	die_assert(pool);

	if (ngim_create_directory(arg_logdir, FPROT_DIR_TAINLOG,
			TAINLOG_SET_PERMS_FOR_EXISTING, pool) < 0) {
		die_error2("failed to set up directory ", arg_logdir);
	}
	
	if (chdir(arg_logdir) == -1) {
		die_syserror3("chdir to ", arg_logdir, " failed");
	}

	/* If this fails, input is simply discarded until it succeeds again */
	open_tainlog(pool);
}

/* If current is non-NULL, closes it. Then archives FILE_CURRENT to a name
 * consisting of the given TAI64N label. */
static void close_tainlog(ngim_tain_t *stamp, apr_pool_t *pool)
{
	apr_status_t status;
	char name[NGIM_TAIN_FORMAT + 1];

	die_assert(stamp);
	die_assert(pool);
	
	/* Close current if its open */
	if (current) {
		apr_file_unlock(current);
		apr_file_close(current);
		current = NULL;
	}

	/* The name for the archived log file is the time stamp of the first line
	 * written to the next log file. It is very unlikely that another file has
	 * the same name, but if one does, we simply (try to) overwrite it */
	
	ngim_tain_format(name, stamp);
	name[NGIM_TAIN_FORMAT] = '\0';
	
	if (APR_FAIL(status, apr_file_rename(FILE_CURRENT, name, pool))) {
		/* If renaming fails, we just keep writing to FILE_CURRENT and
		 * try again later */
		warn_aprerror1(status, "failed to archive " FILE_CURRENT);
	}
}

/* Walks through the archived log files in the current directory. If there are
 * more than arg_keepnum files, keeps removing the oldest until there isn't. */
static void flush_archive(apr_pool_t *pool)
{
	apr_status_t status;
	apr_dir_t *directory;
	apr_finfo_t info;
	int files;
	char oldest[NGIM_TAIN_FORMAT + 1];

	die_assert(pool);

	if (arg_keepnum < 0) {
		/* Flushing archived log files is disabled */
		return;
	}

	/* Usually, we need to remove only one log file, so this is not meant to
	 * be very efficient for a large number of files */
	do {
		files = 0;

		if (APR_FAIL(status, apr_dir_open(&directory, ".", pool))) {
			warn_aprerror3(status, "failed to open ", arg_logdir,
				", not flushing archived log files");
			break;
		} else {
			while (apr_dir_read(&info, APR_FINFO_NORM,
						directory) == APR_SUCCESS) {
				die_assert(info.name);

				/* Count the number of log files */
				if (info.filetype == APR_REG && info.name[0] == '@' &&
					strlen(info.name) == NGIM_TAIN_FORMAT) {
					/* Remember the name of the oldest file */
					if (!files || strcmp(info.name, oldest) < 0) {
						/* Always NUL-terminates */
						apr_cpystrn(oldest, info.name, NGIM_TAIN_FORMAT + 1);
					}
					++files;
				}
			}
			apr_dir_close(directory);
		}

		/* If there are too many log files, remove the oldest */
		if (files > arg_keepnum) {
			if (APR_FAIL(status, apr_file_remove(oldest, pool))) {
				warn_aprerror2(status, "failed to remove file ", oldest);
				break;
			} else {
				--files;
			}
		}
	} while (files > arg_keepnum);
}

/* Formats a line that has been read in the buffer starting from BUFFER_START
 * by prepending it with the given TAI64N label. If the line was wrapped, this
 * is indicated by the separator between the timestamp and the line. */
static inline void format_tainlog(char *buffer, apr_size_t *len,
		ngim_tain_t *stamp, int *wrapped)
{
	die_assert(buffer);
	die_assert(len);
	die_assert(stamp);
	die_assert(wrapped);
	
	die_assert(*len < arg_bufsize);
	
	/* Prepend the line with a timestamp */
	ngim_tain_format(buffer, stamp);

	/* If the previous line was wrapped, indicate it with a tab as the
	 * separator, otherwise use a space */
	buffer[BUFFER_SEPARATOR] = (*wrapped) ? '\t' : ' ';

	*wrapped = (buffer[*len - 1] != '\n');
	
	/* Make sure the buffer ends with a newline */
	if (*wrapped) {
		buffer[(*len)++] = '\n';
	}
}

/* Writes out a buffer with length len to the file current, and increases
 * current_size by the number of bytes actually written. If current is full,
 * archives it. If current cannot be opened, prints out a warning and
 * discards the buffer. */
static inline void append_tainlog(char *buffer, apr_size_t len,
		ngim_tain_t *stamp, apr_pool_t *pool)
{
	apr_status_t status;
	apr_size_t written;
	
	die_assert(buffer);
	die_assert(pool);

	die_assert(len > 0 && len <= arg_bufsize);
	
	/* If current is full, archive it */
	if (current_size + len > arg_filesize) {
		close_tainlog(stamp, pool);
		flush_archive(pool);
	}
	
	open_tainlog(pool);
	
	if (current) {
		if (APR_FAIL(status,
				apr_file_write_full(current, buffer, len, &written))) {
			warn_aprerror1(status, "failed to write to " FILE_CURRENT);
		}
		current_size += written;
	} else {
		warn_error1("discarding buffer");
	}
}

/* Reads input from stdin, writes every line to FILE_CURRENT prepended by a
 * time stamp. */
static int tainlog(const char *root)
{
	apr_status_t status;
	apr_pool_t *pool;
	apr_size_t len;
	ngim_tain_t stamp;
	int wrapped = 0;
	char *buffer;

	/* Drop unneeded privileges */
	if (ngim_priv_drop(NGIM_PRIV_NONE, arg_user, arg_group) < 0) {
		die_error1("failed to drop privileges");
	}

	die_assert(root);

	if (chdir(root) == -1) {
		die_syserror3("chdir to ", root, " failed");
	}

	if (ALLOC_FAIL(buffer, apr_pcalloc(g_pool, arg_bufsize))) {
		die_allocerror0();
	}

	if (APR_FAIL(status, apr_pool_create(&pool, g_pool))) {
		die_aprerror1(status, "failed to create a memory pool");
	}

	setup_tainlog(pool);

	/* After this point, the program should not die in vain */
	do {
		if (readline(buffer, &len, BUFFER_START, arg_bufsize - 1, &stamp)) {
			format_tainlog(buffer, &len, &stamp, &wrapped);
			append_tainlog(buffer, len, &stamp, pool);
		}
	} while (!flag_eof);

	return EXIT_SUCCESS;
}

int main(int argc, const char * const * argv, const char * const *env)
{
	apr_uint32_t selected;
	char *progname;

	ngim_base_app_init(PROGRAM_TAINLOG, &argc, &argv, &env);

	/* Ignore signals and keep reading until we get EOF (or SIGKILL) */
	apr_signal(SIGHUP,	SIG_IGN);
	apr_signal(SIGINT,	SIG_IGN);
	apr_signal(SIGQUIT,	SIG_IGN);
	apr_signal(SIGTERM,	SIG_IGN);

	if (ngim_cmdline_parse(argc, argv, 1, logger_params, logger_args,
			&selected) < 0 ||
		validate_cmdline(selected) < 0) {
		die_error4("usage: ", argv[0], " ", CMDLINE_USAGE);
	}

	/* Program name */
	if (ALLOC_FAIL(progname,
			apr_psprintf(g_pool, PROGRAM_TAINLOG " [pid %i]",
				getpid()))) {
		warn_allocerror0();
	} else {
		ngim_setprogname(progname);
	}

	return tainlog(arg_root);
}
