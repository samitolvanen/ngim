AC_PREREQ(2.59)
AC_INIT([NGIM srvctl], [0.1], [sami@ngim.org], [ngim-srvctl])

AC_CONFIG_SRCDIR(src/scanner.c)

AC_CONFIG_AUX_DIR(config)
AC_CONFIG_HEADER(include/config.h)

AM_INIT_AUTOMAKE([1.9 dist-bzip2])

# Debugging
NGIM_DEBUG

# Checks for programs
AC_PROG_CC
AC_PROG_INSTALL

# Checks for libraries
NGIM_APR
NGIM_LIBBASE

# Checks for header files
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h netinet/in.h signal.h fcntl.h sys/stat.h \
				  sys/param.h sys/resource.h])
AC_CHECK_HEADERS([sys/jail.h], [], [], [#if HAVE_SYS_PARAM_H
											#include <sys/param.h>
										#endif])

# Checks for typedefs, structures, and compiler characteristics
AC_C_CONST

# Checks for library functions
AC_FUNC_MALLOC
AC_CHECK_FUNCS([alarm chdir chroot execvp getpid getrlimit jail memset open \
				qsort setpriority setrlimit strcmp strlen])

# Checks for functions that may not be in the default libraries
NGIM_CHECK_FUNC_LIBS(inet_aton, [resolv socket nsl])

# Output
AC_CONFIG_FILES([Makefile m4/Makefile src/Makefile tests/Makefile])
AC_OUTPUT
