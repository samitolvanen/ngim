/*
 * symlink.c
 *
 * Copyright © 2006  Sami Tolvanen <sami@ngim.org>
 */

#include "common.h"
#include "base.h"
#include <apr_strings.h>

int ngim_create_symlink(const char *target, const char *path)
{
	die_assert(target);
	die_assert(path);
	
	if (symlink(target, path) == -1) {
		warn_syserror4("failed to create a symbolic link ", path,
			" -> ", target);
		return -1;
	}

	return 0;
}

int ngim_resolve_symlink_basename(const char *path, char **target,
		apr_pool_t *pool)
{
	char link[PATH_MAX];
	int i, start = 0;

	die_assert(path);
	die_assert(target);
	die_assert(pool);

	/* If we were passed a symbolic link, resolve it */
	memset(link, '\0', sizeof(link));

	if (readlink(path, link, PATH_MAX - 1) == -1) {
		warn_syserror2("failed to resolve symbolic link ", path);
		return -1;
	}

	/* Find the basename */
	for (i = 0; link[i] != '\0'; ++i) {
		if (link[i] == '/') {
			if (link[i + 1] != '\0') {
				start = i + 1;
			}
			link[i] = '\0';
		}
	}

	if (ALLOC_FAIL(*target, apr_pstrdup(pool, &link[start]))) {
		warn_allocerror0();
		return -1;
	}

	return 0;
}
