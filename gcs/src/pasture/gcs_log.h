// Copyright (C) 2007 Codership Oy <info@codership.com>

    Logging is a question here. This is a library
    which is supposed to be used by a 3-rd party
    applications and should not print anything on
    its own. However for testing and debugging
    this could be useful.
*/

#ifndef _gcs_log_h_
#define _gcs_log_h_

/*
 * Severity classes:
 * FATAL - is a fatal malfunction of the library which cannot be recovered
 *         from. Application must close.
 * error - error condition in the library which prevents further normal
 *         operation but can be recovered from by the application. E.g. EAGAIN.
 * warn  - some abnormal condition which library can recover from by itself.
 *
 * info  - just a log message.
 *
 * debug - debugging message.
 */

typedef enum gcs_severity
{
    GCS_LOG_FATAL,
    GCS_LOG_ERROR,
    GCS_LOG_WARNING,
    GCS_LOG_INFO,
    GCS_LOG_DEBUG
}
gcs_severity_t;

/* creates a string to be logged */
char *gcs_log_string (const char *file, const char *function, int line,
		      const char *format, ...);

/* default log function, can be overriden in config */
void gcs_log (gcs_severity_t severity, char *string);

#define gcs_fatal(format, ...)\
        gcs_log(GCS_LOG_FATAL,\
                gcs_log_string(__FILE__, __PRETTY_FUNCTION__, __LINE__,\
                               format, ## __VA_ARGS__, NULL))

#define gcs_error(format, ...)\
        gcs_log(GCS_LOG_ERROR,\
	        gcs_log_string(__FILE__, __PRETTY_FUNCTION__, __LINE__,\
	                       format, ## __VA_ARGS__, NULL))

#define gcs_warn(format, ...)\
        gcs_log(GCS_LOG_WARNING,\
                gcs_log_string(__FILE__, __PRETTY_FUNCTION__, __LINE__,\
                               format, ## __VA_ARGS__, NULL))

#define gcs_info(format, ...)\
        gcs_log(GCS_LOG_INFO,\
	        gcs_log_string(__FILE__, __PRETTY_FUNCTION__, __LINE__,\
		               format, ## __VA_ARGS__, NULL))

#define gcs_debug(format, ...)\
        gcs_log(GCS_LOG_DEBUG,\
                gcs_log_string(__FILE__, __PRETTY_FUNCTION__, __LINE__,\
		               format, ## __VA_ARGS__, NULL))

#endif /* _gcs_log_h_ */
