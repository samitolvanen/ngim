/*
 * create.c
 *
 * Copyright © 2005, 2006  Sami Tolvanen <sami@ngim.org>
 */

#include "common.h"
#include "base.h"

int ngim_create_directory(const char *name, apr_fileperms_t perms,
		int setperms, apr_pool_t *pool)
{
	apr_status_t status;
	apr_finfo_t info;

	die_assert(name);
	die_assert(pool);

	if (APR_FAIL(status, apr_stat(&info, name, APR_FINFO_NORM, pool))) {
		if (APR_STATUS_IS_ENOENT(status)) {
			if (APR_FAIL(status, apr_dir_make(name, perms, pool))) {
				warn_aprerror2(status, "failed to create ", name);
				return -1;
			}
		} else {
			warn_aprerror2(status, "stat failed for ", name);
			return -1;
		}
	} else if (info.filetype != APR_DIR) {
		warn_error3("invalid type for ", name, ": Not a directory");
		return -1;
	} else if (setperms) {
		if (APR_FAIL(status, apr_file_perms_set(name, perms))) {
			warn_aprerror2(status, "failed to set permissions for ", name);
			return -1;
		}
	}

	return 0;
}

/*
 * For apr_pool_cleanup_register.
 */
static apr_status_t pollset_destroy(void *data)
{
	die_assert(data);
	return apr_pollset_destroy((apr_pollset_t*)data);
}

int ngim_create_pollset_file_in(apr_pollset_t **pset, apr_file_t *file,
		apr_pool_t *pool)
{
	apr_status_t status;

	die_assert(pset);
	die_assert(file);
	die_assert(pool);
	
	if (APR_FAIL(status, apr_pollset_create(pset, 1, pool, 0))) {
		warn_aprerror1(status, "failed to create pollset");
		return -1;
	} else {
		/* Poll the file for input */
		apr_pollfd_t fd;
		fd.desc_type = APR_POLL_FILE;
		fd.reqevents = APR_POLLIN;
		fd.desc.f = file;

		die_assert(*pset);
	
		if (APR_FAIL(status, apr_pollset_add(*pset, &fd))) {
			warn_aprerror1(status, "failed to add a file to pollset");
			apr_pollset_destroy(*pset);
			*pset = NULL;
			return -1;
		}
	
		/* Possible file descriptors opened by the pollset implementation
		 * should not be inherited by children */
		apr_pool_cleanup_register(pool, *pset, apr_pool_cleanup_null,
			pollset_destroy);
	}

	return 0;
}
