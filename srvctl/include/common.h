/*
 * common.h
 *
 * Copyright © 2005, 2006  Sami Tolvanen <sami@ngim.org>
 */

#ifndef LIB_COMMON_H
#define LIB_COMMON_H 1

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

#if HAVE_SIGNAL_H
	#include <signal.h>
#endif

/* Make sure we have NSIG */
#if !defined(NSIG)
	#if defined(_NSIG)
		#define NSIG _NSIG
	#elif defined(MAXSIG)
		#define NSIG (MAXSIG+1)
	#elif defined(SIGMAX)
		#define NSIG (SIGMAX+1)
	#elif defined(_SIG_MAX)
		#define NSIG (_SIG_MAX+1)
	#else
		#error NSIG not defined
	#endif
#endif

#if HAVE_FCNTL_H
	#include <fcntl.h>
#endif
#if HAVE_SYS_STAT_H
	#include <sys/stat.h>
#endif

/* Make sure we have O_NONBLOCK */
#if !defined(O_NONBLOCK)
	#if defined(O_NDELAY)
		#define O_NONBLOCK O_NDELAY
	#elif defined(O_FNDELAY)
		#define O_NONBLOCK O_FNDELAY
	#else
		#error Definition missing: O_NONBLOCK
	#endif
#endif

#if HAVE_SYS_RESOURCE_H
	#include <sys/resource.h>
#endif

/* Make sure we have RLIM_NLIMITS */
#if !defined(RLIM_NLIMITS)
	#if defined(RLIMIT_NLIMITS)
		#define RLIM_NLIMITS RLIMIT_NLIMITS
	#else
		#warning RLIM_NLIMITS not defined
		#define RLIM_NLIMITS 64
	#endif
#endif

/* Make sure we have PRIO_MIN and PRIO_MAX */
#if !defined(PRIO_MIN)
	#warning PRIO_MIN not defined
	#define PRIO_MIN -20
#endif
#if !defined(PRIO_MAX)
	#warning PRIO_MAX not defined
	#define PRIO_MAX 20
#endif

#if HAVE_SYS_PARAM_H
	#include <sys/param.h>
#endif

#if HAVE_SYS_JAIL_H
	#include <sys/jail.h>
#endif

#if HAVE_NETINET_IN_H
	#include <netinet/in.h>
#endif

#if HAVE_ARPA_INET_H
	#include <arpa/inet.h>
#endif

#if !HAVE_ALARM
	#error Function missing: alarm
#endif
#if !HAVE_CHDIR
	#error Function missing: chdir
#endif
#if !HAVE_EXECVP
	#error Function missing: execvp
#endif
#if !HAVE_GETPID
	#error Function missing: getpid
#endif
#if !HAVE_INET_ATON
	#error Function missing: inet_aton
#endif
#if !HAVE_MEMSET
	#error Function missing: memset
#endif
#if !HAVE_QSORT
	#error Function missing: qsort
#endif
#if !HAVE_STRCMP
	#error Function missing: strcmp
#endif
#if !HAVE_STRLEN
	#error Function missing: strlen
#endif
#if !HAVE_MALLOC
	#error Function missing: malloc
#endif

#endif /* LIB_COMMON_H */
