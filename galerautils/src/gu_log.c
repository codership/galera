// Copyright (C) 2007 Codership Oy <info@codership.com>

/**
 * @file Logging functions definitions
 *
 * $Id$
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "galerautils.h"

/* Global configurable variables */
static FILE* gu_log_file        = NULL;
static int   gu_log_self_tstamp = 0;
int          gu_log_debug       = 0;

int
gu_conf_set_log_file (FILE *file)
{
    gu_debug ("Log file changed by application");
    gu_log_file = file;
    return 0;
}

int
gu_conf_self_tstamp_on ()
{
    gu_debug ("Turning self timestamping on");
    gu_log_self_tstamp = 1;
    return 0;
}

int
gu_conf_self_tstamp_off ()
{
    gu_debug ("Turning self timestamping off");
    gu_log_self_tstamp = 0;
    return 0;
}

int
gu_conf_debug_on ()
{
    gu_log_debug = 1;
    gu_debug ("Turning debug on");
    return 0;
}

int
gu_conf_debug_off ()
{
    gu_debug ("Turning debug off");
    gu_log_debug = 0;
    return 0;
}

/** Returns current timestamp in the provided buffer */
static inline int
gu_log_tstamp (char* const tstamp, int const len)
{
    int            ret = 0;
    struct tm      date;
    struct timeval time;

    gettimeofday (&time, NULL);
    localtime_r  (&time.tv_sec, &date);

    /* 23 symbols */
    ret = snprintf (tstamp, len, "%04d-%02d-%02d %02d:%02d:%02d.%03d ",
		    date.tm_year + 1900, date.tm_mon + 1, date.tm_mday,
		    date.tm_hour, date.tm_min, date.tm_sec,
		    (int)time.tv_usec / 1000);
    return ret;
}

/**
 * @function
 * Default logging function: simply writes to stderr or gu_log_file if set.
 */
void
gu_log_default (int severity, const char* string)
{
    FILE *log_file   = stderr;
    char *sev        = NULL;

    switch (severity) {
    case GU_LOG_FATAL: sev = "FATAL"  ; break;
    case GU_LOG_ERROR: sev = "ERROR"  ; break;
    case GU_LOG_WARN:  sev = "WARN "  ; break;
    case GU_LOG_INFO:  sev = "INFO "  ; break;
    case GU_LOG_DEBUG: sev = "DEBUG"  ; break;
    default:           sev = "UNKNOWN";
    }

    if (gu_log_file) log_file = gu_log_file;

    fprintf (log_file, "%s: %s\n", sev, string);
    fflush (log_file);

    return;
}

/**
 * Log function handle.
 * Can be changed by application through gu_conf_set_log_callback()
 */
gu_log_cb_t gu_log_handle = gu_log_default;

int
gu_conf_set_log_callback (gu_log_cb_t callback)
{
    if (callback) {
	gu_debug ("Logging function changed by application");
        gu_log_handle = callback;
    } else {
	gu_debug ("Logging function restored to default");
        gu_log_handle = gu_log_default;
    }
    return 0;
}

int
gu_log (gu_log_severity_t severity,
	const char*       file,
	const char*       function,
	const int         line,
	const char*       format, ...)
{
    va_list ap;
    int   max_string = 2048;
    char  string[max_string]; /** @note: this can cause stack overflow
                               * in kernel mode (both Linux and Windows). */
    char* str = string;
    int   len;

// Modified gu_debug() macro to handle this instead of calling gu_log().
// This way this condition is checked only in gu_debug() and if fails,
// this function is not called.
//    if (GU_LOG_DEBUG == severity && !gu_log_debug) return 0;

    if (gu_log_self_tstamp) {
	len = gu_log_tstamp (str, max_string);
	str += len;
	max_string -= len;
    }

    if (gu_likely(max_string > 0)) {
	len = snprintf (str, max_string, "%s:%s():%d: ",
			file, function, line);
	str += len;
	max_string -= len;
	if (gu_likely(max_string > 0 && format)) {
	    va_start (ap, format);
	    vsnprintf (str, max_string, format, ap);
	    va_end (ap);
	}
    }

    /* actual logging */
    gu_log_handle (severity, string);

    return 0;
}



