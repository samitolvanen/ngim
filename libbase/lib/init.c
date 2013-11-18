/*
 * init.c
 *
 * Copyright © 2005, 2006, 2007  Sami Tolvanen <sami@ngim.org>
 */

#include "common.h"
#include "base.h"
#include <apr_general.h>
#include <apr_env.h>
#include <apr_thread_proc.h>

#define MAX_POOL_FREE	32

/* Global variables */
apr_pool_t *g_pool = NULL;
apr_file_t *g_apr_stdin = NULL;
apr_file_t *g_apr_stdout = NULL;
apr_file_t *g_apr_stderr = NULL;

/*
 * Cleans up application memory pool and APR.
 */
static void cleanup(void)
{
	ngim_setprogname(NULL);

	g_apr_stderr = NULL;
	g_apr_stdout = NULL;
	g_apr_stdin  = NULL;

	if (g_pool) {
		apr_pool_destroy(g_pool);
		g_pool = NULL;
	}

	apr_terminate();
}

static void seterrorlevel()
{
	char *value;

	if (APR_FAIL_N(apr_env_get(&value, NGIM_ENV_ERROR_LEVEL, g_pool))) {
		return;
	}

	if (!strcmp(value, "verbose")) {
		ngim_seterrorlevel(VERBOSE);
	} else if (!strcmp(value, "info")) {
		ngim_seterrorlevel(INFO);
	} else if (!strcmp(value, "warning")) {
		ngim_seterrorlevel(WARNING);
	} else if (!strcmp(value, "fatal")) {
		ngim_seterrorlevel(FATAL);
	} else {
		warn_error1("invalid value for environment variable "
			NGIM_ENV_ERROR_LEVEL);
	}
}

static void init()
{
	apr_status_t status;

	/* Memory pool */
	if (APR_FAIL(status, apr_pool_create(&g_pool, NULL))) {
		die_aprerror1(status, "failed to create the global memory pool");
	}

	/* If the application prefers to keep more memory allocated, it should
	 * call apr_allocator_max_free_set on g_pool as it sees fit */
	apr_allocator_max_free_set(apr_pool_allocator_get(g_pool), MAX_POOL_FREE);

	/* Standard I/O */
	if (APR_FAIL(status, apr_file_open_stderr(&g_apr_stderr, g_pool)) ||
		APR_FAIL(status, apr_file_open_stdout(&g_apr_stdout, g_pool)) ||
		APR_FAIL(status, apr_file_open_stdin(&g_apr_stdin, g_pool))) {
		die_aprerror1(status, "failed to open standard I/O");
	}

	/* Cleanup at exit */
	if (atexit(cleanup)) {
		cleanup();
		die_syserror1("atexit failed");
	}

	/* Error level from environment */
	seterrorlevel();
}

void ngim_base_init()
{
	apr_status_t status;

	/* Initialize APR */
	if (APR_FAIL(status, apr_initialize())) {
		die_aprerror1(status, "failed to initialize APR");
	}

	/* Initialize library */
	init();
}

void ngim_base_app_init(const char *name, int *argc, const char * const **argv,
		const char * const **env)
{
	apr_status_t status;

	/* Initialize APR */
	if (APR_FAIL(status, apr_app_initialize(argc, argv, env))) {
		die_aprerror1(status, "failed to initialize APR");
	}

	/* Initialize library */
	ngim_setprogname(name);
	init();
}

int ngim_base_fork(apr_pool_t *pool)
{
	apr_proc_t child;
	apr_status_t status;

	status = apr_proc_fork(&child, pool);

	if (status == APR_INPARENT) {
		return child.pid;
	} else if (status == APR_INCHILD) {
		if (APR_FAIL(status, apr_initialize())) {
			die_aprerror1(status, "failed to initialize APR after fork");
		}
		return 0;
	}

	return -1;
}
