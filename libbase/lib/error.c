/*
 * error.c
 *
 * Copyright © 2005, 2006  Sami Tolvanen <sami@ngim.org>
 */

#include "common.h"
#include "base.h"

#define ERROR_BUFFERSIZE 128

/* Program name */
static const char *progname = NULL;

/* Error level */
static int error_level = INFO; /* Defaults to informational */

/*
 * Sets program name for error messages.
 */
void ngim_setprogname(const char *name)
{
	/* The name may be NULL */
	progname = name;
}

/*
 * Returns the error level name.
 */
static inline const char * strlevel(int level)
{
	switch (level) {
	case FATAL:
		return "fatal";
	case WARNING:
		return "warning";
	case INFO:
		return "information";
	case VERBOSE:
		return "debug";
	default:
		return "unknown";
	}
}

/*
 * Sets the minimum error level to be displayed.
 */
void ngim_seterrorlevel(int level)
{
	/* Never suppress fatal errors */
	if (level < FATAL) {
		error_level = level;
	} else {
		error_level = FATAL;
	}
}

/*
 * Prints an error message to stderr.
 */
void ngim_error6(int level, const char *s1, const char *s2,
		const char *s3, const char *s4, const char *s5, const char *s6)
{
	if (level < error_level) {
		return;
	}
	
	if (likely(g_apr_stderr)) {
		apr_file_flush(g_apr_stderr);
		/* Error level */
		apr_file_puts(strlevel(level), g_apr_stderr);
		apr_file_puts(": ", g_apr_stderr);
		/* Program name */
		if (likely(progname)) {
			apr_file_puts(progname, g_apr_stderr);
			apr_file_puts(": ", g_apr_stderr);
		}
		/* Messages */
		if (likely(s1)) apr_file_puts(s1, g_apr_stderr);
		if (s2) apr_file_puts(s2, g_apr_stderr);
		if (s3) apr_file_puts(s3, g_apr_stderr);
		if (s4) apr_file_puts(s4, g_apr_stderr);
		if (s5) apr_file_puts(s5, g_apr_stderr);
		if (s6) apr_file_puts(s6, g_apr_stderr);
		apr_file_puts("\n", g_apr_stderr);
		apr_file_flush(g_apr_stderr);
	} else {
		/* TODO: Use stdio? */
	}
}

/*
 * Prints out an APR error messages.
 */
void ngim_aprerror4(int level, apr_status_t status, const char *s1,
		const char *s2, const char *s3, const char *s4)
{
	char buffer[ERROR_BUFFERSIZE];
	const char *s5 = NULL, *s6 = NULL;

	if (likely(apr_strerror(status, buffer, sizeof(buffer)))) {
		if (s1 || s2 || s3 || s4) {
			s5 = ": ";
		}
		s6 = buffer;
	}

	ngim_error6(level, s1, s2, s3, s4, s5, s6);
}

void ngim_die_error6(const char *s1, const char *s2, const char *s3,
		const char *s4, const char *s5, const char *s6)
{
	ngim_error6(FATAL, s1, s2, s3, s4, s5, s6);
	exit(EXIT_FAILURE);
}

void ngim_die_aprerror4(apr_status_t status, const char *s1, const char *s2,
		const char *s3, const char *s4)
{
	ngim_aprerror4(FATAL, status, s1, s2, s3, s4);
	exit(EXIT_FAILURE);
}

const char * ngim_strexitwhy(apr_exit_why_e reason)
{
	if (APR_PROC_CHECK_EXIT(reason)) {
		return "normally";
	} else if (APR_PROC_CHECK_SIGNALED(reason)) {
		if (APR_PROC_CHECK_CORE_DUMP(reason)) {
			return "due to a signal (core dumped)";
		} else {
			return "due to a signal";
		}
	} else {
		return "for unknown reason";
	}
}
