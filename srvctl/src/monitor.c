/*
 * monitor.c
 *
 * Copyright © 2005, 2006  Sami Tolvanen <sami@ngim.org>
 */

#include "common.h"
#include "srvctl.h"
#include <apr_general.h>
#include <apr_file_info.h>
#include <apr_file_io.h>
#include <apr_poll.h>
#include <apr_signal.h>
#include <apr_strings.h>
#include <apr_thread_proc.h>
#include <ngim/base.h>

/* If zero, insecure permissions for files and directories are ignored */
#define MONITOR_SET_PERMS_FOR_EXISTING 1

/* Parameters, times are in seconds */
#define PAUSE_FAILURE		5		/* Pause if command poll/read fails */
#define PAUSE_RESPAWN		1		/* Pause after starting a child */
#define PAUSE_TERMWAIT		10		/* Max. time to wait for a child to die */
#define TIMEOUT_POLL		3600	/* Timeout for polling commands */
#define TIMER_CHILD			10		/* Time between suspension checks */
#define CHILD_MAXSTARTS		2		/* Max. allowed restarts in TIMER_CHILD */
#define CHILD_SUSPENSION	3		/* Number of TIMER_CHILD's to suspend */

/* Flags */
static int flag_stop = 0;		/* Stop monitor, i.e. exit the main loop */
static int flag_intr = 0;		/* Received a signal, don't restart children */
static int flag_forward = 0;	/* Output of run is forwarded to pipe_runlog */
static int flag_ignchld = 0;	/* Do nothing on SIGCHLD */
static int flag_timer = 0;		/* Suspension timer active */

/* Files and pipes */
static apr_file_t *file_lock = NULL;
static apr_file_t *pipe_control = NULL;
static apr_file_t *pipe_stdin = NULL;	/* To run's stdin */
static apr_file_t *pipe_runlog[2] = { NULL, NULL }; /* From run to logger */
static apr_pollset_t *pset_control = NULL;
static apr_pool_t *pool_runlog = NULL;

/* Children */
typedef struct {
	const char *progname;	/* Name of the program to execute */
	apr_proc_t proc;		/* Process information */
	ngim_tain_t changed;	/* Last started or stopped */
	apr_uint32_t starts;	/* Start attempts within TIMER_CHILD */
	int suspended;			/* Process suspended? */
	int suspended_periods;	/* Number of TIMER_CHILD's suspended */
} child_proc;

static child_proc run;
static child_proc log;

/* Error function called on platforms using fork in case exec fails (in the
 * child process). */
static void aprprocerror(apr_pool_t *pool __unused, apr_status_t err,
		const char *desc)
{
	/* Doesn't matter if desc is NULL */
	warn_aprerror1(err, desc);
}

/* Opens a named pipe with given name for reading and writing. If the
 * pipe doesn't exist, creates one with given permissions. apr_file_t
 * is allocated from the global g_pool. Dies in case of failure. */
static void create_namedpipe(apr_file_t **file, const char *name,
		apr_fileperms_t perms, apr_pool_t *pool)
{
	apr_status_t status;
	apr_finfo_t info;

	die_assert(file);
	die_assert(name);
	die_assert(pool);

	/* See if the pipe exists */
	if (APR_FAIL(status, apr_stat(&info, name, APR_FINFO_NORM, pool))) {
		if (APR_STATUS_IS_ENOENT(status)) {
			/* Create a new pipe */
			if (APR_FAIL(status,
					apr_file_namedpipe_create(name, perms, pool))) {
				die_aprerror2(status, "failed to create ", name);
			}
		} else {
			die_aprerror2(status, "stat failed for ", name);
		}
	} else if (info.filetype != APR_PIPE) {
		die_error3("failed to open ", name, ": Not a pipe");
	}
#if MONITOR_SET_PERMS_FOR_EXISTING
	else if (APR_FAIL(status, apr_file_perms_set(name, perms))) {
		die_aprerror2(status, "failed to set permissions for ", name);
	}
#endif

	/* Open the pipe */
	if (APR_FAIL(status, apr_file_open(file, name,
					APR_FOPEN_READ | APR_FOPEN_WRITE | APR_FOPEN_APPEND |
					APR_FOPEN_BINARY, perms, g_pool))) {
		die_aprerror2(status, "failed to open ", name);
	}

	die_assert(*file);
}

/* Creates a file named FILE_LOCK, tries to gain an exclusive lock on it.
 * Dies in case of failure. */
static void create_lockfile()
{
	apr_status_t status;

	if (APR_FAIL(status,
			apr_file_open(&file_lock, FILE_LOCK, APR_FOPEN_CREATE |
				APR_FOPEN_APPEND | APR_FOPEN_READ | APR_FOPEN_WRITE,
				FPROT_FILE_LOCK, g_pool))) {
		die_aprerror1(status, "failed to open " FILE_LOCK);
	} else if (APR_FAIL(status, apr_file_lock(file_lock,
					APR_FLOCK_EXCLUSIVE | APR_FLOCK_NONBLOCK))) {
		if (APR_STATUS_IS_EAGAIN(status)) {
			die_error1("another monitor already running, exiting");
		} else {
			die_aprerror1(status, "failed to lock " FILE_LOCK);
		}
	}
}

/* Makes sure DIR_MONITOR exists with proper permissions, sets up a control
 * pipe, pollset for it, a pipe for run's stdin, and a pipe subpool. */
static void setup_monitor(apr_pool_t *pool)
{
	apr_status_t status;
	die_assert(pool);

	/* Set up a subdirectory */
	if (ngim_create_directory(DIR_MONITOR, FPROT_DIR_MONITOR,
			MONITOR_SET_PERMS_FOR_EXISTING, pool) < 0) {
		die_error1("failed to set up directory " DIR_MONITOR);
	}

	/* Make sure there is only one monitor running for each service */
	create_lockfile();

	/* Create the control pipe */
	create_namedpipe(&pipe_control, PIPE_CONTROL, FPROT_PIPE_CONTROL, pool);

	/* Create a pollset for polling the control pipe */
	if (ngim_create_pollset_file_in(&pset_control, pipe_control, g_pool) < 0) {
		die_error1("failed to set up polling for " PIPE_CONTROL);
	}

	/* Create a pipe to run's stdin */
	create_namedpipe(&pipe_stdin, PIPE_STDIN, FPROT_PIPE_STDIN, pool);

	/* Create a subpool for storing pipe_runlog, which may need to be stored
	 * for a while, yet is possibly repeatedly recreated */
	if (APR_FAIL(status, apr_pool_create(&pool_runlog, g_pool))) {
		die_aprerror1(status, "failed to create a memory pool");
	}
}

/* Closes both ends of pipe_runlog, clears pool_runlog. The pipe should only
 * be closed when terminating both children; if either one is still running,
 * the pipe is still needed for forwarding. */
static inline void close_pipe()
{
	die_assert(pool_runlog);

	if (pipe_runlog[0]) {
		apr_file_close(pipe_runlog[0]);
		pipe_runlog[0] = NULL;
	}

	if (pipe_runlog[1]) {
		apr_file_close(pipe_runlog[1]);
		pipe_runlog[1] = NULL;
	}

	/* Clear the pool when the pipes are gone so repeated calls to
	 * create_pipe won't starve all the memory */
	apr_pool_clear(pool_runlog);
}

/* Creates pipe_runlog for interprocess communication between run and
 * log, if it doesn't exist already. Returns non-zero if the pipe was
 * already open or was successfully created. */
static inline int create_pipe()
{
	apr_status_t status;
	die_assert(pool_runlog);

	if (!pipe_runlog[0] || !pipe_runlog[1]) {
		/* If one end is closed, they both should be */
		close_pipe();

		/* Create a new pipe, which is not inherited by children */
		if (APR_FAIL(status,
				apr_file_pipe_create(&pipe_runlog[0], &pipe_runlog[1],
					pool_runlog)) ||
			APR_FAIL(status, apr_file_inherit_unset(pipe_runlog[0])) ||
			APR_FAIL(status, apr_file_inherit_unset(pipe_runlog[1]))) {
			warn_aprerror1(status, "failed to create a pipe");
			return 0;
		}

		die_assert(pipe_runlog[0]);
		die_assert(pipe_runlog[1]);
	}

	/* The pipe is ready */
	return 1;
}

/* Returns non-zero if FILE_UP exists. */
static inline int check_fileup(apr_pool_t *pool)
{
	apr_status_t status;
	apr_finfo_t info;

	die_assert(pool);

	if (APR_FAIL(status, apr_stat(&info, FILE_UP, APR_FINFO_NORM, pool))) {
		/* Something failed or the file doesn't exist */
		if (!APR_STATUS_IS_ENOENT(status)) {
			warn_aprerror1(status, "stat failed for " FILE_UP);
		}
		/* Either way, don't start the service */
		return 0;
	}

	/* It exists */
	return 1;
}

/* Writes current status to FILE_STATUS. Should be called only when something
 * has changed. */
static void write_status(apr_pool_t *pool)
{
	apr_status_t status;
	apr_file_t *file;
	char *tmpname;
	ngim_tain_t updated;
	unsigned char buffer[MONITOR_STATUS_SIZE];

	die_assert(pool);
	memset(buffer, 0, sizeof(buffer));

	/* Time stamps */
	ngim_tain_now(&updated);
	ngim_tain_pack(&buffer[MONITOR_STATUS_UPDATED], &updated);
	ngim_tain_pack(&buffer[MONITOR_STATUS_CHG_RUN], &run.changed);
	ngim_tain_pack(&buffer[MONITOR_STATUS_CHG_LOG], &log.changed);
	/* PIDs */
	*(apr_uint32_t*)&buffer[MONITOR_STATUS_PID_RUN] =
		(apr_uint32_t)run.proc.pid;
	*(apr_uint32_t*)&buffer[MONITOR_STATUS_PID_LOG] =
		(apr_uint32_t)log.proc.pid;
	/* Flags */
	if (flag_forward) {
		buffer[MONITOR_STATUS_FORWARD] = 1;
	}

	/* Write to file */
	if (ALLOC_FAIL(tmpname, apr_psprintf(pool, FILE_STATUS ".XXXXXX"))) {
		warn_allocerror1(" while updating " FILE_STATUS);
		return;
	}

	if (APR_FAIL(status, apr_file_mktemp(&file, tmpname, APR_FOPEN_CREATE |
			APR_EXCL | APR_FOPEN_WRITE | APR_FOPEN_BINARY, pool))) {
		warn_aprerror1(status, "failed to update " FILE_STATUS);
		return;
	}

	if (APR_FAIL(status, apr_file_perms_set(tmpname, FPROT_FILE_STATUS))) {
		apr_file_close(file);
		warn_aprerror2(status, "failed to set permissions for ", tmpname);
		return;
	}

	if (APR_FAIL(status,
			apr_file_write_full(file, buffer, sizeof(buffer), NULL))) {
		apr_file_close(file);
		warn_aprerror2(status, "failed to write to ", tmpname);
		return;
	}

	apr_file_close(file);

	if (APR_FAIL(status, apr_file_rename(tmpname, FILE_STATUS, pool))) {
		warn_aprerror3(status, "failed to rename ", tmpname,
			" -> " FILE_STATUS);
		if (APR_FAIL(status, apr_file_remove(tmpname, pool))) {
			warn_aprerror2(status, "failed to remove ", tmpname);
		}
	}
}

/* Sees if either of the children has died, reports accordingly. */
static void check_children(apr_pool_t *pool)
{
	apr_proc_t child;
	apr_exit_why_e exitwhy;
	int exitcode;
	const char *name;
	char *str;

	die_assert(pool);

	while (APR_STATUS_IS_CHILD_DONE(apr_proc_wait_all_procs(&child,
				&exitcode, &exitwhy, APR_NOWAIT, pool))) {
		if (child.pid == run.proc.pid) {
			/* run has died */
			ngim_tain_now(&run.changed);
			memset(&run.proc, 0, sizeof(run.proc));
			flag_forward = 0; /* Forwarding no more */
			name = run.progname;
		} else if (child.pid == log.proc.pid) {
			/* log has died */
			ngim_tain_now(&log.changed);
			memset(&log.proc, 0, sizeof(log.proc));
			name = log.progname;
		} else {
			/* Weird stuff */
			warn_error1("unknown child process exited");
			continue;
		}

		/* A process has died, report */
		write_status(pool);

		if (ALLOC_FAIL(str,
				apr_psprintf(pool, "%s [pid %i] exited %s with code %i",
					name, child.pid, ngim_strexitwhy(exitwhy),
					exitcode))) {
			warn_allocerror0();
			warn_error2(name, " exited");
		} else {
			warn_error1(str);
		}
	}
}

/* Creates process attributes and sets up file descriptors for run. */
static apr_procattr_t * create_procattr_run(apr_procattr_t **attr,
		apr_pool_t *pool)
{
	apr_status_t status;

	die_assert(attr);
	die_assert(pool);
	die_assert(pipe_stdin);

	flag_forward = 0;

	/* Create attributes, set pipe_stdin --> stdin */
	if (APR_FAIL(status, apr_procattr_create(attr, pool)) ||
		APR_FAIL(status, apr_procattr_cmdtype_set(*attr, APR_PROGRAM_ENV)) ||
		APR_FAIL(status, apr_procattr_error_check_set(*attr, 1)) ||
		APR_FAIL(status, apr_procattr_child_errfn_set(*attr, aprprocerror)) ||
		APR_FAIL(status, apr_procattr_child_in_set(*attr, pipe_stdin, NULL))) {
		warn_aprerror1(status, "failed to create process attributes");
		return NULL;
	}

	/* Forward stdout/err to pipe_runlog only if log is running */
	if (log.proc.pid) {
		/* If log is running, we must have a pipe already */
		die_assert(pipe_runlog[1]);
		if (APR_FAIL(status,
				apr_procattr_child_out_set(*attr, pipe_runlog[1], NULL)) ||
			APR_FAIL(status,
				apr_procattr_child_err_set(*attr, pipe_runlog[1], NULL))) {
			warn_aprerror1(status, "failed set up forwarding");
			return NULL;
		}
		flag_forward = 1;
	}

	die_assert(*attr);
	return *attr;
}

/* Creates process attributes and sets up file descriptors for log. */
static apr_procattr_t * create_procattr_log(apr_procattr_t **attr,
		apr_pool_t *pool)
{
	apr_status_t status;

	die_assert(attr);
	die_assert(pool);

	/* Make sure pipe_runlog exists */
	if (unlikely(!create_pipe())) {
		return NULL;
	}

	/* Create process attributes */
	if (APR_FAIL(status, apr_procattr_create(attr, pool)) ||
		APR_FAIL(status, apr_procattr_cmdtype_set(*attr, APR_PROGRAM_ENV)) ||
		APR_FAIL(status, apr_procattr_error_check_set(*attr, 1)) ||
		APR_FAIL(status, apr_procattr_child_errfn_set(*attr, aprprocerror)) ||
		/* pipe_runlog --> stdin */
		APR_FAIL(status,
			apr_procattr_child_in_set(*attr, pipe_runlog[0], NULL))) {
		warn_aprerror1(status, "failed to create process attributes");
		return NULL;
	}

	die_assert(*attr);
	return *attr;
}

/* Starts a program from the working directory with given process
 * attributes. */
static void start_child(child_proc *child, apr_procattr_t *attr,
		apr_pool_t *pool)
{
	apr_status_t status;
	apr_finfo_t info;
	char *str;
	const char *args[] = { NULL, NULL };

	die_assert(child);
	die_assert(attr);
	die_assert(pool);
	warn_assert(child->proc.pid == 0); /* Already running? */
	warn_assert(TIMER_CHILD > 0); /* Sanity check */

	/* Start the suspension timer if it's inactive */
	if (!flag_timer) {
		alarm(TIMER_CHILD);
		flag_timer = 1;
	}
	++child->starts;

	/* See if the file exists */
	if (APR_FAIL(status,
			apr_stat(&info, child->progname, APR_FINFO_NORM, pool))) {
		if (!APR_STATUS_IS_ENOENT(status)) {
			warn_aprerror2(status, "stat failed for ", child->progname);
		}
		/* The file doesn't exist, just ignore it */
		return;
	} else if (info.filetype != APR_REG) {
		warn_error3("failed to start ", child->progname,
				": Invalid file type");
		return;
	}

	/* Don't start the child if we have been instructed to stop */
	if (flag_intr) {
		return;
	}

	/* Command line arguments */
	args[0] = child->progname;

	/* Start a child process */
	if (APR_FAIL(status, apr_proc_create(&child->proc, child->progname,
				args, NULL, attr, pool))) {
		child->proc.pid = 0;
		warn_aprerror2(status, "failed to start ", child->progname);
	} else {
		/* Start time */
		ngim_tain_now(&child->changed);

		/* Process started, report */
		write_status(pool);

		if (ALLOC_FAIL(str,
				apr_psprintf(pool, "started %s [pid %i]",
					child->progname, child->proc.pid))) {
			warn_allocerror0();
			error2(INFO, "started ", child->progname);
		} else {
			error1(INFO, str);
		}
	}
}

/* Starts children if the service is requested to be up. */
static void start_children(apr_pool_t *pool)
{
	apr_procattr_t *attr;
	die_assert(pool);

	if (flag_intr || !check_fileup(pool)) {
		return;
	}

	/* If starting a process fails before apr_proc_create returns with a
	 * success, we won't attempt to restart it. Instead, the user should
	 * manually tell us to restart it after fixing the problem.
	 *
	 * However, if we actually manage to start a new process, which dies
	 * on us, we will try to restart it upon receiving SIGCHLD.  Respawn
	 * rate is limited by PAUSE_RESPAWN below.
	 *
	 * If a process respawns over CHILD_MAXSTARTS times in TIMER_CHILD
	 * seconds after it was first started, it will be suspended. This is
	 * handled by a suspension timer started in start_child. */

	/* Don't start log if run was already started without forwarding its
	 * output to pipe_runlog */
	if (!log.suspended && !log.proc.pid && (!run.proc.pid || flag_forward)) {
		if (ALLOC_FAIL(attr, create_procattr_log(&attr, pool))) {
			warn_error2("failed to start ", log.progname);
		} else {
			start_child(&log, attr, pool);
		}
	}

	/* Always start run if not already running */
	if (!run.suspended && !run.proc.pid) {
		if (ALLOC_FAIL(attr, create_procattr_run(&attr, pool))) {
			warn_error2("failed to start ", run.progname);
		} else {
			start_child(&run, attr, pool);
		}
	}

	/* Limit respawn rate. This is only an issue if exec repeatedly fails
	 * after a child process has been created */
	apr_sleep(apr_time_from_sec(PAUSE_RESPAWN));
}

/* Sends a signal to a child process if its pid is non-zero, prints out a log
 * message. */
static void signal_child(child_proc *child, int sig, apr_pool_t *pool)
{
	char *str;

	die_assert(child);
	die_assert(pool);
	warn_assert(sig > 0 && sig < NSIG);

	if (!child->proc.pid) {
		/* Process not running */
		return;
	}

	if (ALLOC_FAIL(str,
			apr_psprintf(pool, "sending signal %i to %s [pid %i]",
				sig, child->progname, child->proc.pid))) {
		warn_allocerror0();
		error2(INFO, "sending a signal to ", child->progname);
	} else {
		error1(INFO, str);
	}

	apr_proc_kill(&child->proc, sig);
}

/* Terminates a child and waits until it exits. */
static void terminate_child(child_proc *child, apr_pool_t *pool)
{
	/* Services are expected to handle SIGTERM and exit gracefully.
	 * Otherwise, we follow the postgresql signaling scheme:
	 * http://www.postgresql.org/docs/8.1/interactive/app-postmaster.html */

	int sigs[] = { SIGTERM, SIGTERM, SIGINT, SIGQUIT, SIGKILL };
	int i = 0;

	die_assert(child);
	die_assert(pool);

	/* Do not try to wake up the process on SIGCHLD */
	flag_ignchld = 1;

	while (child->proc.pid) {
		/* Send a signal */
		signal_child(child, sigs[i], pool);

		/* Wait for the process to exit */
		apr_signal_unblock(SIGCHLD);
		apr_sleep(apr_time_from_sec(PAUSE_TERMWAIT));
		apr_signal_block(SIGCHLD);

		/* See if it really did */
		check_children(pool);

		/* Just give up if SIGKILL had no effect */
		if (sigs[i] == SIGKILL) {
			break;
		} else {
			/* Try again */
			++i;
		}
	}

	/* Reset suspension */
	child->starts = 0;
	child->suspended = 0;
	child->suspended_periods = 0;

	/* Continue with normal behavior */
	flag_ignchld = 0;
}

/* Performs actions based on the received control command. */
static void parse_command(unsigned char cmd, apr_pool_t *pool)
{
	die_assert(pool);

	/* Sanity checks */
	warn_assert(MONITOR_CMD_TERMINATE >= NSIG);
	warn_assert(MONITOR_CMD_KILL >= NSIG);
	warn_assert(MONITOR_CMD_WAKEUP >= NSIG);

	if (cmd == MONITOR_CMD_TERMINATE) {
		/* Terminate service */
		flag_stop = 1;

		/* Close run's stdin, just in case */
		if (pipe_stdin) {
			apr_file_close(pipe_stdin);
			pipe_stdin = NULL;
		}
	}

	if (cmd == MONITOR_CMD_KILL || flag_stop) {
		/* Close pipe_runlog, so the children receive EOF in case they are
		 * waiting for I/O on the pipe (i.e. after one of them exits) */
		close_pipe();
		terminate_child(&run, pool);
		terminate_child(&log, pool);
	} else if (cmd == MONITOR_CMD_WAKEUP) {
		/* Do nothing */
	} else if (cmd > 0 && cmd < NSIG) {
		/* Try to send as a signal */
		signal_child(&run, cmd, pool);
	} else {
		warn_error1("unknown command");
	}
}

/* Waits for input to the control pipe, reads one byte and tries to process it
 * as a command. */
static void wait_for_command(apr_pool_t *pool)
{
	apr_status_t status;
	unsigned char cmd;
	int signaled = 0;

	die_assert(pool);
	die_assert(pset_control);
	die_assert(pipe_control);

	apr_signal_unblock(SIGCHLD);

	/* Wait for a command from pipe_control */
	if (APR_FAIL(status, apr_pollset_poll(pset_control,
			apr_time_from_sec(TIMEOUT_POLL), &signaled, NULL))) {
		apr_signal_block(SIGCHLD);

		if (APR_STATUS_IS_EINTR(status) || APR_STATUS_IS_TIMEUP(status)) {
			/* Interrupt or timeout */
			return;
		} else {
			/* Sleep for a while and try again */
			warn_aprerror1(status, "failed to poll for " PIPE_CONTROL
						", sleeping");
			apr_sleep(apr_time_from_sec(PAUSE_FAILURE));
		}
	} else {
		apr_signal_block(SIGCHLD);
		warn_assert(signaled == 1);

		if (APR_FAIL(status,
				apr_file_read_full(pipe_control, &cmd, 1, NULL))) {
			warn_aprerror1(status, "failed to read from " PIPE_CONTROL);
			apr_sleep(apr_time_from_sec(PAUSE_FAILURE));
		} else {
			parse_command(cmd, pool);
		}
	}
}

/* Initializes a child_proc structure */
static inline void child_init(child_proc *child, const char *progname)
{
	die_assert(child);
	die_assert(progname);

	memset(child, 0, sizeof(child_proc));
	child->progname = progname;
}

/* Where the magic happens */
static int monitor(const char *root)
{
	apr_status_t status;
	apr_pool_t *pool;

	die_assert(root);

	if (chdir(root) == -1) {
		die_syserror3("chdir to ", root, " failed");
	}

	/* Drop unneeded privileges */
	if (ngim_priv_drop(NGIM_PRIV_SRVCTL, NULL, NULL) < 0) {
		warn_error1("failed to drop privileges");
	}

	if (APR_FAIL(status, apr_pool_create(&pool, g_pool))) {
		die_aprerror1(status, "failed to create a memory pool");
	}

	/* Setup */
	setup_monitor(pool);
	child_init(&run, FILE_RUN);
	child_init(&log, FILE_LOG);

	/* Initial status */
	write_status(pool);

	apr_pool_clear(pool);

	/* After this point, the program should not die in vain */
	do {
		/* Check for dead children, and start service if requested */
		check_children(pool);
		start_children(pool);

		/* Clear the pool before sleeping */
		apr_pool_clear(pool);

		/* Wait for some action */
		wait_for_command(pool);
	} while (!flag_stop);

	error1(INFO, "exiting");
	return EXIT_SUCCESS;
}

/* Writes a command to pipe_control */
static inline void write_command(unsigned char cmd)
{
	apr_status_t status;
	die_assert(pipe_control);

	if (APR_FAIL(status, apr_file_putc(cmd, pipe_control))) {
		die_aprerror1(status, "failed to write a command to " PIPE_CONTROL);
	}
}

/* Checks if a child process should be suspended, i.e. if it has been restarted
 * more than CHILD_MAXSTARTS times during the past TIMER_CHILD period. If timer
 * is not needed by this child anymore, returns zero. Otherwise, the suspension
 * timer should be restarted by the caller. */
static int check_suspension(child_proc *child)
{
	if (child->suspended) {
		/* Should the suspension be lifted */
		if (++child->suspended_periods >= CHILD_SUSPENSION) {
			child->suspended = 0;
			child->suspended_periods = 0;
			write_command(MONITOR_CMD_WAKEUP);
			/* Timer should be restarted later if needed */
			return 0;
		}
		/* Suspension in progress */
		return 1;
	} else {
		if (child->starts > 0) {
			/* Should the process be suspended */
			if (child->starts > CHILD_MAXSTARTS) {
				child->suspended = 1;
				error3(WARNING, "suspended ", child->progname,
						", respawning too fast");
			}
			child->starts = 0;
			/* There were restarts */
			return 1;
		}
		/* No restarts lately */
		return 0;
	}
}

/* Handler for SIGALRM */
static inline void handle_alarm()
{
	/* Check for suspension, restart the timer if necessary */
	int restart = check_suspension(&log);

	if (check_suspension(&run) || restart) {
		alarm(TIMER_CHILD);
	} else {
		flag_timer = 0;
	}
}

/* Signal handler */
static void handler(int sig)
{
	switch (sig) {
	case SIGALRM:
		handle_alarm();
		break;
	case SIGCHLD:
		if (flag_ignchld) {
			/* Don't wake up the process */
			return;
		}
		/* No break */
	case SIGHUP:
		/* Wake up the process */
		write_command(MONITOR_CMD_WAKEUP);
		break;
	case SIGINT:
	case SIGTERM:
	case SIGQUIT:
		/* Try to terminate gracefully, although a command would have been
		 * preferred instead of a signal */
		warn_error1("received a signal");
		/* Set the flag to prevent us from restarting children that have
		 * possibly also been signaled to terminate, usually in case of a
		 * system wide shutdown */
		flag_intr = 1;
		write_command(MONITOR_CMD_TERMINATE);
		break;
	default:
		break;
	}
}

/* Find a display name for the service */
const char * service_displayname(const char *root)
{
	apr_status_t status;
	apr_finfo_t info;
	char *basename;

	if (APR_FAIL(status,
			apr_stat(&info, root, APR_FINFO_NORM | APR_FINFO_LINK, g_pool))) {
		die_aprerror2(status, "failed to stat ", root);
	}

	if (info.filetype != APR_LNK ||
		ngim_resolve_symlink_basename(root, &basename, g_pool) < 0) {
		return root;
	}

	return basename;
}

int main(int argc, const char * const * argv, const char * const *env)
{
	const char *progname, *dispname;

	ngim_base_app_init(PROGRAM_MONITOR, &argc, &argv, &env);

	if (argc < 2 || argc > 3) {
		die_error3("usage: ", argv[0], " directory [ name ]");
	}

	/* Signals */
	apr_signal_block(SIGCHLD);
	apr_signal(SIGALRM,	handler);
	apr_signal(SIGCHLD, handler);
	apr_signal(SIGHUP,	handler);
	apr_signal(SIGINT,  handler);
	apr_signal(SIGTERM, handler);
	apr_signal(SIGQUIT, handler);

	if (argc > 2) {
		dispname = argv[2];
	} else {
		dispname = service_displayname(argv[1]);
	}

	/* Program name */
	if (ALLOC_FAIL(progname,
			apr_psprintf(g_pool, PROGRAM_MONITOR " [pid %i] %s",
				getpid(), dispname))) {
		warn_allocerror0();
	} else {
		ngim_setprogname(progname);
	}

	return monitor(argv[1]);
}
