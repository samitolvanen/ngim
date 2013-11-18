AM_CFLAGS = -Wall @APR_CFLAGS@
AM_CPPFLAGS = @APR_CPPFLAGS@ @NGIM_LIBBASE_INCLUDES@

LDADD = @APR_LDADD@ @NGIM_LIBBASE_LINK@
SRVHEADERS = srvctl.h $(top_builddir)/include/common.h

bin_PROGRAMS = limiter taiconv
sbin_PROGRAMS = monitor scanner srvctl tainlog

limiter_SOURCES = limiter.c $(SRVHEADERS)
monitor_SOURCES = monitor.c $(SRVHEADERS)
scanner_SOURCES = scanner.c $(SRVHEADERS)
srvctl_SOURCES  = srvctl.c  $(SRVHEADERS)
taiconv_SOURCES = taiconv.c $(SRVHEADERS)
tainlog_SOURCES = tainlog.c $(SRVHEADERS)
