/*
 * cmdline.c
 *
 * Copyright © 2005, 2006  Sami Tolvanen <sami@ngim.org>
 */

#include "common.h"
#include "base.h"

/*
 * Parses command line parameters specified in params. Marks present
 * parameters to bitmask selected and sets argument pointers to variables
 * optionally set in params. Returns <0 on error and prints an error
 * message. Return value >0 is the index of the first argument in argv.
 */
static int parse_params(int argc, const char * const *argv,
		ngim_cmdline_params_t *params, apr_uint32_t *selected)
{
	int i, j;
	int param;

	die_assert(argv);
	die_assert(params);
	die_assert(selected);

	for (i = 1; i < argc; ++i) {
		die_assert(argv[i]);
		
		if (argv[i][0] != '-') {
			/* End of parameters */
			return i;
		} else if (argv[i][1] == '\0' ||
				(argv[i][1] == '-' && argv[i][2] == '\0')) {
			/* End of parameters after - or -- */
			if (++i < argc) {
				return i;
			} else {
				return 0;
			}
		}

		/* See if the parameter is valid */
		for (param = 0; params[param].name; ++param) {
			if (!strcmp(argv[i], params[param].name)) {
				/* Found a match */
				if (*selected & params[param].cmd) {
					warn_error2(params[param].name, " already set");
					return -1;
				} else {
					*selected |= params[param].cmd;
				}

				/* Possible argument */
				if (params[param].arg) {
					if (++i >= argc) {
						warn_error2(params[param].name, " missing argument");
						return -1;
					} else if (*params[param].arg) {
						warn_error2(params[param].name,
							" argument already set");
					} else {
						/* Probably a negative integer, but if the argument
						 * matches a valid parameter, assume its an error */
						if (argv[i][0] == '-') {
							for (j = 0; params[j].name; ++j) {
								if (!strcmp(argv[i], params[j].name)) {
									warn_error2(params[param].name,
										" has an invalid argument");
									return -1;
								}
							}
						}
						/* Appears valid */
						*params[param].arg = argv[i];
					}
				}
				break;
			}
		}

		if (!params[param].name) {
			warn_error2("unknown parameter ", argv[i]);
			return -1;
		}
	}

	/* Everything was parsed */
	return 0;
}

/*
 * Parses command line arguments specified in args. Returns <0 on error.
 * A return value >0 is in the index of the first unused argument in argv.
 */
static int parse_args(int argc, const char * const *argv, int index,
		ngim_cmdline_args_t *args)
{
	int i;

	die_assert(argv);
	die_assert(args);

	for (i = 0; args[i].arg; ++index, ++i) {
		/* If there are arguments, there must be enough for all */
		if (index >= argc) {
			warn_error1("too few arguments");
			return -1;
		}

		if (*args[i].arg) {
			warn_error1("argument already set");
			return -1;
		} else {
			*args[i].arg = argv[index];
		}
	}

	if (index < argc) {
		return index;
	} else {
		return 0;
	}
}

int ngim_cmdline_parse(int argc, const char * const *argv, int noextra,
		ngim_cmdline_params_t *params, ngim_cmdline_args_t *args,
		apr_uint32_t *selected)
{
	int index;

	die_assert(argv);
	die_assert(params);
	die_assert(selected);

	/* Nothing selected yet */
	*selected = 0;

	/* Sanity check */
	if (argc < 2) {
		return 0;
	}

	/* Parse parameters */
	index = parse_params(argc, argv, params, selected);

	/* If there are any arguments left, parse them as well */
	if (index > 0) {
		if (args) {
			index = parse_args(argc, argv, index, args);
		}

		/* Any unwanted unused arguments? */
		if (noextra && index > 0) {
			warn_error1("too many arguments");
			return -1;
		}
	}

	return index;
}
