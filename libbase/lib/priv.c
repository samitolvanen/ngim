/*
 * priv.c
 * 
 * Copyright 2005, 2006  Sami Tolvanen <sami@ngim.org>
 */

#include "base.h"
#include "common.h"

#if HAVE_SYS_CAPABILITY_H
static cap_value_t caps_netsrv[] = {
	/* This may be useful for allocating secure memory */
	#ifdef CAP_IPC_LOCK
		CAP_IPC_LOCK,
	#endif
	/* This is needed to bind a socket to a privileged port */
	#ifdef CAP_NET_BIND_SERVICE
		CAP_NET_BIND_SERVICE,
	#endif
	/* This is needed for dropping group privileges */
	#ifdef CAP_SETGID
		CAP_SETGID,
	#endif
	/* This is needed for dropping user privileges */
	#ifdef CAP_SETUID
		CAP_SETUID
	#endif
};

static cap_value_t caps_srvctl[] = {
	/* This is needed for terminating children with different uids */
	#ifdef CAP_KILL
		CAP_KILL
	#endif
};

static inline int priv_cap_set_flag(cap_t ct, cap_flag_t cf, int level)
{
	int rv = -1;
	
	switch (level) {
	case NGIM_PRIV_NONE:
		/* Set no flags */
		return 0;
	case NGIM_PRIV_NETSRV:
		rv = cap_set_flag(ct, cf, NELEMS(caps_netsrv), caps_netsrv, CAP_SET);
		break;
	case NGIM_PRIV_SRVCTL:
		rv = cap_set_flag(ct, cf, NELEMS(caps_srvctl), caps_srvctl, CAP_SET);
		break;
	default:
		die_error1("invalid privilege level");
	}

	return rv;
}

static int priv_setcaps(int level)
{
	cap_t ct;

	if (level == NGIM_PRIV_CURRENT) {
		/* Do not change capabilities */
		return 0;
	}

#if HAVE_PRCTL && defined(PR_SET_KEEPCAPS)
	/* Make sure capabilities aren't preserved over uid change */
	if (prctl(PR_SET_KEEPCAPS, 0) < 0) {
		return -1;
	}
#endif

	/* Drop capabilities to a desired level */
	if (ALLOC_FAIL(ct, cap_init())) {
		return -1;
	}
	
	if (priv_cap_set_flag(ct, CAP_PERMITTED, level) < 0 ||
		priv_cap_set_flag(ct, CAP_EFFECTIVE, level) < 0) {
		cap_free(ct);
		return -1;
	}

	if (cap_set_proc(ct) < 0) {
		cap_free(ct);
		return -1;
	}

	cap_free(ct);

	return 0;
}
#endif /* HAVE_SYS_CAPABILITY_H */

static inline int priv_getuid(const char *name)
{
	struct passwd *pw;
	die_assert(name);

	if (ALLOC_FAIL(pw, getpwnam(name))) {
		return -1;
	}

	return pw->pw_uid;
}

static inline int priv_getgid(const char *name)
{
	struct group *gr;
	die_assert(name);

	if (ALLOC_FAIL(gr, getgrnam(name))) {
		return -1;
	}

	return gr->gr_gid;
}

static inline int priv_setuid(uid_t uid)
{
	if (setuid(uid) < 0) {
		return -1;
	}

	/* Must not be able to regain root privileges */
	if (uid > 0 && setuid(0) == 0) {
		die_error1("unable to drop privileges");
	}

	return 0;
}

static inline int priv_setgid(gid_t gid)
{
#if HAVE_SETGROUPS
	if (setgroups(1, &gid) < 0) {
		return -1;
	}
#endif
	
	if (setgid(gid) < 0) {
		return -1;
	}

	return 0;
}

int ngim_priv_drop(int level, const char *uname, const char *gname)
{
	int uid;

	/* Set group id */
	if (gname) {
		int gid = priv_getgid(gname);

		if (gid < 0) {
			warn_error2("unknown group ", gname);
			return -1;
		}

		if (priv_setgid(gid) < 0) {
			warn_syserror2("failed to change group to ", gname);
			return -1;
		}
	}

	/* Set user id */
	if (uname) {
		uid = priv_getuid(uname);

		if (uid < 0) {
			warn_error2("unknown user ", uname);
			return -1;
		}

		if (priv_setuid(uid) < 0) {
			warn_syserror2("failed to change user to ", uname);
			return -1;
		}
	} else {
		uid = getuid();
	}

	/* Drop unneeded privileges if still running as root */
	if (uid == 0) {
#if HAVE_SYS_CAPABILITY_H
		/* Set capabilities */
		if (priv_setcaps(level) < 0) {
			warn_error1("failed to drop capabilities");
			return -1;
		}
#endif
		/* TODO: add platform-specific functionality here */
	}

	return 0;
}
