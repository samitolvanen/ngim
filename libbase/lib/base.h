/*
 * base.h
 *
 * Copyright © 2005, 2006, 2007  Sami Tolvanen <sami@ngim.org>
 */

#ifndef NGIM_BASE_H
#define NGIM_BASE_H 1

/**
 * @file base.h
 * @brief Main header file for the NGIM Base library
 */

#include <apr_general.h>
#include <apr_errno.h>
#include <apr_file_info.h>
#include <apr_file_io.h>
#include <apr_poll.h>
#include <apr_pools.h>
#include <apr_thread_proc.h>
#include <apr_time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup ngim-base NGIM Base library
 * @{
 */

/**
 * Compiler-specific macros
 */
#if __GNUC__ >= 3
	#define inline				inline __attribute__ ((always_inline))
	#define __noreturn			__attribute__ ((noreturn))
	#define __unused			__attribute__ ((unused))
	#define likely(x)			__builtin_expect (!!(x), 1)
	#define unlikely(x)			__builtin_expect (!!(x), 0)
	#if __GNUC__ > 3 || __GNUC_MINOR__ >= 4
		#undef  __must_check
		#define __must_check	__attribute__ ((warn_unused_result))
	#else
		#define __must_check	/* no warn_unused_result */
	#endif
#else
	#define __noreturn			/* no noreturn */
	#define __must_check		/* no warn_unused_result */
	#define __unused			/* no unused */
	#define likely(x)			(x)
	#define unlikely(x)			(x)
#endif

/**
 * @defgroup base-global-vars Global variables
 * @{
 */

/** APR stdin */
extern apr_file_t *g_apr_stdin;
/** APR stdout */
extern apr_file_t *g_apr_stdout;
/** APR stderr */
extern apr_file_t *g_apr_stderr;
/** global memory pool */
extern apr_pool_t *g_pool;

/** @} */

/**
 * @defgroup base-init Library initialization
 * @{
 */

/**
 * Initializes the NGIM Base library.
 * Initializes APR, opens standard input and outputs, creates the global
 * memory pool, sets up cleanup. This must be the first function called
 * for any library using the library.
 * @see ngim_base_app_init()
 */
extern void ngim_base_init();

/**
 * Initializes the NGIM Base library.
 * Initializes APR, opens standard input and outputs, creates the global
 * memory pool, sets up cleanup. This must be the first function called
 * for any application using the library.
 * @param name Name identifying the program.
 * @param argc Pointer to the argc that may be corrected.
 * @param argv Pointer to the argv that may be corrected.
 * @param env Pointer to the env that may be corrected, may be NULL.
 * @see ngim_base_init()
 */
extern void ngim_base_app_init(const char *name, int *argc,
		const char * const **argv, const char * const **env);

/**
 * Creates a child process.
 * Initializes APR properly after forking. This should be used instead of
 * apr_proc_fork.
 * @param pool Pointer to a pool to use.
 * @return If successful, returns the PID of the child in the parent process
 *		   and 0 in the child process. Otherwise, returns -1 and no children
 *		   are created.
 */
extern int ngim_base_fork(apr_pool_t *pool);

/** @} */

/**
 * @defgroup base-priv Application privileges
 * @{
 */

/** Do not change privilege level */
#define NGIM_PRIV_CURRENT	0
/** Drop all privileges */
#define NGIM_PRIV_NONE		1
/** Drop privileges not needed by a network service */
#define NGIM_PRIV_NETSRV	2
/** Drop privileges not needed by a service control daemon */
#define NGIM_PRIV_SRVCTL	3

/**
 * Drops unneeded privileges
 * @param[in] level The privilege level for processes with root privileges.
 * @param[in] uname The user name, or NULL to leave unchanged.
 * @param[in] gname The group name, or NULL to leave unchanged.
 * @return If successful, returns 0. Otherwise, a value <0.
 * @remarks Daemons should change to an unprivileged uid instead of relying
 *          only on the platform-specific privilege level implementation.
 */
extern int __must_check ngim_priv_drop(int level, const char *uname,
		const char *gname);

/** @} */

/**
 * @defgroup base-errors Error reporting
 * @{
 */

/**
 * Sets the program name for error functions.
 * @param[in] name Name identifying the program.
 * @see ngim_error6()
 */
extern void ngim_setprogname(const char *name);

/**
 * Sets the minimum error message level to be printed out.
 * @param[in] level The error level.
 * @remarks Does not allow #FATAL errors to be suppressed.
 * @see ngim_error6()
 */
extern void ngim_seterrorlevel(int level);

/**
 * Reports an error consisting of up to six message parts.
 * @param[in] level Error level.
 * @param[in] s1 A message string, or NULL if there is none.
 * @param[in] s2 A message string, or NULL if there is none.
 * @param[in] s3 A message string, or NULL if there is none.
 * @param[in] s4 A message string, or NULL if there is none.
 * @param[in] s5 A message string, or NULL if there is none.
 * @param[in] s6 A message string, or NULL if there is none.
 * @remarks Messages are always written to g_apr_stderr and prepended
 *          by a name if one is set.
 * @see ngim_setprogname()
 */
extern void ngim_error6(int level, const char *s1, const char *s2,
		const char *s3, const char *s4, const char *s5, const char *s6);

/**
 * Calls ngim_error6() with error level #FATAL and exits.
 * @param[in] s1 A message string, or NULL if there is none.
 * @param[in] s2 A message string, or NULL if there is none.
 * @param[in] s3 A message string, or NULL if there is none.
 * @param[in] s4 A message string, or NULL if there is none.
 * @param[in] s5 A message string, or NULL if there is none.
 * @param[in] s6 A message string, or NULL if there is none.
 * @see ngim_error6()
 */
extern void __noreturn ngim_die_error6(const char *s1, const char *s2,
		const char *s3, const char *s4, const char *s5, const char *s6);

/**
 * Reports an error consisting of APR error up to four messages.
 * @param[in] level Error level.
 * @param[in] status APR error.
 * @param[in] s1 A message string, or NULL if there is none.
 * @param[in] s2 A message string, or NULL if there is none.
 * @param[in] s3 A message string, or NULL if there is none.
 * @param[in] s4 A message string, or NULL if there is none.
 * @see ngim_error6()
 */
extern void ngim_aprerror4(int level, apr_status_t status, const char *s1,
		const char *s2, const char *s3, const char *s4);

/**
 * Calls ngim_aprerror4() with error level #FATAL and exits.
 * @param[in] status APR error.
 * @param[in] s1 A message string, or NULL if there is none.
 * @param[in] s2 A message string, or NULL if there is none.
 * @param[in] s3 A message string, or NULL if there is none.
 * @param[in] s4 A message string, or NULL if there is none.
 * @see ngim_aprerror4()
 */
extern void __noreturn ngim_die_aprerror4(apr_status_t status, const char *s1,
		const char *s2, const char *s3, const char *s4);

/**
 * Error level macros
 */

/** debugging */
#define VERBOSE	0
/** informational */
#define INFO 	1
/** warning */
#define WARNING	2
/** fatal error */
#define FATAL	3

#define NGIM_ENV_ERROR_LEVEL	"NGIM_ERROR_LEVEL"

/**
 * Error reporting macros
 */

/** reports an error with six message parts */
#define error6(l, s1, s2, s3, s4, s5, s6) \
	ngim_error6(l, s1, s2, s3, s4, s5, s6)
/** reports an error with five message parts */
#define error5(l, s1, s2, s3, s4, s5) \
	error6(l, s1, s2, s3, s4, s5, NULL)
/** reports an error with four message parts */
#define error4(l, s1, s2, s3, s4) \
	error5(l, s1, s2, s3, s4, NULL)
/** reports an error with three message parts */
#define error3(l, s1, s2, s3) \
	error4(l, s1, s2, s3, NULL)
/** reports an error with two message parts */
#define error2(l, s1, s2) \
	error3(l, s1, s2, NULL)
/** reports an error with one message part */
#define error1(l, s1) \
	error2(l, s1, NULL)

/** reports a fatal error with six message parts and exits */
#define die_error6(s1, s2, s3, s4, s5, s6) \
	ngim_die_error6(s1, s2, s3, s4, s5, s6)
/** reports a fatal error with five message parts and exits */
#define die_error5(s1, s2, s3, s4, s5) \
	die_error6(s1, s2, s3, s4, s5, NULL)
/** reports a fatal error with four message parts and exits */
#define die_error4(s1, s2, s3, s4) \
	die_error5(s1, s2, s3, s4, NULL)
/** reports a fatal error with three message parts and exits */
#define die_error3(s1, s2, s3) \
	die_error4(s1, s2, s3, NULL)
/** reports a fatal error with two message parts and exits */
#define die_error2(s1, s2) \
	die_error3(s1, s2, NULL)
/** reports a fatal error with one message part and exits */
#define die_error1(s1) \
	die_error2(s1, NULL)

/** reports a warning with six message parts */
#define warn_error6(s1, s2, s3, s4, s5, s6) \
	error6(WARNING, s1, s2, s3, s4, s5, s6)
/** reports a warning with five message parts */
#define warn_error5(s1, s2, s3, s4, s5) \
	warn_error6(s1, s2, s3, s4, s5, NULL)
/** reports a warning with four message parts */
#define warn_error4(s1, s2, s3, s4) \
	warn_error5(s1, s2, s3, s4, NULL)
/** reports a warning with three message parts */
#define warn_error3(s1, s2, s3) \
	warn_error4(s1, s2, s3, NULL)
/** reports a warning with two message parts */
#define warn_error2(s1, s2) \
	warn_error3(s1, s2, NULL)
/** reports a warning with one message part */
#define warn_error1(s1) \
	warn_error2(s1, NULL)

/**
 * Memory allocation error macros
 */

/** allocation error message */
#define ERROR_MEMALLOC_S1 "memory allocation failed"

/** reports a fatal allocation error with five message parts and exits */
#define die_allocerror5(s1, s2, s3, s4, s5) \
	die_error6(__FILE__ ":" APR_STRINGIFY(__LINE__) ": " ERROR_MEMALLOC_S1, \
		s1, s2, s3, s4, s5)
/** reports a fatal allocation error with four message parts and exits */
#define die_allocerror4(s1, s2, s3, s4) \
	die_allocerror5(s1, s2, s3, s4, NULL)
/** reports a fatal allocation error with three message parts and exits */
#define die_allocerror3(s1, s2, s3) \
	die_allocerror4(s1, s2, s3, NULL)
/** reports a fatal allocation error with two message parts and exits */
#define die_allocerror2(s1, s2) \
	die_allocerror3(s1, s2, NULL)
/** reports a fatal allocation error with one message part and exits */
#define die_allocerror1(s1) \
	die_allocerror2(s1, NULL)
/** reports a fatal allocation error and exits */
#define die_allocerror0() \
	die_allocerror1(NULL)

/** warns about an allocation error with five message parts */
#define warn_allocerror5(s1, s2, s3, s4, s5) \
	error6(WARNING, __FILE__ ":" APR_STRINGIFY(__LINE__) ": " \
		ERROR_MEMALLOC_S1, s1, s2, s3, s4, s5)
/** warns about an allocation error with four message parts */
#define warn_allocerror4(s1, s2, s3, s4) \
	warn_allocerror5(s1, s2, s3, s4, NULL)
/** warns about an allocation error with three message parts */
#define warn_allocerror3(s1, s2, s3) \
	warn_allocerror4(s1, s2, s3, NULL)
/** warns about an allocation error with two message parts */
#define warn_allocerror2(s1, s2) \
	warn_allocerror3(s1, s2, NULL)
/** warns about an allocation error with one message part */
#define warn_allocerror1(s1) \
	warn_allocerror2(s1, NULL)
/** warns about an allocation error */
#define warn_allocerror0() \
	warn_allocerror1(NULL)

/** assigns the value of alloc to p and tests if its non-NULL */
#define ALLOC_FAIL(p, alloc) \
	unlikely(((p) = (alloc)) == NULL)

/**
 * APR error macros
 */

/** reports an APR error with four message parts */
#define aprerror4(l, status, s1, s2, s3, s4) \
	ngim_aprerror4(l, status, s1, s2, s3, s4)
/** reports an APR error with three message parts */
#define aprerror3(l, status, s1, s2, s3) \
	aprerror4(l, status, s1, s2, s3, NULL)
/** reports an APR error with two message parts */
#define aprerror2(l, status, s1, s2) \
	aprerror3(l, status, s1, s2, NULL)
/** reports an APR error with one message part */
#define aprerror1(l, status, s1) \
	aprerror2(l, status, s1, NULL)
/** reports an APR error */
#define aprerror0(l, status) \
	aprerror1(l, status, NULL)

/** reports an APR error with four message parts and exits */
#define die_aprerror4(status, s1, s2, s3, s4) \
	ngim_die_aprerror4(status, s1, s2, s3, s4)
/** reports an APR error with three message parts and exits */
#define die_aprerror3(status, s1, s2, s3) \
	die_aprerror4(status, s1, s2, s3, NULL)
/** reports an APR error with two message parts and exits */
#define die_aprerror2(status, s1, s2) \
	die_aprerror3(status, s1, s2, NULL)
/** reports an APR error with one message part and exits */
#define die_aprerror1(status, s1) \
	die_aprerror2(status, s1, NULL)
/** reports an APR error and exits */
#define die_aprerror0(status) \
	die_aprerror1(status, NULL)

/** reports an APR warning with four message parts */
#define warn_aprerror4(status, s1, s2, s3, s4) \
	aprerror4(WARNING, status, s1, s2, s3, s4)
/** reports an APR warning with three message parts */
#define warn_aprerror3(status, s1, s2, s3) \
	warn_aprerror4(status, s1, s2, s3, NULL)
/** reports an APR warning with two message parts */
#define warn_aprerror2(status, s1, s2) \
	warn_aprerror3(status, s1, s2, NULL)
/** reports an APR warning with one message part */
#define warn_aprerror1(status, s1) \
	warn_aprerror2(status, s1, NULL)
/** reports an APR warning */
#define warn_aprerror0(status) \
	warn_aprerror1(status, NULL)

/** assigns the value of function to var the tests for a failure */
#define APR_FAIL(var, function) \
	unlikely(((var) = (function)) != APR_SUCCESS)
/** tests for a failure in function */
#define APR_FAIL_N(function) \
	unlikely((function) != APR_SUCCESS)

/**
 * System error macros
 */

/** reports a system error with four message parts and exits */
#define die_syserror4(s1, s2, s3, s4) \
	die_aprerror4(apr_get_os_error(), s1, s2, s3, s4)
/** reports a system error with three message parts and exits */
#define die_syserror3(s1, s2, s3) \
	die_syserror4(s1, s2, s3, NULL)
/** reports a system error with two message parts and exits */
#define die_syserror2(s1, s2) \
	die_syserror3(s1, s2, NULL)
/** reports a system error with one message part and exits */
#define die_syserror1(s1) \
	die_syserror2(s1, NULL)
/** reports a system error and exits */
#define die_syserror0() \
	die_syserror1(NULL)

/** reports a system error with four message parts */
#define warn_syserror4(s1, s2, s3, s4) \
	warn_aprerror4(apr_get_os_error(), s1, s2, s3, s4)
/** reports a system error with three message parts */
#define warn_syserror3(s1, s2, s3) \
	warn_syserror4(s1, s2, s3, NULL)
/** reports a system error with two message parts */
#define warn_syserror2(s1, s2) \
	warn_syserror3(s1, s2, NULL)
/** reports a system error with one message part */
#define warn_syserror1(s1) \
	warn_syserror2(s1, NULL)
/** reports a system error */
#define warn_syserror0() \
	warn_syserror1(NULL)

/**
 * Assertion macros
 */

#ifndef NGIM_NDEBUG
/** Reports an error if condition is not true */
#define warn_assert(x) \
	if (unlikely(!(x))) {\
		warn_error2(__FILE__, ":" APR_STRINGIFY(__LINE__) \
			   ": assertion `" #x "' failed"); \
	}
/** Reports and error and terminates the program if condition is not true */
#define die_assert(x) \
	if (unlikely(!(x))) { \
		die_error2(__FILE__, ":" APR_STRINGIFY(__LINE__) \
				   ": assertion `" #x "' failed"); \
	}
#else /* NGIM_NDEBUG */
#define warn_assert(x)
#define die_assert(x)
#endif

/** @} */

/**
 * @defgroup base-tai TAI time support
 * @{
 */

/*
 * Types and definitions
 */

/** TAI64 label */
typedef struct ngim_tai {
	/**
	 * A particular second of real time.
	 * If 0 <= x < 2^62, x refers to the TAI second beginning 2^62 - x
	 * seconds before the beginning of 1970 TAI. If 2^62 <= x < 2^63,
	 * x refers to the TAI second beginning x - 2^62 seconds after the
	 * beginning of 1970 TAI.
	 */
	apr_uint64_t x;
} ngim_tai_t;

/** TAI64N label */
typedef struct ngim_tain {
	/** TAI64 label */
	ngim_tai_t sec;
	/** 0...999999999 */
	apr_uint32_t nano;
} ngim_tain_t;

/**
 * TAI64 label of the APR epoch.
 * Assuming apr_time_now returns the number of TAI seconds since
 * 00:00:10 1970-01-01 TAI.
 */
#define NGIM_TAI_APR_EPOCH	APR_TIME_C(4611686018427387914) /* 2^62 + 10 */

/** External binary TAI64 format size in bytes */
#define NGIM_TAI_PACK		8
/** External textual TAI64 format size in bytes without a terminating NUL */
#define NGIM_TAI_FORMAT		(2 * NGIM_TAI_PACK + 1)
/** External binary TAI64N format size in bytes */
#define NGIM_TAIN_PACK		12
/** External textual TAI64N format size in bytes without a terminating NUL */
#define NGIM_TAIN_FORMAT	(2 * NGIM_TAIN_PACK + 1)

/*
 * Functions
 */

/**
 * Converts a TAI64 label to apr_time_t.
 * @param[in] t The label to convert.
 * @return The value for apr_time_t, as microseconds from epoch.
 * @see #NGIM_TAI_APR_EPOCH
 */
extern apr_time_t ngim_tai_to_apr(const ngim_tai_t *t);

/**
 * Converts an apr_time_t value to a TAI64 label.
 * @param[out] t Result.
 * @param[in] a The value to convert.
 * @see #NGIM_TAI_APR_EPOCH
 */
extern void ngim_tai_from_apr(ngim_tai_t *t, apr_time_t a);

/**
 * Sets a TAI64 label to an approximate of the current TAI time.
 * @param[out] t Result.
 * @remarks Equivalent to calling ngim_tai_from_apr using the result of
 *          apr_time_now as a parameter.
 * @see #NGIM_TAI_APR_EPOCH
 */
extern void ngim_tai_now(ngim_tai_t *t);

/**
 * Compares the values of two TAI64 labels.
 * @param[in] t A label to compare.
 * @param[in] u A label to compare.
 * @return Non-zero if label t is smaller than label u.
 */
extern int ngim_tai_less(const ngim_tai_t *t, const ngim_tai_t *u);

/**
 * Converts a TAI64 label to the external TAI64 format.
 * @param[out] s Pointer to an array of at least #NGIM_TAI_PACK bytes.
 * @param[in] t A label.
 * @see #NGIM_TAI_PACK
 */
extern void ngim_tai_pack(unsigned char *s, const ngim_tai_t *t);

/**
 * Converts a TAI64 label from the external TAI64 format.
 * @param[in] s Pointer to an array of at least #NGIM_TAI_PACK bytes.
 * @param[out] t A label.
 * @return Non-zero if the result is a valid TAI64 label.
 * @see #NGIM_TAI_PACK
 */
extern int __must_check ngim_tai_unpack(const unsigned char *s, ngim_tai_t *t);

/**
 * Converts a TAI64 label to the external TAI64 ASCII format.
 * @param[out] s Pointer to an array of at least #NGIM_TAI_FORMAT bytes.
 * @param[in] t A label.
 * @remarks Does not null-terminate s.
 * @see #NGIM_TAI_FORMAT
 */
extern void ngim_tai_format(char *s, const ngim_tai_t *t);

/**
 * Converts a TAI64 label from the external TAI64 ASCII format.
 * @param[out] s Pointer to an array of at least #NGIM_TAI_FORMAT bytes.
 * @param[in] t A label.
 * @remarks Non-zero if the result is a valid TAI64 label.
 * @see #NGIM_TAI_FORMAT
 */
extern int __must_check ngim_tai_unformat(const char *s, ngim_tai_t *t);

/**
 * Converts a TAI64N label to apr_time_t.
 * @param[in] t The label to convert.
 * @return The value for apr_time_t, as microseconds from epoch.
 * @see #NGIM_TAI_APR_EPOCH
 */
extern apr_time_t ngim_tain_to_apr(const ngim_tain_t *t);

/**
 * Converts an apr_time_t value to a TAI64 label.
 * @param[out] t Result.
 * @param[in] a The value to convert.
 * @see #NGIM_TAI_APR_EPOCH
 */
extern void ngim_tain_from_apr(ngim_tain_t *t, apr_time_t a);

/**
 * Converts a TAI64N label to a TAI64 label.
 * @param[in] ta Result.
 * @param[out] t The label to convert.
 * @see #NGIM_TAI_APR_EPOCH
 */
extern void ngim_tain_to_tai(const ngim_tain_t *ta, ngim_tai_t *t);

/**
 * Sets a TAI64N label to an approximate of the current TAI time.
 * @param[out] t Result.
 * @remarks Equivalent to calling ngim_tain_from_apr using the result of
 *          apr_time_now as a parameter.
 * @see #NGIM_TAI_APR_EPOCH
 */
extern void ngim_tain_now(ngim_tain_t *t);

/**
 * Compares the values of two TAI64N labels.
 * @param[in] t A label to compare.
 * @param[in] u A label to compare.
 * @return Non-zero if label t is smaller than label u.
 */
extern int ngim_tain_less(const ngim_tain_t *t, const ngim_tain_t *u);

/**
 * Calculates the approximate difference between two TAI64N labels.
 * @param[in] t A label to compare.
 * @param[in] u A label to compare.
 * @param[out] d Receives the difference in seconds.
 * @return Non-zero if label t is smaller than label u.
 * @remarks If label t is larger than label u, the time difference is
 *          not calculated and the return value is zero.
 */
extern int ngim_tain_diff(const ngim_tain_t *t, const ngim_tain_t *u,
		apr_uint64_t *d);

/**
 * Converts a TAI64N label to the external TAI64N format.
 * @param s Pointer to an array of at least #NGIM_TAIN_PACK bytes.
 * @param t A label.
 * @see #NGIM_TAIN_PACK
 */
extern void ngim_tain_pack(unsigned char *s, const ngim_tain_t *t);

/**
 * Converts a TAI64N label from the external TAI64N format.
 * @param[in] s Pointer to an array of at least #NGIM_TAIN_PACK bytes.
 * @param[out] t A label.
 * @return Non-zero if the result is a valid TAI64N label.
 * @see #NGIM_TAIN_PACK
 */
extern int __must_check ngim_tain_unpack(const unsigned char *s,
		ngim_tain_t *t);

/**
 * Converts a TAI64N label to the external TAI64 ASCII format.
 * @param[out] s Pointer to an array of at least #NGIM_TAIN_FORMAT bytes.
 * @param[in] t A label.
 * @remarks Does not null-terminate s.
 * @see #NGIM_TAIN_FORMAT
 */
extern void ngim_tain_format(char *s, const ngim_tain_t *t);

/**
 * Converts a TAI64N label from the external TAI64 ASCII format.
 * @param[out] s Pointer to an array of at least #NGIM_TAIN_FORMAT bytes.
 * @param[in] t A label.
 * @remarks Non-zero if the result is a valid TAI64N label.
 * @see #NGIM_TAIN_FORMAT
 */
extern int __must_check ngim_tain_unformat(const char *s, ngim_tain_t *t);

/** @} */

/**
 * @defgroup base-iso8601 ISO 8601 support
 * @{
 */

/*
 * Definitions
 */

/**
 * The maximum length for an ISO 8601 formatted date and time, including the
 * terminating NUL. The resulting format is YYYYY-MM-DD hh:mm:ss.uuuuuu+OOOO,
 * where the year may be only four digits, microseconds may be omitted, and
 * the offset from UTC may be replaced with Z if zero or presented with only
 * two characters.
 */
#define NGIM_ISO8601_FORMAT		33

/*
 * Functions
 */

/**
 * Converts apr_time_t to an ISO 8601:2004 string in the UTC time zone.
 * @param[out] s Pointer to an array of at least #NGIM_ISO8601_FORMAT bytes.
 * @param[in] t The time to convert.
 * @remarks Always NUL-terminates s.
 * @see #NGIM_ISO8601_FORMAT
 */
extern void ngim_iso8601_utc_format(char *s, apr_time_t t);

/**
 * Converts apr_time_t to an ISO 8601:2004 string in the local time zone.
 * @param[out] s Pointer to an array of at least #NGIM_ISO8601_FORMAT bytes.
 * @param[in] t The time to convert.
 * @remarks Always NUL-terminates s.
 * @see #NGIM_ISO8601_FORMAT
 */
extern void ngim_iso8601_local_format(char *s, apr_time_t t);

/** @} */

/**
 * @defgroup base-cmdline Command line processing
 * @{
 */

/*
 * Types
 */

/** Command line parameters */
typedef struct cmdline_params {
	/** A command line parameter, including '--' or '-' at the start */
	const char *name;
	/** Unique command bitmask */
	apr_uint32_t cmd;
	/** Pointer to a variable receiving the argument, or NULL if the
	 * parameter has no argument */
	const char **arg;
} ngim_cmdline_params_t;

/** Command line arguments */
typedef struct cmdline_args {
	/** Pointer to a variable receiving the argument */
	const char **arg;
} ngim_cmdline_args_t;

/*
 * Functions
 */

/**
 * Parses the command line.
 * @param[in] argc Number of arguments in argv.
 * @param[in] argv A list of command line arguments.
 * @param[in] noextra Return an error if there are unused arguments left in argv
 *                    after processing.
 * @param[in] params A NULL-terminated list of command line parameters.
 * @param[in] args A NULL-terminated list of command line arguments expected
 *                 after the parameters, or can be NULL.
 * @param[out] selected
 * @return A value <0 if there were errors. If noextra is zero, a return value
 *         >0 is the index of the first unused argument in argv.
 * @remarks Supports only as many commands as there are bits in selected.
 */
extern int __must_check ngim_cmdline_parse(int argc, const char * const *argv,
		int noextra, ngim_cmdline_params_t *params, ngim_cmdline_args_t *args,
		apr_uint32_t *selected);

/** @} */

/**
 * @defgroup base-misc Miscellaneous
 * @{
 */

/**
 * Creates a directory.
 * @param[in] name The name of the directory to create.
 * @param[in] perms Permisssions for the new directory.
 * @param[in] setperms If the directory already exists, should we set its
 * 					   permission.
 * @param[in] pool A pool to use for temporary storage.
 * @return If the directory was created or already exists (with proper
 * 		   permissions), returns 0, otherwise a value <0.
 */
extern int __must_check ngim_create_directory(const char *name,
		apr_fileperms_t perms, int setperms, apr_pool_t *pool);

/**
 * Creates a pollset for polling input from one file.
 * @param[out] pset Receives the pointer to the new pollset.
 * @param[in] file The file to poll for input.
 * @param[in] pool A pool for the pollset.
 * @return If successful, returns 0, otherwise a value <0.
 * @remarks Adds a child cleanup for the created pollset to prevent possible
 *          file descriptors to be inherited by children.
 */
extern int __must_check ngim_create_pollset_file_in(apr_pollset_t **pset,
		apr_file_t *file, apr_pool_t *pool);

/**
 * Creates a symbolic link.
 * @param[in] target Target of the new symbolic link.
 * @param[in] path Path for the symbolic link.
 * @return If successful, returns 0, otherwise a value <0.
 */
extern int __must_check ngim_create_symlink(const char *target,
		const char *path);

/**
 * Resolves a basename for a symbolic link.
 * @param[in] path Path to the symbolic link to resolve.
 * @param[out] target Receives the resolved basename for the link.
 * @param[in] pool Memory pool from which target is allocated.
 * @return If successful, returns 0, otherwise a value <0.
 */
extern int __must_check ngim_resolve_symlink_basename(const char *path,
		char **target, apr_pool_t *pool);

/**
 * Tries to converts apr_exit_why_e to an explanation message.
 * @param[in] reason Value apr_exit_why_e usually returned by apr_proc_wait.
 * @return A string explaining the reason for process exit.
 */
extern const char * ngim_strexitwhy(apr_exit_why_e reason);

/** @} */

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ! NGIM_BASE_H */
