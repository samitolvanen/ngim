/*
 * taiconv.c
 *
 * Copyright © 2005, 2006  Sami Tolvanen <sami@ngim.org> 
 */

#include "common.h"
#include "srvctl.h"
#include <apr_file_io.h>
#include <apr_lib.h>
#include <apr_mmap.h>
#include <apr_time.h>
#include <ngim/base.h>

/* Function pointer type for the ISO 8601 conversion */
typedef void (*iso8601_format)(char *s, apr_time_t t);

/* Bitmasks for command line parameters */
enum {
	cmd_help	= 1 << 0,
	cmd_local	= 1 << 1,
	cmd_utc		= 1 << 2,
	cmd_all		= 1 << 3
};

/* Variables for command line parameters */
static const char *arg_file = NULL;
static int arg_all = 0;
static iso8601_format arg_func_format = NULL;

/* Command line parameters and arguments */
static ngim_cmdline_params_t taiconv_params[] = {
	{ "--help",			cmd_help,	NULL },
	{ "-h",				cmd_help,	NULL },
	{ "--local-time",	cmd_local,	NULL },
	{ "-l",				cmd_local,	NULL },
	{ "--utc",			cmd_utc,	NULL },
	{ "-u",				cmd_utc,	NULL },
	{ "--all",			cmd_all,	NULL },
	{ "-a",				cmd_all, 	NULL },
	{ NULL,				0,			NULL }
};
static ngim_cmdline_args_t taiconv_args[] = {
	{ &arg_file },
	{ NULL }
};

#define CMDLINE_USAGE \
	"--help | [--local-time (default) | --utc] [--all] [file]"


/* Validates command line. Present parameters are specified in selected.
 * Prints an error message and returns <0 if command line is invalid. */
static int validate_cmdline(const apr_uint32_t selected)
{
	if (selected & cmd_help) {
		return -1;
	}

	if (selected & cmd_local && selected & cmd_utc) {
		warn_error1("invalid parameters");
		return -1;
	}

	/* Choose an ISO 8601 convertion function */
	if (selected & cmd_utc) {
		arg_func_format = ngim_iso8601_utc_format;
	} else {
		arg_func_format = ngim_iso8601_local_format;
	}

	/* Should we convert all time stamps or just the ones at the
	 * beginning of each line */
	if (selected & cmd_all) {
		arg_all = 1;
	}

	return 0;
}

/* Tests if a character is a valid ASCII hex nibble */
#define is_hex_nibble(c) \
	(((c) >= '0' && (c) <= '9') || ((c) >= 'a' && (c) <= 'f'))

/* Outputs a character to stdout, dies in case of a failure */
static inline void flush_char(const char ch)
{
	if (APR_FAIL_N(apr_file_putc(ch, g_apr_stdout))) {
		die_error1("failed to write to stdout");
	}
}

/* Outputs a string to stdout, dies in case of a failure */
static inline void flush_string(const char *str)
{
	die_assert(str);

	if (APR_FAIL_N(apr_file_puts(str, g_apr_stdout))) {
		die_error1("failed to write to stdout");
	}
}

/* Outputs i bytes from buffer to stdout, dies in case of a failure */
static inline void flush_buffer(const char *buf, int i)
{
	die_assert(buf);
	die_assert(i > 0);

	if (APR_FAIL_N(apr_file_write_full(g_apr_stdout, buf, i, NULL))) {
		die_error1("failed to write to stdout");
	}
}

/* The result from convert_buffer */
static char result[NGIM_ISO8601_FORMAT]; /* NUL-terminated */

/* Converts an external textual TAI64 or TAI64N label of length len to an
 * ISO 8601 date and time string returned in result. Returns non-zero if
 * a textual label converted. Assumes that any buffer of valid length is
 * a valid textual label. If there are unused bytes in textual, returns
 * the number of unused bytes in unused, and a pointer to the start of the
 * unused part in textual in remain. */
static inline int convert_buffer(const char *textual, apr_off_t len,
		const char **remain, int *unused)
{
	die_assert(textual);
	die_assert(remain);
	die_assert(unused);
	warn_assert(len > 0);

	*remain = NULL;
	*unused = 0;

	/* If we have a sequence of valid length, convert it */
	if (len >= NGIM_TAIN_FORMAT) {
		ngim_tain_t t_tain;
		if (ngim_tain_unformat(textual, &t_tain)) {
			arg_func_format(result, ngim_tain_to_apr(&t_tain));
			if (len > NGIM_TAIN_FORMAT) {
				*remain = &textual[NGIM_TAIN_FORMAT];
				*unused = len - NGIM_TAIN_FORMAT;
			}
			return 1;
		}
	} else if (len >= NGIM_TAI_FORMAT) {
		ngim_tai_t t_tai;
		if (ngim_tai_unformat(textual, &t_tai)) {
			arg_func_format(result, ngim_tai_to_apr(&t_tai));
			if (len > NGIM_TAI_FORMAT) {
				*remain = &textual[NGIM_TAI_FORMAT];
				*unused = len - NGIM_TAI_FORMAT;
			}
			return 1;
		}
	}

	return 0;
}

/* Converts all valid time stamps */
static inline void convert_read_all(apr_file_t *in)
{
	apr_status_t status;
	char textual[NGIM_TAIN_FORMAT + 1]; /* +1 for terminating character */
	const char *remain;
	int index = 0;
	int unused;

	die_assert(in);

	for (;;) {
		die_assert(index >= 0 && index <= NGIM_TAIN_FORMAT);

		if (APR_FAIL(status, apr_file_getc(&textual[index], in))) {
			/* If there was anything in the buffer, flush it */
			if (index > 0) {
				if (convert_buffer(textual, index, &remain, &unused)) {
					flush_string(result);
					if (unused > 0) {
						flush_buffer(remain, unused);
					}
				} else {
					flush_buffer(textual, index);
				}
			}
			if (APR_STATUS_IS_EOF(status)) {
				/* Done */
				break;
			} else {
				die_aprerror1(status, "failed to read from input");
			}
		}

		if (index > 0) {
			/* In the middle of a sequence */
			if (is_hex_nibble(textual[index])) {
				/* And there's more */
				if (index == NGIM_TAIN_FORMAT) {
					/* The sequence is too long, convert what we have */
					if (convert_buffer(textual, index, &remain, &unused)) {
						die_assert(!unused);
						flush_string(result);
						flush_char(textual[index]);
					} else {
						flush_buffer(textual, index + 1);
					}
					index = 0;
				} else {
					++index;
				}
			} else {
				/* Flush the sequence */
				if (convert_buffer(textual, index, &remain, &unused)) {
					flush_string(result);
					if (unused > 0) {
						flush_buffer(remain, unused);
					}
					/* See if another sequence follows immediately */
					if (textual[index] == '@') {
						index = 1;
						continue;
					} else {
						flush_char(textual[index]);
					}
				} else {
					/* See if another sequence follows immediately */
					if (textual[index] == '@') {
						flush_buffer(textual, index);
						index = 1;
						continue;
					} else {
						flush_buffer(textual, index + 1);
					}
				}
				/* Back to square zero */
				index = 0;
			}
		} else {
			if (textual[index] == '@') {
				/* Sequence starts */
				++index;
			} else {
				flush_char(textual[index]);
			}
		}
	}
}

/* Converts time stamps at the beginning of each line */
static inline void convert_read_nrm(apr_file_t *in)
{
	apr_status_t status;
	char textual[NGIM_TAIN_FORMAT + 1]; /* +1 for terminating character */
	const char *remain;
	int index = 0;
	int start = 1;
	int unused;

	die_assert(in);

	for (;;) {
		die_assert(index >= 0 && index <= NGIM_TAIN_FORMAT);

		if (APR_FAIL(status, apr_file_getc(&textual[index], in))) {
			/* If there was anything in the buffer, flush it */
			if (index > 0) {
				if (convert_buffer(textual, index, &remain, &unused)) {
					flush_string(result);
					if (unused > 0) {
						flush_buffer(remain, unused);
					}
				} else {
					flush_buffer(textual, index);
				}
			}
			if (APR_STATUS_IS_EOF(status)) {
				/* Done */
				break;
			} else {
				die_aprerror1(status, "failed to read from input");
			}
		}

		if (start && textual[index] == '@') {
			start = 0;
			++index;
		} else if (index > 0) {
			if (!is_hex_nibble(textual[index]) ||
					index == NGIM_TAIN_FORMAT) {
				/* End of stamp or sequence too long */
				if (convert_buffer(textual, index, &remain, &unused)) {
					flush_string(result);
					if (unused > 0) {
						flush_buffer(remain, unused);
					}
					flush_char(textual[index]);
				} else {
					flush_buffer(textual, index + 1);
				}
				start = (textual[index] == '\n');
				index = 0;
			} else {
				++index;
			}
		} else {
			start = (textual[index] == '\n');
			flush_char(textual[index]);
		}
	}
}

/* Tries to convert a file using buffered I/O where available. */
static int convert_read(const char *file)
{
	apr_status_t status;
	apr_file_t *in;

	/* File pointer for incoming data */
	if (file) {
		/* Use APR's internal buffering so we won't have to */
		if (APR_FAIL(status, apr_file_open(&in, file, APR_FOPEN_READ |
				APR_FOPEN_BINARY | APR_FOPEN_BUFFERED, 0, g_pool))) {
			die_aprerror2(status, "failed to open file ", file);
		}
	} else {
		/* TODO: APR currently doesn't allow buffering with stdin */
		in = g_apr_stdin;
	}

	die_assert(in);

	/* Drop unneeded privileges */
	if (ngim_priv_drop(NGIM_PRIV_NONE, NULL, NULL) < 0) {
		warn_error1("failed to drop privileges");
	}

	/* Start converting */
	if (arg_all) {
		convert_read_all(in);
	} else {
		convert_read_nrm(in);
	}

	return 1;
}

/* Converts all valid timestamps */
static inline void convert_mmap_all(const char *textual, apr_off_t size)
{
	apr_off_t index = 0;
	apr_off_t start = 0;
	const char *remain;
	int inside = 0;
	int unused;

	die_assert(textual);
	die_assert(size > 0); /* Zero-sized mmap? */

	while (index < size) {
		if (inside) {
			if (!is_hex_nibble(textual[index]) ||
						index - start == NGIM_TAIN_FORMAT) {
				/* End of stamp or sequence too long */
				if (convert_buffer(&textual[start], index - start, &remain,
							&unused)) {
					/* Flush the stamp */
					flush_string(result);
					start = index - unused;
				}
				inside = 0;
				continue; /* Continue by rechecking textual[index] */
			}
		} else if (textual[index] == '@') {
			if (start < index) {
				/* Flush what we have processed so far */
				flush_buffer(&textual[start], index - start);
			}
			start = index;
			inside = 1;
		}
		++index;
	}

	/* Flush the remains */
	if (inside &&
		convert_buffer(&textual[start], index - start, &remain, &unused)) {
		flush_string(result);
		if (unused > 0) {
			flush_buffer(remain, unused);
		}
	} else {
		die_assert(start < index);
		flush_buffer(&textual[start], index - start);
	}
}

/* Converts time stamps at the beginning of each line */
static inline void convert_mmap_nrm(const char *textual, apr_off_t size)
{
	apr_off_t index = 0;
	apr_off_t start = 0;
	const char *remain;
	int unused;

	die_assert(textual);
	die_assert(size > 0); /* Zero-sized mmap? */

	while (index < size) {
		if (textual[index] == '@') {
			/* Flush what we have processed so far */
			if (start < index) {
				flush_buffer(&textual[start], index - start);
				start = index;
			}
			
			/* Calculate stamp length */
			while (++index < size && is_hex_nibble(textual[index]) &&
					index - start <= NGIM_TAIN_FORMAT)
				/* Do nothing */ ;

			if (convert_buffer(&textual[start], index - start, &remain,
						&unused)) {
				/* Flush the stamp */
				flush_string(result);
				start = index - unused;
			}
		}

		/* Move to the beginning of the next line */
		while (index < size && textual[index++] != '\n')
			/* Do nothing */ ;
	}

	/* Flush the remains */
	if (start < index) {
		flush_buffer(&textual[start], index - start);
	}
}

/* Tries to convert the file using mmap. If successful, returns a non-zero
 * value. Caller should always fall back to convert_read if this fails. */
static int convert_mmap(const char *file)
{
#if APR_HAS_MMAP
	apr_status_t status;
	apr_file_t *in;
	apr_finfo_t finfo;
	apr_mmap_t *map;

	/* File pointer for incoming data */
	if (file) {
		if (APR_FAIL(status, apr_file_open(&in, file, APR_FOPEN_READ |
				APR_FOPEN_BINARY, 0, g_pool))) {
			die_aprerror2(status, "failed to open file ", file);
		}
	} else {
		in = g_apr_stdin;
	}

	die_assert(in);

	/* Read file size, and map to memory */
	if (APR_FAIL(status, apr_file_info_get(&finfo,
					APR_FINFO_SIZE, in)) ||
		APR_FAIL(status, apr_mmap_create(&map, in, 0, finfo.size,
				APR_MMAP_READ, g_pool))) {
		/* An empty file, a pipe, or another failure */
		if (file) {
			apr_file_close(in);
		}
		return 0;
	}

	die_assert(map);

	/* Drop unneeded privileges */
	if (ngim_priv_drop(NGIM_PRIV_NONE, NULL, NULL) < 0) {
		warn_error1("failed to drop privileges");
	}

	/* Start converting */
	if (arg_all) {
		convert_mmap_all((const char*)map->mm, finfo.size);
	} else {
		convert_mmap_nrm((const char*)map->mm, finfo.size);
	}

	apr_mmap_delete(map);
	
	return 1;
#else
	return 0;
#endif
}

int main(int argc, const char * const *argv, const char * const *env)
{
	apr_uint32_t selected;

	ngim_base_app_init(PROGRAM_TAICONV, &argc, &argv, &env);

	if (ngim_cmdline_parse(argc, argv, 1, taiconv_params, taiconv_args,
			&selected) < 0 ||
		validate_cmdline(selected) < 0) {
		die_error4("usage: ", argv[0], " ", CMDLINE_USAGE);
	}

	die_assert(arg_func_format);

	if (convert_mmap(arg_file) || convert_read(arg_file)) {
		return EXIT_SUCCESS;
	} else {
		return EXIT_FAILURE;
	}
}
