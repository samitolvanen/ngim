/*
 * iso8601.c
 *
 * Copyright © 2005, 2006  Sami Tolvanen <sami@ngim.org>
 */

#include "common.h"
#include "base.h"

/*
 * Formats apr_time_exp_t to ISO 8601:2004 format with at least four-digit
 * year and microsecond precision (if present).
 */
static void format_iso8601(char *s, const apr_time_exp_t *exp)
{
	int tmp;
	die_assert(s && exp);

	/* Year */
	tmp = exp->tm_year + 1900;
	if (unlikely(tmp > 9999)) {
		warn_assert(tmp < 100000); /* Y100K bug */
		*s++ = tmp / 10000 + '0';
		*s++ = tmp % 10000 / 1000 + '0';
	} else {
		*s++ = tmp / 1000 + '0';
	}
	*s++ = tmp % 1000 / 100 + '0';
	*s++ = tmp % 100 / 10 + '0';
	*s++ = tmp % 10 + '0';
	*s++ = '-';
	/* Month */
	tmp = exp->tm_mon + 1;
	*s++ = tmp / 10 + '0';
	*s++ = tmp % 10 + '0';
	*s++ = '-';
	/* Day */
	*s++ = exp->tm_mday / 10 + '0';
	*s++ = exp->tm_mday % 10 + '0';
	*s++ = ' ';
	/* Hours */
	*s++ = exp->tm_hour / 10 + '0';
	*s++ = exp->tm_hour % 10 + '0';
	*s++ = ':';
	/* Minutes */
	*s++ = exp->tm_min / 10 + '0';
	*s++ = exp->tm_min % 10 + '0';
	*s++ = ':';
	/* Seconds */
	*s++ = exp->tm_sec / 10 + '0';
	*s++ = exp->tm_sec % 10 + '0';
	/* Microseconds */
	if (exp->tm_usec > 0) {
		*s++ = '.';
		*s++ = exp->tm_usec / 100000 + '0';
		*s++ = exp->tm_usec % 100000 / 10000 + '0';
		*s++ = exp->tm_usec % 10000 / 1000 + '0';
		*s++ = exp->tm_usec % 1000 / 100 + '0';
		*s++ = exp->tm_usec % 100 / 10 + '0';
		*s++ = exp->tm_usec % 10 + '0';
	}
	/* Time zone offset */
	if (exp->tm_gmtoff != 0) {
		/* Sign */
		if (exp->tm_gmtoff > 0) {
			*s++ = '+';
		} else {
			*s++ = '-';
		}
		/* Hours */
		tmp = exp->tm_gmtoff / 3600;
		*s++ = tmp / 10 + '0';
		*s++ = tmp % 10 + '0';
		/* Minutes */
		tmp = exp->tm_gmtoff % 3600 / 60;
		if (tmp > 0) {
			*s++ = tmp / 10 + '0';
			*s++ = tmp % 10 + '0';
		}
	} else {
		/* UTC */
		*s++ = 'Z';
	}
	/* NUL-termination */
	*s = '\0';
}

/*
 * Formats an ISO 8601 date and time string in the UTC time zone.
 */
void ngim_iso8601_utc_format(char *s, apr_time_t t)
{
	apr_status_t status;
	apr_time_exp_t exp;

	/* TODO: this is quite probably vulnerable to the Y2.038K bug as
	 * long as it uses gmtime internally */
	if (APR_FAIL(status, apr_time_exp_gmt(&exp, t))) {
		warn_aprerror1(status, "apr_time_exp_gmt failed"); /* Huh? */
	}
	format_iso8601(s, &exp);
}

/*
 * Formats an ISO 8601 date and time string in the local time zone.
 */
void ngim_iso8601_local_format(char *s, apr_time_t t)
{
	apr_status_t status;
	apr_time_exp_t exp;
	
	/* TODO: this is quite probably vulnerable to the Y2.038K bug as
	 * long as it uses localtime internally */
	if (APR_FAIL(status, apr_time_exp_lt(&exp, t))) {
		warn_aprerror1(status, "apr_time_exp_lt failed"); /* Huh? */
	}
	format_iso8601(s, &exp);
}
