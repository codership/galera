// Copyright (C) 2007 Codership Oy <info@codership.com>

/**
 * @file Logging API
 */

#ifndef _gu_log_h_
#define _gu_log_h_

/**
 * @typedef
 * Defines severity classes for log messages:
 * FATAL - is a fatal malfunction of the library which cannot be recovered
 *         from. Application must close.
 * error - error condition in the library which prevents further normal
 *         operation but can be recovered from by the application. E.g. EAGAIN.
 * warn  - some abnormal condition which library can recover from by itself.
 *
 * info  - just an informative log message.
 *
 * debug - debugging message.
 */

typedef enum gu_log_severity
{
    GU_LOG_FATAL,
    GU_LOG_ERROR,
    GU_LOG_WARN,
    GU_LOG_INFO,
    GU_LOG_DEBUG
}
gu_log_severity_t;

/**
 * @typedef
 * Defines a type of callback function that application can provide
 * to do the logging
 */
typedef void (*gu_log_cb_t) (int severity, const char* msg);

/** Helper for macros defined below. Should not be called directly. */
int gu_log (gu_log_severity_t severity,
	    const char*       file,
	    const char*       function,
	    const int         line,
	    const char*       format, ...);

/** This variable is made global only for the purpose of using it in
 *  gu_debug() macro and avoid calling gu_log() when debug is off.
 *  Don't use it directly! */
extern int gu_log_debug;

/**
 * @name Logging macros.
 * Must be implemented as macros to report the location of the code where
 * they are called.
 */
/*@{*/
#define gu_fatal(format, ...)\
        gu_log(GU_LOG_FATAL, __FILE__, __PRETTY_FUNCTION__, __LINE__,\
               format, ## __VA_ARGS__, NULL)

#define gu_error(format, ...)\
        gu_log(GU_LOG_ERROR, __FILE__, __PRETTY_FUNCTION__, __LINE__,\
	       format, ## __VA_ARGS__, NULL)

#define gu_warn(format, ...)\
        gu_log(GU_LOG_WARN, __FILE__, __PRETTY_FUNCTION__, __LINE__,\
               format, ## __VA_ARGS__, NULL)

#define gu_info(format, ...)\
        gu_log(GU_LOG_INFO, __FILE__, __PRETTY_FUNCTION__, __LINE__,\
	       format, ## __VA_ARGS__, NULL)

#define gu_debug(format, ...)\
        (!gu_log_debug ? 0 : \
        gu_log(GU_LOG_DEBUG, __FILE__, __PRETTY_FUNCTION__, __LINE__,\
	       format, ## __VA_ARGS__, NULL))
/*@}*/

#endif /* _gu_log_h_ */
