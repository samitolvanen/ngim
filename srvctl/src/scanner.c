/*
 * scanner.c
 *
 * Copyright © 2005, 2006  Sami Tolvanen <sami@ngim.org>
 */

#include "common.h"
#include "srvctl.h"
#include <apr_general.h>
#include <apr_file_info.h>
#include <apr_hash.h>
#include <apr_portable.h>
#include <apr_signal.h>
#include <apr_strings.h>
#include <apr_thread_proc.h>
#include <ngim/base.h>

/* Parameters */
#define MAX_SERVICES		128	/* Maximum number of monitors started */
#define PAUSE_SCANNER		5	/* Pause between scans, in seconds */
#define PAUSE_MONITOR		1	/* Pause after monitor start */
#define VALUE_NAME_LEN		80	/* Maximum name length for status messages */

/* Flags */
static int flag_stop = 0;

/* Services */
static apr_hash_t *services = NULL;

/* Hash table key */
typedef struct service_key {
	apr_ino_t inode;	/* Directory inode */
	apr_dev_t device;	/* Directory device */
} service_key;

/* Hash table value */
typedef struct service_value {
	int pid;			/* Monitor running if non-zero */
	int active;			/* Directory still exists if non-zero */
	char name[VALUE_NAME_LEN];	/* For status messages only */
} service_value;


/* For apr_pool_cleanup_register. */
static apr_status_t aprfree(void *data)
{
	die_assert(data);
	free(data);
	return APR_SUCCESS;
}

/* Error function called on platforms using fork in case exec fails (in the
 * child process). */
static void aprprocerror(apr_pool_t *pool __unused, apr_status_t err,
		const char *desc)
{
	/* Doesn't matter if desc is NULL */
	warn_aprerror1(err, desc);
}

static int compare_finfo_name(const void *a, const void *b)
{
	die_assert(a);
	die_assert(b);

	return strcmp(((const apr_finfo_t *)b)->name,
				  ((const apr_finfo_t *)a)->name);
}

/* Creates a new service directory entry to the hash table. Reports an error
 * and returns NULL if fails. */
static inline service_value * create_entry(apr_finfo_t *entry,
		const char *name, apr_pool_t *pool)
{
	char *basename;
	service_key *key;
	service_value *value;

	die_assert(entry);
	die_assert(name);
	die_assert(pool);
	die_assert(services);

	/* Create a new entry, if there aren't too many already */
	if (apr_hash_count(services) >= MAX_SERVICES) {
		warn_error2("too many services, skipping ", name);
		return NULL;
	}
	
	/* Resolve basename for the symbolic link for display purposes */
	if (ngim_resolve_symlink_basename(name, &basename, pool) < 0) {
		return NULL;
	}
	
	/* Use malloc, and register the memory for freeing with g_pool */
	if (ALLOC_FAIL(key,   malloc(sizeof(*key))) ||
		ALLOC_FAIL(value, malloc(sizeof(*value)))) {
		if (key) {
			free(key);
		}
		warn_allocerror2(", skipping ", name);
		return NULL;
	} else {
		apr_pool_cleanup_register(g_pool, key, aprfree,
				apr_pool_cleanup_null);
		apr_pool_cleanup_register(g_pool, value, aprfree,
				apr_pool_cleanup_null);
	}

	/* Remember service name for logging */
	die_assert(VALUE_NAME_LEN > 4);
	value->name[VALUE_NAME_LEN - 1] = 0xFF; /* Test for overlong name */

	/* NUL-terminates in all cases */
	apr_snprintf(value->name, VALUE_NAME_LEN, "%s", basename);

	/* If the last character was overwritten, indicate a truncated name */
	if (value->name[VALUE_NAME_LEN - 1] == '\0') {
		value->name[VALUE_NAME_LEN - 2] =
			value->name[VALUE_NAME_LEN - 3] =
			value->name[VALUE_NAME_LEN - 4] = '.';
	}

	/* Data */
	key->inode = entry->inode;
	key->device = entry->device;

	/* These are filled out in start */
	value->pid = 0;
	value->active = 0;

	/* Add to table */
	apr_hash_set(services, key, sizeof(*key), value);
	return value;
}

/* Tries to start a monitor for the given service directory. */
static void start(apr_finfo_t *entry, apr_pool_t *pool)
{
	apr_status_t status;
	service_key current;
	service_value *value;
	apr_finfo_t dirent;
	apr_proc_t child;
	apr_procattr_t *attr;
	char *str;
	const char *args[] = { NULL, NULL, NULL, NULL };

	die_assert(entry);
	die_assert(entry->name);
	die_assert(pool);
	die_assert(services);

	/* Ignore entries that are not symbolic links or are hidden */
	if (entry->filetype != APR_LNK || entry->name[0] == '.') {
		warn_error2("skipping ", entry->name);
		return;
	}

	if (APR_FAIL(status,
			apr_stat(&dirent, entry->name, APR_FINFO_NORM, pool))) {
		warn_aprerror2(status, "stat failed, skipping ", entry->name);
		return;
	}

	/* Make sure we have a subdirectory */
	if (dirent.filetype != APR_DIR) {
		return;
	}

	/* Find the service */
	current.inode  = dirent.inode;
	current.device = dirent.device;

	value = apr_hash_get(services, &current, sizeof(current));

	if (value) {
		/* The directory still exists */
		value->active = 1;

		if (value->pid) {
			/* Already running */
			return;
		}
	} else {
		/* Create a new entry */
		value = create_entry(&dirent, entry->name, pool);

		if (value) {
			/* The directory exists */
			value->active = 1;
		} else {
			/* Something went wrong */
			return;
		}
	}

	/* Create process attributes */
	if (APR_FAIL(status, apr_procattr_create(&attr, pool)) ||
		APR_FAIL(status, apr_procattr_cmdtype_set(attr, APR_PROGRAM_PATH)) ||
		APR_FAIL(status, apr_procattr_error_check_set(attr, 1)) ||
		APR_FAIL(status, apr_procattr_child_errfn_set(attr, aprprocerror))) {
		warn_aprerror2(status, "skipping ", value->name);
		return;
	}

	die_assert(attr);

	/* Pass the link name as a command line argument to the monitor */
	args[0] = PROGRAM_MONITOR;
	args[1] = entry->name;
	args[2] = value->name; /* Display name */

	/* Start a monitor for the service */
	if (APR_FAIL(status, apr_proc_create(&child, PROGRAM_MONITOR, args, NULL,
			attr, pool))) {
		warn_aprerror2(status, "failed to start a monitor for ", value->name);
	} else {
		/* Process started */
		value->pid = child.pid;

		if (ALLOC_FAIL(str,
				apr_psprintf(pool, "started a monitor [pid %i] for %s",
					value->pid, value->name))) {
			warn_allocerror0();
			error2(INFO, "started a monitor for ", value->name);
		} else {
			error1(INFO, str);
		}

		apr_sleep(apr_time_from_sec(PAUSE_MONITOR));
	}
}

/* Clears dead monitors whose service directory is gone from the table. */
static inline void clear_services(apr_pool_t *pool)
{
	apr_hash_index_t *hi;
	void *key;
	void *tmp;
	service_value *value;
	apr_ssize_t size;

	die_assert(pool);
	die_assert(services);

	for (hi = apr_hash_first(pool, services); hi;
			hi = apr_hash_next(hi)) {
		apr_hash_this(hi, (const void**)&key, &size, &tmp);

		value = (service_value*)tmp;
		die_assert(key && value); /* Impossible */

		if (value->active) {
			/* Mark as nonexisting for the next scan */
			value->active = 0;
		} else if (!value->pid) {
			/* Directory was removed, monitor is dead; remove
			 * service from the table and free memory */
			apr_hash_set(services, key, size, NULL);
			apr_pool_cleanup_kill(g_pool, key, aprfree);
			free(key);
			apr_pool_cleanup_kill(g_pool, value, aprfree);
			free(value);
		}
	}
}

/* Marks dead monitors as not running in the table. */
static inline void monitor_done(apr_proc_t *child, int exitcode,
		apr_exit_why_e exitwhy, apr_pool_t *pool)
{
	apr_hash_index_t *hi;
	service_value *value;
	void *tmp;
	char *str;

	die_assert(child);
	die_assert(pool);
	die_assert(services);

	/* Find service from table */
	for (hi = apr_hash_first(pool, services); hi;
			hi = apr_hash_next(hi)) {
		apr_hash_this(hi, NULL, NULL, &tmp);

		value = (service_value*)tmp;
		die_assert(value); /* The table cannot contain null values */

		if (value->pid == child->pid) {
			/* Mark service as not running */
			value->pid = 0;

			/* A monitor should die only when the service is removed */
			if (ALLOC_FAIL(str, apr_psprintf(pool,
					"monitor [pid %i] for %s exited %s with code %i",
						child->pid, value->name,
						ngim_strexitwhy(exitwhy),
						exitcode))) {
				warn_allocerror0();
				warn_error4("monitor for ", value->name, " exited ",
						ngim_strexitwhy(exitwhy));
			} else {
				warn_error1(str);
			}
			return;
		}
	}

	/* Weird stuff */
	warn_error1("unknown monitor exited");
}

/* Scans root/DIR_ACTIVE for service directories every PAUSE_SCANNER seconds.
 * Attempts to start a monitor for each one, restarts if they happen to die. */
static int scan(const char *root)
{
	apr_status_t status;
	apr_pool_t *scan_pool, *start_pool;
	apr_dir_t *directory;
	apr_finfo_t info[MAX_SERVICES];
	apr_proc_t child;
	int exitcode, active;
	apr_exit_why_e exitwhy;
	char *path;

	die_assert(root);

	/* Create a memory pool */
	if (APR_FAIL(status, apr_pool_create(&scan_pool, g_pool))) {
		die_aprerror1(status, "failed to create a memory pool");
	}

	/* Path to active services */
	if (ALLOC_FAIL(path,
			apr_psprintf(scan_pool, "%s/" DIR_ACTIVE, root))) {
		die_allocerror0();
	}

	if (chdir(path) == -1) {
		die_syserror3("chdir to ", path, " failed");
	}

	/* Drop unneeded privileges */
	if (ngim_priv_drop(NGIM_PRIV_SRVCTL, NULL, NULL) < 0) {
		warn_error1("failed to drop privileges");
	}

	/* Create a hash table for services from g_pool */
	if (ALLOC_FAIL(services, apr_hash_make(g_pool))) {
		die_allocerror1(" while creating a hash table");
	}

	apr_pool_clear(scan_pool);

	/* After this point, the program should not die in vain */
	error2(INFO, "scanning ", path);

	/* Scan subdirectories */
	do {
		if (APR_FAIL(status, apr_pool_create(&start_pool, g_pool))) {
			warn_aprerror1(status, "failed to create a memory pool");
		} else {
			/* Check for dead monitors */
			while (APR_STATUS_IS_CHILD_DONE(apr_proc_wait_all_procs(&child,
						&exitcode, &exitwhy, APR_NOWAIT, start_pool))) {
				monitor_done(&child, exitcode, exitwhy, start_pool);
			}

			/* Scan for services */
			if (APR_FAIL(status, apr_dir_open(&directory, ".", scan_pool))) {
				warn_aprerror2(status, "failed to open ", path);
			} else {
				active = 0;
				memset(info, 0, sizeof(info));

				while (apr_dir_read(&info[active], APR_FINFO_DIRENT,
							directory) == APR_SUCCESS) {
					if (info[active].name[0] == '.') {
						continue;
					}
					if (active < (MAX_SERVICES - 1)) {
						++active;
					} else {
						warn_error1("reached " APR_STRINGIFY(MAX_SERVICES)
							" services, ignoring the rest");
						break;
					}
				}
				apr_dir_close(directory);

				/* Sort by entry name */
				qsort(info, active, sizeof(apr_finfo_t), compare_finfo_name);

				/* Start monitors if not already running */
				while (active-- > 0 && !flag_stop) {
					apr_pool_clear(start_pool);
					start(&info[active], start_pool);
				}

				/* Remove nonexisting services from table */
				clear_services(start_pool);
			}

			apr_pool_destroy(start_pool);
			start_pool = NULL;

			apr_pool_clear(scan_pool);
		}

		if (flag_stop) {
			break;
		}

		apr_sleep(apr_time_from_sec(PAUSE_SCANNER));
	} while (!flag_stop);

	/* Nobody lives forever */
	error1(FATAL, "exiting");
	return EXIT_SUCCESS;
}

/* Signal handler */
static void handler(int sig)
{
	switch (sig) {
	case SIGINT:
		error1(INFO, "received SIGINT");
		flag_stop = 1;
		break;
	case SIGQUIT:
		error1(INFO, "received SIGQUIT");
		flag_stop = 1;
		break;
	case SIGTERM:
		error1(INFO, "received SIGTERM");
		flag_stop = 1;
		break;
	case SIGHUP:
	default:
		break;
	}
}

int main(int argc, const char * const * argv, const char * const *env)
{
	char *progname;

	ngim_base_app_init(PROGRAM_SCANNER, &argc, &argv, &env);

	if (argc != 2) {
		die_error3("usage: ", argv[0], " directory");
	}

	/* Signals */
	apr_signal(SIGHUP,  handler);
	apr_signal(SIGINT,  handler);
	apr_signal(SIGQUIT, handler);
	apr_signal(SIGTERM, handler);

	/* Program name */
	if (ALLOC_FAIL(progname,
			apr_psprintf(g_pool, PROGRAM_SCANNER " [pid %i]",
				getpid()))) {
		warn_allocerror0();
	} else {
		ngim_setprogname(progname);
	}

	return scan(argv[1]);
}
