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
#include <stdbool.h>
#include "gu_log.h"
#include "gu_macros.h"

/* Global configurable variables */
static FILE*      gu_log_file        = NULL;
bool              gu_log_self_tstamp = false;
gu_log_severity_t gu_log_max_level   = GU_LOG_INFO;

int
gu_conf_set_log_file (FILE *file)
{
    gu_debug ("Log file changed by application");
    if (file) {
        gu_log_file = file;
    }
    else {
        gu_log_file = stderr;
    }

    return 0;
}

int
gu_conf_self_tstamp_on ()
{
    gu_debug ("Turning self timestamping on");
    gu_log_self_tstamp = true;
    return 0;
}

int
gu_conf_self_tstamp_off ()
{
    gu_debug ("Turning self timestamping off");
    gu_log_self_tstamp = false;
    return 0;
}

int
gu_conf_debug_on ()
{
    gu_log_max_level = GU_LOG_DEBUG;
    gu_debug ("Turning debug on");
    return 0;
}

int
gu_conf_debug_off ()
{
    gu_debug ("Turning debug off");
    gu_log_max_level = GU_LOG_INFO;
    return 0;
}

/** Returns current timestamp in the provided buffer */
static inline int
log_tstamp (char* tstamp, size_t const len)
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

const char* gu_log_level_str[GU_LOG_DEBUG + 2] = 
{
    "FATAL: ",
    "ERROR: ",
    " WARN: ",
    " INFO: ",
    "DEBUG: ",
    "XXXXX: "
};

/**
 * @function
 * Default logging function: simply writes to stderr or gu_log_file if set.
 */
void
gu_log_cb_default (int severity, const char* msg)
{
    fputs  (msg, gu_log_file ? gu_log_file : stderr);
    fflush (gu_log_file);
}

/**
 * Log function handle.
 * Can be changed by application through gu_conf_set_log_callback()
 */
gu_log_cb_t gu_log_cb = gu_log_cb_default;

int
gu_conf_set_log_callback (gu_log_cb_t callback)
{
    if (callback) {
	gu_debug ("Logging function changed by application");
        gu_log_cb = callback;
    } else {
	gu_debug ("Logging function restored to default");
        gu_log_cb = gu_log_cb_default;
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

    if (gu_log_self_tstamp) {
	len = log_tstamp (str, max_string);
	str += len;
	max_string -= len;
    }

    if (gu_likely(max_string > 0)) {
        const char* log_level_str =
            gu_log_cb_default == gu_log_cb ? gu_log_level_str[severity] : "";

	len = snprintf (str, max_string, "%s%s:%s():%d: ",
			log_level_str, file, function, line);
	str += len;
	max_string -= len;
	if (gu_likely(max_string > 0 && format)) {
	    va_start (ap, format);
	    vsnprintf (str, max_string, format, ap);
	    va_end (ap);
	}
    }

    /* actual logging */
    gu_log_cb (severity, string);

    return 0;
}



