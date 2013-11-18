/*
 * srvctl.c
 *
 * Copyright © 2005, 2006  Sami Tolvanen <sami@ngim.org>
 */

#include "common.h"
#include "srvctl.h"
#include <ngim/base.h>
#include <apr_env.h>
#include <apr_file_info.h>
#include <apr_file_io.h>
#include <apr_portable.h>
#include <apr_strings.h>

/* Misc. definitions */
#define PRIORITY_MAXLEN		512

/* Function pointer type for the ISO 8601 conversion */
typedef void (*iso8601_format)(char *s, apr_time_t t);

/* Bitmasks for command line parameters */
enum {
	cmd_help		= 1 << 0,
	cmd_base		= 1 << 1,
	cmd_down		= 1 << 2,
	cmd_kill		= 1 << 3,
	cmd_killall		= 1 << 4,
	cmd_list		= 1 << 5,
	cmd_name		= 1 << 6,
	cmd_priority	= 1 << 7,
	cmd_restart		= 1 << 8,
	cmd_signal		= 1 << 9,
	cmd_sigterm		= 1 << 10,
	cmd_start		= 1 << 11,
	cmd_status		= 1 << 12,
	cmd_stop		= 1 << 13,
	cmd_term		= 1 << 14,
	cmd_up			= 1 << 15,
	cmd_utc			= 1 << 16
};

/* Variables for command line arguments */
static const char *arg_base = NULL;
static const char *arg_sign = NULL;
static const char *arg_name = NULL;
static const char *arg_priority = NULL;
static int arg_signum = 0;
static iso8601_format arg_func_format = NULL;

/* Command line parameters and arguments */
static ngim_cmdline_params_t service_params[] = {
	{ "--help",		cmd_help,		NULL },
	{ "-h",			cmd_help,		NULL },
	{ "--base",		cmd_base,		&arg_base },
	{ "--down",		cmd_down,		NULL },
	{ "--kill-all",	cmd_killall,	NULL },
	{ "--kill",		cmd_kill,		NULL },
	{ "--list",		cmd_list,		NULL },
	{ "--name",		cmd_name,		&arg_name },
	{ "--priority",	cmd_priority,	&arg_priority },
	{ "--restart",	cmd_restart,	NULL },
	{ "--signal",	cmd_signal,		&arg_sign },
	{ "--sigterm",	cmd_sigterm,	&arg_sign },
	{ "--start",	cmd_start,		NULL },
	{ "--status",	cmd_status,		NULL },
	{ "--stop", 	cmd_stop,		NULL },
	{ "--term",		cmd_term,		NULL },
	{ "--up",		cmd_up,			NULL },
	{ "--utc",		cmd_utc,		NULL },
	{ NULL,			0,				NULL }
};
static ngim_cmdline_args_t service_args[] = {
	{ &arg_name },
	{ NULL }
};

#define CMDLINE_USAGE \
	"--help | [ --base directory ] {1}\n" \
	"    1: --list | --status [ --utc ] | {2} [ --name ] service | --kill-all\n" \
	"    2: --priority number | --up | --down | --start | --restart | --stop | --kill | {3} | --term\n" \
	"    3: --signal {4} | --sigterm {4}\n" \
	"    4: ALRM | CONT | HUP | STOP | TERM | USR1 | USR2 | WINCH\n" \
	"\n" \
	"    Basic operations:\n" \
	"      --help      prints this message\n" \
	"      --base      sets the base service directory\n" \
	"      --list      prints information about available services\n" \
	"      --status    prints information about active services\n" \
	"      --utc       prints status times in the UTC time zone\n" \
	"      --name      sets the name of the targeted service\n" \
	"      --kill-all  restarts all active services and monitors\n" \
	"\n" \
	"    Service operations:\n" \
	"      --priority  sets a scanning priority for the service\n" \
	"      --up        tells the monitor to restart service if it dies (default)\n" \
	"      --down      tells the monitor not to restart service if it dies\n" \
	"      --start     starts a service\n" \
	"      --restart   restarts a service\n" \
	"      --stop      stops a service\n" \
	"      --kill      restarts a service and its monitor\n" \
    "      --signal    sends a signal to a service process\n" \
    "      --sigterm   same as --down followed by --signal\n" \
	"      --term      same as --sigterm TERM\n"

/* Signals allowed to be sent to services */
static const struct {
	const char *name;
	int signum;
} service_signals[] = {
	{ "ALRM",		SIGALRM	 },
	{ "CONT",		SIGCONT	 },
	{ "HUP",		SIGHUP	 },
	{ "STOP",		SIGSTOP	 },
	{ "TERM",		SIGTERM	 },
	{ "USR1",		SIGUSR1	 },
	{ "USR2",		SIGUSR2	 },
	{ "WINCH",		SIGWINCH },
	{ NULL,			0		 }
};

/* Human-readable status message formats */
#define LIST_MESSAGE_FORMAT \
	"\t%i. service %s\n" \
		"\t\tactive %s\n" \
		"\t\trun %s\n" \
		"\t\tlog %s\n" \
		"\t\tpriority %s\n"
#define STATUS_MESSAGE_FORMAT \
	"\t%i. service %s\n" \
		"\t\tupdated %s\n" \
		"\t\trun %s\n" \
		"\t\tlog %s\n" \
		"\t\tlogging %s\n" \
		"\t\twants %s\n"
#define STATUS_MESSAGE_RUNNING_FORMAT_M \
	"pid %u up %u min %u s"
#define STATUS_MESSAGE_RUNNING_FORMAT_H \
	"pid %u up %u h %u min %u s"
#define STATUS_MESSAGE_RUNNING_FORMAT_D \
	"pid %u up %" APR_UINT64_T_FMT " d %u h %u min %u s"
#define STATUS_MESSAGE_NOTRUNNING		"not running"

/* Finds a signal number for a signal name from the list of allowed signals. */
static int service_signal_byname(const char *name)
{
	int i;

	die_assert(name);

	for (i = 0; service_signals[i].name; ++i) {
		if (!strcmp(name, service_signals[i].name)) {
			return service_signals[i].signum;
		}
	}
	return -1;
}

/* Validates command line. Present parameters are specified in selected.
 * Prints an error message and returns <0 if command line is invalid. */
static int validate_cmdline(const apr_uint32_t selected)
{
	apr_status_t status;
	int commands = 0, i;

	if (selected & cmd_help) {
		return -1;
	}

	/* Base is optional */
	if (selected & cmd_base) {
		die_assert(arg_base);
	} else if (APR_FAIL(status, apr_env_get((char**)&arg_base,
					ENV_SRVCTL_BASE, g_pool))) {
		arg_base = DIR_BASE;
	}

	/* Count the number of commands */
	if (selected & cmd_priority) {
		++commands;

		die_assert(arg_priority);

		if (strlen(arg_priority) > PRIORITY_MAXLEN) {
			warn_error1("name too long");
			return -1;
		}

		for (i = 0; arg_priority[i] != '\0'; ++i) {
			if (arg_priority[i] < '0' || arg_priority[i] > '9') {
				warn_error1("invalid value for --priority");
				return -1;
			}
		}
	}

	if (selected & cmd_up) {
		++commands;
	}
	if (selected & cmd_down) {
		++commands;
	}
	if (selected & cmd_restart) {
		++commands;
	}
	if (selected & cmd_signal || selected & cmd_sigterm) {
		++commands;

		die_assert(arg_sign);

		/* Signal name */
		arg_signum = service_signal_byname(arg_sign);
		if (arg_signum < 0) {
			warn_error2("unknown signal ", arg_sign);
			return -1;
		}

		die_assert(arg_signum > 0 && arg_signum < NSIG);
	}
	if (selected & cmd_start) {
		++commands;
	}
	if (selected & cmd_stop) {
		++commands;
	}
	if (selected & cmd_term) {
		++commands;
	}
	if (selected & cmd_kill) {
		++commands;
	}

	/* Must have either list, status, killall, or name */
	if ((selected & cmd_list    && arg_name) ||
		(selected & cmd_status  && arg_name) ||
		(selected & cmd_killall && arg_name) ||
		(selected & cmd_status  && selected & cmd_list) ||
		(selected & cmd_list    && selected & cmd_killall) ||
		(selected & cmd_killall && selected & cmd_status) ||
		(selected & cmd_utc     && !(selected & cmd_status))) {
		warn_error1("invalid parameters");
		return -1;
	} else if (selected & cmd_status) {
		/* If status, no commands */
		if (commands > 0) {
			warn_error1("invalid parameters");
			return -1;
		}
		/* Default to local time zone */
		if (selected & cmd_utc) {
			arg_func_format = ngim_iso8601_utc_format;
		} else {
			arg_func_format = ngim_iso8601_local_format;
		}
	} else if (selected & cmd_killall || selected & cmd_list) {
		/* If killall or list, no commands */
		if (commands > 0) {
			warn_error1("invalid parameters");
			return -1;
		}
	} else if (arg_name) {
		/* If name, exactly one command */
		if (commands > 1) {
			warn_error1("too many commands");
			return -1;
		} else if (!commands) {
			warn_error1("missing command");
			return -1;
		}
	} else {
		warn_error1("invalid parameters");
		return -1;
	}

	return 0;
}

/*
 * Names
 */

static char * service_realname(const char *active)
{
	char *path, *basename;

	die_assert(active);
	die_assert(arg_base);

	/* Resolve the symbolic link path */
	if (ALLOC_FAIL(path, apr_psprintf(g_pool, "%s/" DIR_ACTIVE "/%s",
					arg_base, active))) {
		die_allocerror0();
	}

	if (ngim_resolve_symlink_basename(path, &basename, g_pool) < 0) {
		die_error1("failed to resolve service name");
	}

	return basename;
}

static const char * service_linkname()
{
	apr_status_t status;
	apr_file_t *file;
	char *path, *priority;
	int i;

	die_assert(arg_base);
	die_assert(arg_name);

	if (ALLOC_FAIL(path, apr_psprintf(g_pool,
			"%s/" DIR_ALL "/%s/" FILE_PRIORITY, arg_base, arg_name)) ||
		ALLOC_FAIL(priority, apr_pcalloc(g_pool, PRIORITY_MAXLEN + 1))) {
		die_allocerror0();
	}

	if (APR_FAIL(status, apr_file_open(&file, path, APR_READ | APR_BINARY,
					FPROT_FILE_PRIORITY, g_pool))) {
		if (APR_STATUS_IS_ENOENT(status)) {
			return arg_name;
		} else {
			die_aprerror2(status, "failed to open file ", path);
		}
	}

	if (APR_FAIL(status, apr_file_gets(priority, PRIORITY_MAXLEN, file))) {
		apr_file_close(file);
		if (APR_STATUS_IS_EOF(status)) {
			return arg_name;
		} else {
			die_aprerror2(status, "failed to read from file ", path);
		}
	}

	apr_file_close(file);

	for (i = 0; priority[i] != '\0'; ++i) {
		if (priority[i] == '\n') {
			priority[i] = '\0';
			break;
		} else if (priority[i] < '0' || priority[i] > '9') {
			return arg_name;
		}
	}
	if (priority[0] == '\0') {
		return arg_name;
	}

	return priority;
}

/*
 * Existence
 */

static int service_file_exists(const char *file)
{
	apr_status_t status;
	apr_finfo_t info;
	char *path;

	die_assert(arg_base);
	die_assert(arg_name);

	/* Path to file in DIR_ALL */
	if (ALLOC_FAIL(path,
			apr_psprintf(g_pool, "%s/" DIR_ALL "/%s/%s", arg_base,
				arg_name, file))) {
		die_allocerror0();
	}

	if (APR_FAIL(status,
			apr_stat(&info, path, APR_FINFO_NORM, g_pool))) {
		if (APR_STATUS_IS_ENOENT(status)) {
			return 0;
		} else {
			die_aprerror2(status, "stat failed for ", file);
		}
	}

	return 1;
}

/* Returns non-zero if arg_base/all/arg_name exists and is a directory. */
static int service_exists()
{
	apr_status_t status;
	apr_finfo_t info;
	char *path;

	die_assert(arg_base);
	die_assert(arg_name);

	/* Path to service in DIR_ALL */
	if (ALLOC_FAIL(path,
			apr_psprintf(g_pool, "%s/" DIR_ALL "/%s", arg_base, arg_name))) {
		die_allocerror0();
	}

	if (APR_FAIL(status,
			apr_stat(&info, path, APR_FINFO_NORM, g_pool))) {
		if (APR_STATUS_IS_ENOENT(status) || APR_STATUS_IS_ENOTDIR(status)) {
			warn_error2(path, " does not exist");
			return 0;
		} else {
			die_aprerror2(status, "stat failed for ", path);
		}
	} else if (info.filetype != APR_DIR) {
		warn_error2(path, " is not a directory");
		return 0;
	}

	error1(VERBOSE, "exists");
	return 1;
}

/*
 * "Up" file
 */

/* If arg_base/all/arg_name/monitor/up doesn't exist, create it. Dies if
 * fails. */
static void service_create_up()
{
	apr_status_t status;
	apr_file_t *file;
	char *path;

	die_assert(arg_base);
	die_assert(arg_name);

	/* Path to DIR_MONITOR */
	if (ALLOC_FAIL(path,
			apr_psprintf(g_pool, "%s/" DIR_ALL "/%s/" DIR_MONITOR,
				arg_base, arg_name))) {
		die_allocerror0();
	}

	/* Make sure DIR_MONITOR exists for the service */
	if (APR_FAIL(status,
			apr_dir_make_recursive(path, FPROT_DIR_MONITOR, g_pool))) {
		/* Doesn't fail if directory already exists */
		die_aprerror2(status, "failed to create directory ", path);
	}

	/* Path to FILE_UP for the service */
	if (ALLOC_FAIL(path,
			apr_psprintf(g_pool, "%s/" DIR_ALL "/%s/" FILE_UP,
				arg_base, arg_name))) {
		die_allocerror0();
	}

	/* Create FILE_UP */
	if (APR_FAIL(status,
			apr_file_open(&file, path, APR_FOPEN_WRITE | APR_FOPEN_CREATE |
				APR_FOPEN_TRUNCATE, FPROT_FILE_UP, g_pool))) {
		die_aprerror2(status, "failed to create file ", path);
	}

	apr_file_close(file);
}

/* If arg_base/all/arg_name/monitor/up exists, removes it. Dies if fails. */
static void service_remove_up()
{
	apr_status_t status;
	char *path;

	die_assert(arg_base);
	die_assert(arg_name);

	/* Path to FILE_UP for the service */
	if (ALLOC_FAIL(path,
			apr_psprintf(g_pool, "%s/" DIR_ALL "/%s/" FILE_UP,
				arg_base, arg_name))) {
		die_allocerror0();
	}

	/* Remove FILE_UP if one exists */
	if (APR_FAIL(status, apr_file_remove(path, g_pool))) {
		if (APR_STATUS_IS_ENOENT(status) || APR_STATUS_IS_ENOTDIR(status)) {
			/* Didn't exist, just as well */
		} else {
			die_aprerror2(status, "failed to remove file ", path);
		}
	}
}

/*
 * Activation
 */

/* Returns non-zero if arg_base/active/arg_name exists and is a symbolic
 * link. */
static int service_active()
{
	apr_status_t status;
	apr_dir_t *dir;
	apr_finfo_t info;
	char *path, *name;

	die_assert(arg_base);
	die_assert(arg_name);

	/* Path to DIR_ACTIVE */
	if (ALLOC_FAIL(path, apr_psprintf(g_pool, "%s/" DIR_ACTIVE, arg_base))) {
		die_allocerror0();
	}

	if (APR_FAIL(status, apr_dir_open(&dir, path, g_pool))) {
		die_aprerror2(status, "failed to open directory ", path);
	}

	while (apr_dir_read(&info, APR_FINFO_DIRENT, dir) == APR_SUCCESS) {
		die_assert(info.name);

		/* All valid entries in DIR_ACTIVE are symbolic links */
		if (info.filetype != APR_LNK || info.name[0] == '.') {
			continue;
		}

		if (ALLOC_FAIL(name, service_realname(info.name))) {
			continue;
		}

		if (!strcmp(arg_name, name)) {
			apr_dir_close(dir);
			error1(VERBOSE, "is active");
			return 1;
		}
	}

	apr_dir_close(dir);

	error1(VERBOSE, "is not active");
	return 0;
}

/* Creates a symlink from ../all/arg_name to arg_base/active/arg_name. Dies if
 * fails. */
static void service_add()
{
	apr_status_t status;
	char *newpath;
	char *oldpath;

	die_assert(arg_base);
	die_assert(arg_name);

	/* Path to DIR_ACTIVE */
	if (ALLOC_FAIL(newpath,
			apr_psprintf(g_pool, "%s/" DIR_ACTIVE, arg_base))) {
		die_allocerror0();
	}

	/* Make sure DIR_ACTIVE exists for the base */
	if (APR_FAIL(status,
			apr_dir_make_recursive(newpath, FPROT_DIR_ACTIVE, g_pool))) {
		/* Doesn't fail if directory already exists */
		die_aprerror2(status, "failed to create directory ", newpath);
	}

	/* Path to the link we are creating in DIR_ACTIVE */
	if (ALLOC_FAIL(newpath,
			apr_psprintf(g_pool, "%s/" DIR_ACTIVE "/%s",
				arg_base, service_linkname()))) {
		die_allocerror0();
	}

	/* Relative path to service directory in DIR_ALL */
	if (ALLOC_FAIL(oldpath,
			apr_psprintf(g_pool, "../" DIR_ALL "/%s", arg_name))) {
		die_allocerror0();
	}

	/* Create the symbolic link */
	if (ngim_create_symlink(oldpath, newpath) < 0) {
		die_error1("failed to activate service");
	}
}

/* Tries to remove the symlink arg_base/active/arg_name. Dies if fails. */
static void service_remove()
{
	apr_status_t status;
	char *path;

	/* Path to service in DIR_ACTIVE */
	if (ALLOC_FAIL(path,
			apr_psprintf(g_pool, "%s/" DIR_ACTIVE "/%s",
				arg_base, service_linkname()))) {
		die_allocerror0();
	}

	/* Try to remove the link if it exists */
	if (APR_FAIL(status, apr_file_remove(path, g_pool))) {
		if (APR_STATUS_IS_ENOENT(status) || APR_STATUS_IS_ENOTDIR(status)) {
			/* Didn't exist, just as well */
		} else {
			die_aprerror2(status, "failed to remove symbolic link ", path);
		}
	}
}

/*
 * Priority
 */

static void service_priority()
{
	int active = service_active();
	apr_status_t status;
	apr_file_t *file;
	char *path;

	die_assert(arg_base);
	die_assert(arg_name);
	die_assert(arg_priority);

	if (ALLOC_FAIL(path, apr_psprintf(g_pool,
			"%s/" DIR_ALL "/%s/" FILE_PRIORITY, arg_base, arg_name))) {
		die_allocerror0();
	}

	if (active) {
		service_remove();
	}

	if (APR_FAIL(status, apr_file_open(&file, path, APR_WRITE | APR_BINARY |
					APR_CREATE | APR_TRUNCATE, FPROT_FILE_PRIORITY, g_pool))) {
		if (active) {
			service_add();
		}
		die_aprerror2(status, "failed to open file ", path);
	}

	/* Write priority to file */
	if (APR_FAIL(status, apr_file_puts(arg_priority, file))) {
		apr_file_close(file);
		if (active) {
			service_add();
		}
		die_aprerror2(status, "failed to write to file ", path);
	}

	apr_file_close(file);

	if (active) {
		service_add();
	}
}

/*
 * Monitor commands
 */

/* Tries to open monitor/control for writing write (char)cmd, dies if fails. */
static void monitor_command(int cmd, int nonblocking)
{
	apr_status_t status;
	apr_file_t *control;
	char *path;

	die_assert(arg_name);

	/* Path to PIPE_CONTROl for the service */
	if (ALLOC_FAIL(path,
			apr_psprintf(g_pool, "%s/" DIR_ALL "/%s/" PIPE_CONTROL,
				arg_base, arg_name))) {
		die_allocerror0();
	}

	if (nonblocking) {
		/* TODO: APR doesn't allow O_NONBLOCK on open or setting timeout on
		 * non-pipe descriptors */
#if HAVE_OPEN && defined(O_NONBLOCK)
		int fd = open(path, O_WRONLY | O_NONBLOCK);

		if (fd == -1) {
			/* Monitor not running */
			error2(INFO, "monitor not running for ", arg_name);
			return;
		}

		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);

		if (APR_FAIL(status, apr_os_file_put(&control, &fd, 0, g_pool))) {
			die_aprerror2(status, "failed to open ", path);
		}
#else
		die_error1("nonblocking control commands not supported");
#endif
	} else if (APR_FAIL(status, apr_file_open(&control, path,
					APR_FOPEN_WRITE | APR_FOPEN_APPEND | APR_FOPEN_BINARY,
					FPROT_PIPE_CONTROL, g_pool))) {
		if (APR_STATUS_IS_ENOENT(status) || APR_STATUS_IS_ENOTDIR(status)) {
			/* Didn't exist, doesn't matter */
			return;
		} else {
			die_aprerror2(status, "failed to open ", path);
		}
	}

	if (APR_FAIL(status, apr_file_putc((char)cmd, control))) {
		die_aprerror2(status, "failed to write command to ", path);
	}

	apr_file_close(control);
}

/* Returns non-zero if arg_base/all/name/FILE_UP exists. */
static int service_wantup(const char *name)
{
	apr_status_t status;
	apr_finfo_t info;
	char *path;

	die_assert(name);
	die_assert(arg_base);

	/* Path to arg_base/all/name/FILE_UP */
	if (ALLOC_FAIL(path,
			apr_psprintf(g_pool, "%s/" DIR_ALL "/%s/" FILE_UP,
				arg_base, name))) {
		die_allocerror0();
	}

	if (APR_FAIL(status, apr_stat(&info, path, APR_FINFO_NORM, g_pool))) {
		if (APR_STATUS_IS_ENOENT(status)) {
			return 0;
		} else {
			die_aprerror2(status, "stat failed for ", path);
		}
	}

	return 1;
}

/*
 * Display formatting
 */

static inline const char * format_flag(int flag)
{
	return flag ? "yes" : "no";
}

static inline const char * format_wantup(const char *name)
{
	return service_wantup(name) ? "up" : "down";
}

static inline const char * format_exists(int exists)
{
	return (exists) ? "exists" : "does not exist";
}

static inline const char * format_priority()
{
	const char *priority = service_linkname();

	if (arg_name == priority) {
		return "not set";
	}

	return priority;
}

/* Formats process information */
static const char * format_proc(const unsigned char *packed,
		const apr_uint32_t *pid, apr_pool_t *pool)
{
	char *msg;

	die_assert(packed);
	die_assert(pid);
	die_assert(pool);

	if (*pid) {
		apr_uint64_t days, uptime = 0;
		apr_uint32_t hours, minutes, seconds;
		ngim_tain_t now;
		ngim_tain_t changed;

		/* Calculate the approximate uptime */
		if (ngim_tain_unpack(packed, &changed)) {
			ngim_tain_now(&now);
			ngim_tain_diff(&changed, &now, &uptime);
		}

		seconds = (apr_uint32_t)(uptime        % 60);
		minutes = (apr_uint32_t)(uptime / 60   % 60);
		hours   = (apr_uint32_t)(uptime / 3600 % 24);
		days    = uptime / 86400;

		if (days > 0) {
			if (ALLOC_FAIL(msg,
					apr_psprintf(pool, STATUS_MESSAGE_RUNNING_FORMAT_D,
						*pid, days, hours, minutes, seconds))) {
				die_allocerror0();
			}
		} else if (hours > 0) {
			if (ALLOC_FAIL(msg,
					apr_psprintf(pool, STATUS_MESSAGE_RUNNING_FORMAT_H,
						*pid, hours, minutes, seconds))) {
				die_allocerror0();
			}
		} else {
			if (ALLOC_FAIL(msg,
					apr_psprintf(pool, STATUS_MESSAGE_RUNNING_FORMAT_M,
						*pid, minutes, seconds))) {
				die_allocerror0();
			}
		}
	} else {
		msg = STATUS_MESSAGE_NOTRUNNING;
	}

	return msg;
}

/*
 * Service status
 */

/* Formats service status according to STATUS_MESSAGE_FORMAT. */
static const char * format_status(int *counter, const char *name,
		const unsigned char *filebuf, apr_pool_t *pool)
{
	char *msg, *realname;
	ngim_tain_t stamp;
	char formatted[NGIM_ISO8601_FORMAT];

	die_assert(counter);
	die_assert(name);
	die_assert(filebuf);
	die_assert(pool);
	die_assert(arg_func_format);

	/* Unpack and format to ISO8601 */
	if (ngim_tain_unpack(&filebuf[MONITOR_STATUS_UPDATED], &stamp)) {
		arg_func_format(formatted, ngim_tain_to_apr(&stamp));
	} else {
		formatted[0] = '?';
		formatted[1] = '\0';
	}

	arg_name = name;
	realname = service_realname(name);

	/* Format the message */
	if (ALLOC_FAIL(msg,
			 apr_psprintf(pool, STATUS_MESSAGE_FORMAT,
				++(*counter),
				realname,
				formatted,
				format_proc(&filebuf[MONITOR_STATUS_CHG_RUN],
					(apr_uint32_t*)&filebuf[MONITOR_STATUS_PID_RUN], pool),
				format_proc(&filebuf[MONITOR_STATUS_CHG_LOG],
					(apr_uint32_t*)&filebuf[MONITOR_STATUS_PID_LOG], pool),
				format_flag(filebuf[MONITOR_STATUS_FORWARD]),
				format_wantup(realname)))) {
		die_allocerror0();
	}

	return msg;
}

/* For each link in arg_base/active, resolves it, and for each directory,
 * looks for the file monitor/status and outputs its contents in human-
 * readable format. */
static void command_status()
{
	apr_status_t status;
	apr_pool_t *pool;
	apr_dir_t *dir;
	apr_finfo_t info;
	apr_file_t *file;
	int counter = 0;
	char *path;
	unsigned char filebuf[MONITOR_STATUS_SIZE];

	die_assert(arg_base);

	/* Create a subpool for scanning */
	if (APR_FAIL(status, apr_pool_create(&pool, g_pool))) {
		die_aprerror1(status, "failed to create a memory pool");
	}

	/* Path to DIR_ACTIVE */
	if (ALLOC_FAIL(path, apr_psprintf(g_pool, "%s/" DIR_ACTIVE, arg_base))) {
		die_allocerror0();
	}

	if (APR_FAIL(status, apr_dir_open(&dir, path, g_pool))) {
		die_aprerror2(status, "failed to open directory ", path);
	}

	error1(INFO, "printing status for active services");

	while (apr_dir_read(&info, APR_FINFO_DIRENT, dir) == APR_SUCCESS) {
		die_assert(info.name);

		apr_pool_clear(pool);

		/* All valid entries in DIR_ACTIVE are symbolic links */
		if (info.filetype != APR_LNK || info.name[0] == '.') {
			continue;
		}

		/* Path to the monitor status file for the service */
		if (ALLOC_FAIL(path,
				apr_psprintf(pool, "%s/" DIR_ACTIVE "/%s/" FILE_STATUS,
					arg_base, info.name))) {
			die_allocerror0();
		}

		if (APR_FAIL(status, apr_file_open(&file, path, APR_FOPEN_READ |
				APR_FOPEN_BINARY, FPROT_FILE_STATUS, pool))) {
			warn_aprerror2(status, "failed to open ", path);
			continue;
		}

		die_assert(file);

		if (APR_FAIL(status, apr_file_read_full(file, filebuf,
				MONITOR_STATUS_SIZE, NULL))) {
			warn_aprerror2(status, "failed to read status file ", path);
			apr_file_close(file);
			continue;
		}

		apr_file_close(file);

		apr_file_puts(format_status(&counter, info.name, filebuf, pool),
				g_apr_stdout);
	}

	apr_dir_close(dir);
}

static void command_list()
{
	apr_status_t status;
	apr_dir_t *dir;
	apr_finfo_t info;
	int counter = 0;
	char *path, *msg;

	die_assert(arg_base);

	/* Path to active services */
	if (ALLOC_FAIL(path,
			apr_psprintf(g_pool, "%s/" DIR_ALL, arg_base))) {
		die_allocerror0();
	}

	if (APR_FAIL(status, apr_dir_open(&dir, path, g_pool))) {
		die_aprerror2(status, "failed to open directory ", path);
	}

	error1(INFO, "printing a list of available services");

	while (apr_dir_read(&info, APR_FINFO_DIRENT, dir) == APR_SUCCESS) {
		die_assert(info.name);

		/* All valid entries in DIR_ALL are symbolic directories */
		if (info.filetype != APR_DIR || info.name[0] == '.') {
			continue;
		}

		arg_name = info.name;

		if (ALLOC_FAIL(msg,
				apr_psprintf(g_pool, LIST_MESSAGE_FORMAT, ++counter,
					info.name,
					format_flag(service_active()),
					format_exists(service_file_exists(FILE_RUN)),
					format_exists(service_file_exists(FILE_LOG)),
					format_priority()))) {
			die_allocerror0();
		}

		apr_file_puts(msg, g_apr_stdout);
	}

	apr_dir_close(dir);
}

/*
 * Commands
 */

/* Restarts all active services and monitors, if any */
static void command_killall()
{
	apr_status_t status;
	apr_dir_t *active;
	apr_finfo_t entry;
	char *path;

	error1(INFO, "restarting active services and monitors");

	/* Path to DIR_ACTIVE */
	if (ALLOC_FAIL(path, apr_psprintf(g_pool, "%s/" DIR_ACTIVE, arg_base))) {
		die_allocerror0();
	}

	if (chdir(path) == -1) {
		die_syserror2("failed to chdir to ", path);
	}

	if (APR_FAIL(status, apr_dir_open(&active, ".", g_pool))) {
		die_aprerror2(status, "failed to open ", path);
	}

	while (apr_dir_read(&entry, APR_FINFO_DIRENT, active) == APR_SUCCESS) {
		/* All valid entries in DIR_ACTIVE are symbolic links */
		if (entry.filetype != APR_LNK || entry.name[0] == '.') {
			continue;
		}

		if (APR_FAIL(status,
				apr_stat(&entry, entry.name, APR_FINFO_NORM, g_pool))) {
			die_aprerror2(status, "failed to stat ", entry.name);
		}

		/* And the links must point to a directory */
		if (entry.filetype != APR_DIR) {
			continue;
		}

		arg_name = entry.name;

		error2(INFO, "restarting ", arg_name);
		monitor_command(MONITOR_CMD_TERMINATE, 1);
	}

	apr_dir_close(active);
}

/* Performs an action to a service */
static void command_action(const apr_uint32_t selected)
{
	die_assert(arg_name);

	if (service_exists()) {
		if (selected & cmd_priority) {
			service_priority();
		} else if (selected & cmd_start) {
			if (service_active()) {
				warn_error2(arg_name, " is already active");
			} else {
				error2(INFO, "starting ", arg_name);
				service_create_up();
				service_add();
				/* Should be unnecessary */
				monitor_command(MONITOR_CMD_WAKEUP, 0);
			}
		} else if (service_active()) {
			if (selected & cmd_up) {
				error2(INFO, "setting up ", arg_name);
				service_create_up();
			} else if (selected & cmd_down) {
				error2(INFO, "setting down ", arg_name);
				service_remove_up();
			} else if (selected & cmd_restart) {
				error2(INFO, "restarting ", arg_name);
				service_create_up();
				monitor_command(MONITOR_CMD_KILL, 0);
			} else if (selected & cmd_stop) {
				error2(INFO, "stopping ", arg_name);
				service_remove_up();
				service_remove();
				monitor_command(MONITOR_CMD_TERMINATE, 1);
			} else if (selected & cmd_kill) {
				error3(INFO, "restarting ", arg_name, " and its monitor");
				service_create_up();
				monitor_command(MONITOR_CMD_TERMINATE, 1);
			} else if (selected & cmd_signal) {
				error2(INFO, "signaling ", arg_name);
				monitor_command(arg_signum, 0);
			} else if (selected & cmd_sigterm) {
				error2(INFO, "setting down and signaling ", arg_name);
				service_remove_up();
				monitor_command(arg_signum, 0);
			} else if (selected & cmd_term) {
				error2(INFO, "setting down and terminating ", arg_name);
				service_remove_up();
				monitor_command(SIGTERM, 0);
			}
		} else {
			die_error2(arg_name, " is not active");
		}
	} else {
		die_error1("unknown service");
	}
}

int main(int argc, const char * const *argv, const char * const *env)
{
	apr_uint32_t selected;

	ngim_base_app_init(PROGRAM_SRVCTL, &argc, &argv, &env);

	if (ngim_cmdline_parse(argc, argv, 1, service_params, service_args,
				&selected) < 0 ||
		validate_cmdline(selected) < 0) {
		die_error4("usage: ", argv[0], " ", CMDLINE_USAGE);
	}

	if (selected & cmd_status) {
		command_status();
	} else if (selected & cmd_list) {
		command_list();
	} else if (selected & cmd_killall) {
		command_killall();
	} else {
		command_action(selected);
	}

	error1(INFO, "done");
	return EXIT_SUCCESS;
}
