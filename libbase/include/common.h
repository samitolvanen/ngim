/*
 * common.h
 *
 * Copyright © 2005, 2006  Sami Tolvanen <sami@ngim.org>
 */

#ifndef COMMON_H
#define COMMON_H 1

#if HAVE_CONFIG_H
	#include <config.h>
#endif

#include <stdio.h>
#include <sys/types.h>

#if STDC_HEADERS
	#include <stdlib.h>
	#include <string.h>
#endif

#if HAVE_UNISTD_H
	#include <unistd.h>
#endif

/* Make sure we have PATH_MAX */
#if !defined(PATH_MAX)
	#define PATH_MAX 256
#endif

#if HAVE_GRP_H
	#include <grp.h>
#endif

#if HAVE_PWD_H
	#include <pwd.h>
#endif

#if HAVE_STDINT_H
	#include <stdint.h>
#endif

#if HAVE_INTTYPES_H
	#include <inttypes.h>
#endif

#if HAVE_SYS_CAPABILITY_H
	#include <sys/capability.h>
#endif

#if HAVE_SYS_PRCTL_H
	#include <sys/prctl.h>
#endif

#if !defined(UINT32_C)
	#warning UINT32_C not defined
	#define UINT32_C(x) x
#endif

#if !HAVE_ATEXIT
	#error Function missing: atexit
#endif
#if !HAVE_GETPWNAM
	#error Function missing: getpwnam
#endif
#if !HAVE_GETGRNAM
	#error Function missing: getgrnam
#endif
#if !HAVE_READLINK
	#error Function missing: readlink
#endif
#if !HAVE_STRCMP
	#error Function missing: strcmp
#endif
#if !HAVE_SETGID
	#error Function missing: setgid
#endif
#if !HAVE_SETUID
	#error Function missing: setuid
#endif
#if !HAVE_SYMLINK
	#error Function missing: symlink
#endif

#define NELEMS(array) \
	(sizeof(array) / sizeof(array[0]))

#endif /* COMMON_H */
