dnl ngim.m4 - M4 macros for NGIM
dnl
dnl Copyright © 2005, 2006  Sami Tolvanen <sami@ngim.org>

AC_DEFUN([NGIM_APR],
	[
		APR_FIND_APR(, , 1, 1)

		if test "$apr_found" != "yes" || test "x$apr_config" = "x"; then
			AC_MSG_ERROR(APR not found. Set location using --with-apr)
		fi


		APR_CFLAGS="`$apr_config --cflags`"
		APR_CPPFLAGS="`$apr_config --includes` `$apr_config --cppflags`"
		APR_LDADD="`$apr_config --link-ld` `$apr_config --libs`"
		
		AC_SUBST(APR_CFLAGS)
		AC_SUBST(APR_CPPFLAGS)
		AC_SUBST(APR_LDADD)
	]
)

AC_DEFUN([NGIM_APU],
	[
		APR_FIND_APU(, , 1, 1)

		if test "$apu_found" != "yes" || test "x$apu_config" = "x"; then
			AC_MSG_ERROR(APR-util not found. Set location using --with-apr-util)
		fi


		APU_CPPFLAGS="`$apu_config --includes`"
		APU_LDADD="`$apu_config --link-ld` `$apu_config --libs`"
		
		AC_SUBST(APU_CPPFLAGS)
		AC_SUBST(APU_LDADD)
	]
)

AC_DEFUN([NGIM_ASN1C],
	[
		AC_ARG_WITH(asn1c,
					AC_HELP_STRING([--with-asn1c=PATH],
						[Path to the asn1c ASN.1 compiler]),
					[asn1c_path="${withval}:${withval}/bin:$PATH"],
					[asn1c_path="$PATH"])

		AC_PATH_PROG([asn1c_prog], [asn1c], [no], [$asn1c_path])

		if test "$asn1c_prog" = "no" ; then
			AC_MSG_ERROR(asn1c compiler not found. Set location using --with-asn1c)
		fi

		ASN1C="$asn1c_prog"
		AC_SUBST(ASN1C)
	]
)

AC_DEFUN([NGIM_DEBUG],
	[
		AC_MSG_CHECKING(NGIM Debugging)
		
		AC_ARG_ENABLE(ngim-debug,
					  AC_HELP_STRING([--disable-ngim-debug],
					  	[Disable NGIM debugging (enabled by default)]),
					  [ngim_debug=$enableval],
					  [ngim_debug="yes"])

		if test "$ngim_debug" = "no"; then
			AC_DEFINE(NGIM_NDEBUG, 1, [Define to disable NGIM debugging])
			AC_MSG_RESULT(disabled)
		else
			AC_MSG_RESULT(enabled)
		fi
	]
)

AC_DEFUN([NGIM_LIBBASE],
	[
		AC_ARG_WITH(ngim-libbase,
					AC_HELP_STRING([--with-ngim-libbase=PATH],
						[Path to ngim-libbase installation]),
					[ngim_path="${withval}:${withval}/bin:$PATH"],
					[ngim_path="$PATH"])
			
		AC_PATH_PROG([ngim_libbase_config], [ngim-libbase-config], [no], [$ngim_path])

		if test "$ngim_libbase_config" = "no" ; then
			AC_MSG_ERROR(NGIM Base library not found. Set location using --with-ngim-libbase)
		fi

		NGIM_LIBBASE_INCLUDES="`$ngim_libbase_config --includes`"
		NGIM_LIBBASE_LINK="`$ngim_libbase_config --link`"
		
		AC_SUBST(NGIM_LIBBASE_INCLUDES)
		AC_SUBST(NGIM_LIBBASE_LINK)
	]
)

AC_DEFUN([NGIM_CHECK_FUNC_LIBS],
	[
		AC_CHECK_FUNCS($1)
		
		if test $ac_cv_func_$1 = no; then
			for lib in $2; do
				AC_CHECK_LIB($lib, $1,
					[AC_DEFINE(AS_TR_CPP(HAVE_$1)) LIBS="-l$lib $LIBS"; break])
			done
		fi
	]
)
