AM_CFLAGS = @APR_CFLAGS@
AM_CPPFLAGS = @APR_CPPFLAGS@

ngimincludedir=$(includedir)/ngim

ngiminclude_HEADERS = base.h
lib_LTLIBRARIES = lib@PACKAGE_LIBNAME@.la

lib@PACKAGE_LIBNAME@_la_CFLAGS = -Wall
lib@PACKAGE_LIBNAME@_la_SOURCES = \
	init.c error.c \
	cmdline.c \
	tai.c iso8601.c \
	create.c \
	symlink.c \
	priv.c \
	base.h \
	$(top_builddir)/include/common.h

lib@PACKAGE_LIBNAME@_la_LIBADD = @APR_LDADD@
lib@PACKAGE_LIBNAME@_la_LDFLAGS = -version-info 0:0:0
