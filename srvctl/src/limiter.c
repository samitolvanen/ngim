/*
 * limiter.c
 *
 * Copyright © 2005, 2006  Sami Tolvanen <sami@ngim.org> 
 */

#include "common.h"
#include "srvctl.h"
#include <apr_strings.h>
#include <ngim/base.h>

/* Bitmasks for command line parameters */
enum {
	cmd_help			= 1 << 0,
	cmd_chroot			= 1 << 1,
	cmd_jail			= 1 << 2,
	cmd_jail_host		= 1 << 3,
	cmd_jail_ip			= 1 << 4,
	cmd_limit_mem		= 1 << 5,
	cmd_priority		= 1 << 6,
	cmd_priv_group		= 1 << 7,
	cmd_priv_user		= 1 << 8,
	cmd_rlim_as			= 1 << 9,
	cmd_rlim_core		= 1 << 10,
	cmd_rlim_cpu		= 1 << 11,
	cmd_rlim_data		= 1 << 12,
	cmd_rlim_fsize		= 1 << 13,
	cmd_rlim_locks		= 1 << 14,
	cmd_rlim_memlock	= 1 << 15,
	cmd_rlim_msgqueue	= 1 << 16,
	cmd_rlim_nofile		= 1 << 17,
	cmd_rlim_nproc		= 1 << 18,
	cmd_rlim_ofile		= 1 << 19,
	cmd_rlim_posixlocks	= 1 << 20,
	cmd_rlim_rss		= 1 << 21,
	cmd_rlim_sbsize		= 1 << 22,
	cmd_rlim_sigpending	= 1 << 23,
	cmd_rlim_stack		= 1 << 24,
	cmd_rlim_vmem		= 1 << 25
};

/* Variables for command line parameters */
static const char *arg_chroot = NULL;
#ifdef HAVE_JAIL
static const char *arg_jail = NULL;
static const char *arg_jail_host = NULL;
static const char *arg_jail_ip = NULL;
#endif /* HAVE_JAIL */
static const char *arg_limit_mem = NULL;
static const char *arg_priority = NULL;
static const char *arg_priv_group = NULL;
static const char *arg_priv_user = NULL;
static const char *arg_rlims[RLIM_NLIMITS];

/* Command line parameters and arguments */
static ngim_cmdline_params_t limiter_params[] = {
	{ "--help",			cmd_help,		NULL },
	{ "-h",				cmd_help,		NULL },
#ifdef HAVE_CHROOT
	{ "--chroot",		cmd_chroot,		&arg_chroot },
#endif
#ifdef HAVE_JAIL
	{ "--jail",			cmd_jail,		&arg_jail },
	{ "--jail-host",	cmd_jail_host,	&arg_jail_host },
	{ "--jail-ip",		cmd_jail_ip,	&arg_jail_ip },
#endif
#ifdef HAVE_SETPRIORITY
	{ "--priority",		cmd_priority,	&arg_priority },
	{ "-n",				cmd_priority,	&arg_priority },
#endif
	{ "--group",		cmd_priv_group,	&arg_priv_group },
	{ "-g",				cmd_priv_group,	&arg_priv_group },
	{ "--user",			cmd_priv_user,	&arg_priv_user },
	{ "-u",				cmd_priv_user,	&arg_priv_user },
#if defined(HAVE_GETRLIMIT) && defined(HAVE_SETRLIMIT)
	{ "--limit-mem",	cmd_limit_mem,	&arg_limit_mem },
	{ "-m",				cmd_limit_mem,	&arg_limit_mem },
	#ifdef RLIMIT_AS
	{ "--rlimit-as",
		cmd_rlim_as,
		&arg_rlims[RLIMIT_AS] },
	#endif
	#ifdef RLIMIT_CORE
	{ "--rlimit-core",
		cmd_rlim_core,
		&arg_rlims[RLIMIT_CORE] },
	{ "-c",
		cmd_rlim_core,
		&arg_rlims[RLIMIT_CORE] },
	#endif
	#ifdef RLIMIT_CPU
	{ "--rlimit-cpu",
		cmd_rlim_cpu,
		&arg_rlims[RLIMIT_CPU] },
	#endif
	#ifdef RLIMIT_DATA
	{ "--rlimit-data",
		cmd_rlim_data,
		&arg_rlims[RLIMIT_DATA] },
	#endif
	#ifdef RLIMIT_FSIZE
	{ "--rlimit-fsize",
		cmd_rlim_fsize,
		&arg_rlims[RLIMIT_FSIZE] },
	#endif
	#ifdef RLIMIT_LOCKS
	{ "--rlimit-locks",
		cmd_rlim_locks,
		&arg_rlims[RLIMIT_LOCKS] },
	#endif
	#ifdef RLIMIT_MEMLOCK
	{ "--rlimit-memlock",
		cmd_rlim_memlock,
		&arg_rlims[RLIMIT_MEMLOCK] },
	#endif
	#ifdef RLIMIT_MSGQUEUE
	{ "--rlimit-msgqueue",
		cmd_rlim_msgqueue,
		&arg_rlims[RLIMIT_MSGQUEUE] },
	#endif
	#ifdef RLIMIT_NOFILE
	{ "--rlimit-nofile",
		cmd_rlim_nofile,
		&arg_rlims[RLIMIT_NOFILE] },
	#endif
	#ifdef RLIMIT_NPROC
	{ "--rlimit-nproc",
		cmd_rlim_nproc,
		&arg_rlims[RLIMIT_NPROC] },
	{ "-p",
		cmd_rlim_nproc,
		&arg_rlims[RLIMIT_NPROC] },
	#endif
	#ifdef RLIMIT_POSIXLOCKS
	{ "--rlimit-posixlocks",
		cmd_rlim_posixlocks,
		&arg_rlims[RLIMIT_POSIXLOCKS] },
	#endif
	#ifdef RLIMIT_OFILE
	{ "--rlimit-ofile",
		cmd_rlim_ofile,
		&arg_rlims[RLIMIT_OFILE] },
	#endif
	#ifdef RLIMIT_RSS
	{ "--rlimit-rss",
		cmd_rlim_rss,
		&arg_rlims[RLIMIT_RSS] },
	#endif
	#ifdef RLIMIT_SBSIZE
	{ "--rlimit-sbsize",
		cmd_rlim_sbsize,
		&arg_rlims[RLIMIT_SBSIZE] },
	#endif
	#ifdef RLIMIT_SIGPENDING
	{ "--rlimit-sigpending",
		cmd_rlim_sigpending,
		&arg_rlims[RLIMIT_SIGPENDING] },
	#endif
	#ifdef RLIMIT_STACK
	{ "--rlimit-stack",
		cmd_rlim_stack,
		&arg_rlims[RLIMIT_STACK] },
	#endif
	#ifdef RLIMIT_VMEM
	{ "--rlimit-vmem",
		cmd_rlim_vmem,
		&arg_rlims[RLIMIT_VMEM] },
	#endif
#endif /* HAVE_GETRLIMIT && HAVE_SETRLIMIT */
	{ NULL,	0,	NULL }
};

static ngim_cmdline_args_t limiter_args[] = {
	{ NULL }
};


/* Prints simplified usage */
static __noreturn void die_usage(const char *argv0)
{
	char *str;
	int i, j;

	die_assert(argv0);
	
	if (ALLOC_FAIL(str, apr_psprintf(g_pool, "usage: %s --help | ", argv0))) {
		die_allocerror0();
	}

	for (i = 0; limiter_params[i].name; ++i) {
		/* Skip short names and parameters without arguments */
		if (limiter_params[i].name[0] != '-' ||
			limiter_params[i].name[1] != '-' ||
			!limiter_params[i].arg) {
			continue;
		}

		/* Omit the parameter if it shares an argument with an earlier one */
		for (j = 0; j < i; ++j) {
			if (limiter_params[j].name[0] != '-' ||
				limiter_params[j].name[1] != '-' ||
				!limiter_params[j].arg) {
				continue;
			}
			
			if (limiter_params[j].arg == limiter_params[i].arg) {
				break;
			}
		}

		if (j < i) {
			continue;
		}
		
		if (ALLOC_FAIL(str, apr_pstrcat(g_pool, str,
				"[", limiter_params[i].name, " arg] ", NULL))) {
			die_allocerror0();
		}
	}

	if (ALLOC_FAIL(str, apr_pstrcat(g_pool, str,
			"program [arguments]", NULL))) {
		die_allocerror0();
	}

	die_error1(str);
}

/* Validates command line. Present parameters are specified in selected.
 * Prints an error message and returns <0 if command line is invalid. */
static int validate_cmdline(const apr_uint32_t selected)
{
	apr_uint32_t jail_params;
	
	if (selected & cmd_help) {
		return -1;
	}

	/* Cannot have both chroot and jail */
	if (selected & cmd_chroot && selected & cmd_jail) {
		warn_error1("cannot use --chroot with --jail");
		return -1;
	}

	/* All jail parameters must be given, or none */
	jail_params = selected & (cmd_jail | cmd_jail_host | cmd_jail_ip);
	
	if (jail_params && jail_params !=
			(cmd_jail | cmd_jail_host | cmd_jail_ip)) {
		if (!(jail_params & cmd_jail)) {
			warn_error1("missing option --jail");
		}
		if (!(jail_params & cmd_jail_host)) {
			warn_error1("missing option --jail-host");
		}
		if (!(jail_params & cmd_jail_ip)) {
			warn_error1("missing option --jail-ip");
		}
		return -1;
	}

	return 0;
}

/* Sets process priority */
static inline void limit_priority()
{
#ifdef HAVE_SETPRIORITY
	apr_int64_t num;
	char *end;
	int prionum;
	
	die_assert(arg_priority);

	num = apr_strtoi64(arg_priority, &end, 10);
	if (*end != '\0') {
		die_error2("invalid priority value ", arg_priority);
	}
		
	if (num < PRIO_MIN) {
		warn_error1("value for priority too small");
		prionum = PRIO_MIN;
	} else if (num > PRIO_MAX) {
		warn_error1("value for priority too big");
		prionum = PRIO_MAX;
	} else {
		prionum = (int)num;
	}
	
	if (setpriority(PRIO_PROCESS, 0, prionum) < 0) {
		die_syserror1("failed to set priority");
	}
#endif
}

/* Sets limit for given resource */
static inline void do_limit(int res, const char *arg)
{
#if defined(HAVE_GETRLIMIT) && defined(HAVE_SETRLIMIT)
	apr_int64_t num;
	char *end;
	int set_hard;
	struct rlimit rlim;

	die_assert(arg);

	if (getrlimit(res, &rlim) < 0) {
		die_syserror1("failed to limit resources");
	}

	set_hard = (*arg == '=');
			
	if (set_hard) {
		/* Skip the '=' character */
		++arg;
	}
	
	num = apr_strtoi64(arg, &end, 10);
	
	if (*end != '\0' || num < 0) {
		die_error2("invalid resource limit value ", arg);
	}

	if (set_hard) {
		/* Set hard and soft limits */
		rlim.rlim_max = (rlim_t)num;
		rlim.rlim_cur = rlim.rlim_max;
	} else {
		/* Set soft limit */
		if (num > rlim.rlim_max) {
			warn_error1("value for soft limit too big");
			rlim.rlim_cur = rlim.rlim_max;
		} else {
			rlim.rlim_cur = (rlim_t)num;
		}
	}
		
	if (setrlimit(res, &rlim) < 0) {
		die_syserror1("failed to limit resources");
	}
#endif
}

/* Sets limits for given resources */
static inline void limit_resources()
{
	int i;

	for (i = 0; i < RLIM_NLIMITS; ++i) {
		/* If resource should be limited, the argument is non-NULL */
		if (arg_rlims[i]) {
			do_limit(i, arg_rlims[i]);
		}
	}
}

/* Sets all memory limits */
static inline void limit_memory()
{
	const int memres[] = {
	#ifdef RLIMIT_AS
		RLIMIT_AS,
	#endif
	#ifdef RLIMIT_DATA
		RLIMIT_DATA,
	#endif
	#ifdef RLIMIT_MEMLOCK
		RLIMIT_MEMLOCK,
	#endif
	#ifdef RLIMIT_STACK
		RLIMIT_STACK,
	#endif
	#ifdef RLIMIT_VMEM
		RLIMIT_VMEM,
	#endif
		-1
	};

	int i;
	
	die_assert(arg_limit_mem);

	for (i = 0; memres[i] != -1; ++i) {
		/* This does parse arg_limit_mem each time, but is simpler this way */
		do_limit(memres[i], arg_limit_mem);
	}
}

/* Sets up jail */
static inline void limit_jail()
{
#ifdef HAVE_JAIL
	struct jail jl;
	struct in_addr ip;
	
	die_assert(arg_jail);
	die_assert(arg_jail_host);
	die_assert(arg_jail_ip);

	jl.version = 0;
	
	if (ALLOC_FAIL(jl.path, apr_pstrdup(g_pool, arg_jail))) {
		die_allocerror0();
	}
	
	if (ALLOC_FAIL(jl.hostname, apr_pstrdup(g_pool, arg_jail_host))) {
		die_allocerror0();
	}
	
	if (!inet_aton(arg_jail_ip, &ip)) {
		die_syserror2("invalid jail IPv4 address ", arg_jail_ip);
	}

	jl.ip_number = ntohl(ip.s_addr);

	if (jail(&jl) < 0) {
		die_syserror2("failed to set up jail to ", arg_jail);
	}
#endif
}

/* Sets up chroot */
static inline void limit_chroot()
{
#ifdef HAVE_CHROOT
	die_assert(arg_chroot);
	
	if (chroot(arg_chroot) < 0) {
		die_syserror2("failed to chroot to ", arg_chroot);
	}

	if (chdir("/") < 0) {
		die_syserror1("failed to chdir to / in chroot");
	}
#endif
}

/* Drops privileges */
static inline void limit_priv()
{
	if (ngim_priv_drop(NGIM_PRIV_CURRENT, arg_priv_user, arg_priv_group) < 0) {
		die_error1("failed to drop privileges");
	}
}

int main(int argc, const char * const *argv, const char * const *env)
{
	apr_uint32_t selected;
	int i, first;

	ngim_base_app_init(PROGRAM_LIMITER, &argc, &argv, &env);

	/* Initialize argument array */
	for (i = 0; i < RLIM_NLIMITS; ++i) {
		arg_rlims[i] = NULL;
	}

	/* Parse and validate command line */
	first = ngim_cmdline_parse(argc, argv, 0, limiter_params, limiter_args,
				&selected);
	
	if (first <= 0 || validate_cmdline(selected) < 0) {
		die_usage(argv[0]);
	}

	/* Set priority */
	if (selected & cmd_priority) {
		limit_priority();
	}
	
	/* Limit resources */
	if (selected & cmd_limit_mem) {
		limit_memory();
	}
	limit_resources();

	/* Set up jail or chroot */
	if (selected & cmd_jail) {
		limit_jail();
	} else if (selected & cmd_chroot) {
		limit_chroot();
	}

	/* Drop privileges */
	if (selected & cmd_priv_group || selected & cmd_priv_user) {
		limit_priv();
	}

	/* Close possible unnecessary files */
	apr_pool_cleanup_for_exec();

	/* Start the program -- APR currently ignores apr_procattr_addrspace_set
	 * for most platforms, so we must use execvp instead */
	if (execvp(argv[first], (char *const *)&argv[first]) < 0) {
		die_syserror2("failed to execute ", argv[first]);
	}

	/* Never reached */
	return EXIT_FAILURE;
}
