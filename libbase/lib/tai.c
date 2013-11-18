/*
 * tai.c
 *
 * This file is based on the public domain libtai library written
 * in 1998 by D. J. Bernstein <djb@pobox.com>
 *
 * Copyright 2005, 2006  Sami Tolvanen <sami@ngim.org>
 */

#include "common.h"
#include "base.h"
#include <apr_general.h>
#include <apr_time.h>

/* The maximum value for seconds in a TAI64 label */
#define TAI_MAX_SEC		APR_UINT64_C(9223372036854775807) /* 2^63 - 1 */
/* The maximum value for nano/attoseconds in a TAI64N(A) label */
#define TAI_MAX_NA		UINT32_C(999999999)
/* The maximum value for microseconds in apr_time_t */
#define APR_MAX_U		UINT32_C(999999)


/* Helper: converts from binary external format to textual external format */
static inline void format_textual(char *s, const unsigned char *p, int l)
{
	static const char nibble_to_hex[] = "0123456789abcdef";
	s[0] = '@';

	while (--l >= 0) {
		s[2 * l + 1] = nibble_to_hex[(p[l] >> 4) & 0xF];
		s[2 * l + 2] = nibble_to_hex[ p[l]       & 0xF];
	}
}

#define hex_to_nibble(c) \
	((unsigned char)(((c) >= 'a') ? ((c) - 'a' + 10) : ((c) - '0')))

/* Helper: converts from a textual external format to binary external format */
static inline int unformat_textual(const char *s, unsigned char *p,
		apr_off_t plen)
{
	apr_off_t i;

	if (s[0] != '@') {
		return 0;
	}

	for (i = 0; i < plen; ++i) {
		p[i] = (hex_to_nibble(s[2 * i + 1]) << 4) |
			   (hex_to_nibble(s[2 * i + 2]) & 0xF);
	}

	return 1;
}

/* Helper: converts APR seconds to TAI seconds */
#define tai_sec_from_apr(a) \
	(NGIM_TAI_APR_EPOCH + apr_time_sec(a))
/* Helper: converts TAI seconds to APR seconds */
#define apr_sec_from_tai(t) \
	((t)->x - NGIM_TAI_APR_EPOCH)


apr_time_t ngim_tai_to_apr(const ngim_tai_t *t)
{
	die_assert(t);
	return apr_time_from_sec(apr_sec_from_tai(t));
}

void ngim_tai_from_apr(ngim_tai_t *t, apr_time_t a)
{
	die_assert(t);
	t->x = tai_sec_from_apr(a);
}

void ngim_tai_now(ngim_tai_t *t)
{
	die_assert(t);
	t->x = tai_sec_from_apr(apr_time_now());
}

int ngim_tai_less(const ngim_tai_t *t, const ngim_tai_t *u)
{
	die_assert(t && u);
	return (t->x < u->x);
}

void ngim_tai_pack(unsigned char *s, const ngim_tai_t *t)
{
	apr_uint64_t x;

	die_assert(s && t);

	x = t->x;
	s[7] = x & 0xFF; x >>= 8;
	s[6] = x & 0xFF; x >>= 8;
	s[5] = x & 0xFF; x >>= 8;
	s[4] = x & 0xFF; x >>= 8;
	s[3] = x & 0xFF; x >>= 8;
	s[2] = x & 0xFF; x >>= 8;
	s[1] = x & 0xFF; x >>= 8;
	s[0] = x;
}

int ngim_tai_unpack(const unsigned char *s, ngim_tai_t *t)
{
	apr_uint64_t x;

	die_assert(s && t);

	x = s[0];
	x <<= 8; x += s[1];
	x <<= 8; x += s[2];
	x <<= 8; x += s[3];
	x <<= 8; x += s[4];
	x <<= 8; x += s[5];
	x <<= 8; x += s[6];
	x <<= 8; x += s[7];
	t->x = x;

	return (x <= TAI_MAX_SEC);
}

void ngim_tai_format(char *s, const ngim_tai_t *t)
{
	unsigned char p[NGIM_TAI_PACK];

	die_assert(s && t);

	ngim_tai_pack(p, t);
	format_textual(s, p, NGIM_TAI_PACK);
}

int ngim_tai_unformat(const char *s, ngim_tai_t *t)
{
	unsigned char p[NGIM_TAI_PACK];

	die_assert(s && t);

	return (unformat_textual(s, p, NGIM_TAI_PACK) && ngim_tai_unpack(p, t));
}

void ngim_tain_from_apr(ngim_tain_t *t, apr_time_t a)
{
	die_assert(t);

	/* APR supports microsecond accuracy at best */
	t->sec.x = tai_sec_from_apr(a);
	t->nano = 1000 * apr_time_usec(a) + 500;
}

apr_time_t ngim_tain_to_apr(const ngim_tain_t *t)
{
	apr_uint64_t s;
	apr_uint32_t u;

	die_assert(t);

	s = apr_sec_from_tai(&t->sec);
	u = (t->nano + 500) / 1000;

	if (unlikely(u > APR_MAX_U)) {
		u = 0;
		++s;
	}

	return apr_time_make(s, u);
}

void ngim_tain_to_tai(const ngim_tain_t *ta, ngim_tai_t *t)
{
	die_assert(ta && t);
	*t = ta->sec;
}

void ngim_tain_now(ngim_tain_t *t)
{
	ngim_tain_from_apr(t, apr_time_now());
}

int ngim_tain_less(const ngim_tain_t *t, const ngim_tain_t *u)
{
	die_assert(t && u);

	if (t->sec.x < u->sec.x) {
		return 1;
	}
	if (t->sec.x > u->sec.x) {
		return 0;
	}
	return (t->nano < u->nano);
}

int ngim_tain_diff(const ngim_tain_t *t, const ngim_tain_t *u,
		apr_uint64_t *d)
{
	die_assert(t && u && d);

	if (ngim_tain_less(t, u)) {
		*d = u->sec.x - t->sec.x;

		if (u->nano >= t->nano + 500000) {
			++(*d);
		}

		return 1;
	}

	return 0;
}

void ngim_tain_pack(unsigned char *s, const ngim_tain_t *t)
{
	apr_uint32_t x;

	die_assert(s && t);

	ngim_tai_pack(s, &t->sec);
	s += NGIM_TAI_PACK;

	x = t->nano;
	s[3] = x & 0xFF; x >>= 8;
	s[2] = x & 0xFF; x >>= 8;
	s[1] = x & 0xFF; x >>= 8;
	s[0] = x;
}

int ngim_tain_unpack(const unsigned char *s, ngim_tain_t *t)
{
	die_assert(s && t);

	if (ngim_tai_unpack(s, &t->sec)) {
		apr_uint32_t x;
		s += NGIM_TAI_PACK;

		x = s[0];
		x <<= 8; x += s[1];
		x <<= 8; x += s[2];
		x <<= 8; x += s[3];
		t->nano = x;

		return (x <= TAI_MAX_NA);
	}

	return 0;
}

void ngim_tain_format(char *s, const ngim_tain_t *t)
{
	unsigned char p[NGIM_TAIN_PACK];

	die_assert(s && t);

	ngim_tain_pack(p, t);
	format_textual(s, p, NGIM_TAIN_PACK);
}

int ngim_tain_unformat(const char *s, ngim_tain_t *t)
{
	unsigned char p[NGIM_TAIN_PACK];

	die_assert(s && t);

	return (unformat_textual(s, p, NGIM_TAIN_PACK) && ngim_tain_unpack(p, t));
}
