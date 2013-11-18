AC_PREREQ(2.59)
AC_INIT([NGIM Base library], [0.1], [sami@ngim.org], [ngim-libbase])
AC_SUBST(PACKAGE_LIBNAME, ngimbase)

AC_CONFIG_SRCDIR(lib/init.c)

AC_CONFIG_AUX_DIR(config)
AC_CONFIG_HEADER(include/config.h)

AM_INIT_AUTOMAKE([1.9 dist-bzip2])

# Debugging
NGIM_DEBUG

# Checks for programs.
AC_PROG_CC
AC_PROG_LIBTOOL
AC_PROG_INSTALL

# Checks for libraries.
NGIM_APR
AC_CHECK_LIB(cap, cap_init)

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([grp.h pwd.h sys/capability.h sys/prctl.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

# Checks for library functions.
AC_CHECK_FUNCS([atexit getpwnam getgrnam memset prctl readlink setgid \
				setgroups setuid strcmp symlink])

# Output
AC_CONFIG_FILES([Makefile m4/Makefile lib/Makefile
                 $PACKAGE_TARNAME-config:lib-config.in])
AC_OUTPUT
