// Copyright (C) 2007 Codership Oy <info@codership.com>
 *
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
//#include "gcs.h"
#include "gcs_log.h"

/* Global configurable variables */
FILE* gcs_log_file = NULL;
void (*gcs_log_function) (int severity, char* string) = NULL;
int   gcs_log_self_tstamp = 0;
int   gcs_log_debug = 0;

int gcs_conf_set_log_file (FILE *file)
{
    gcs_debug ("GCS log file changed by application");
    gcs_log_file = file;
    return 0;
}

int gcs_conf_set_log_function (void (*function) (int, char*))
{
    if (function)
	gcs_debug ("GCS logging function changed by application");
    else
	gcs_debug ("GCS logging function restored to default");

    gcs_log_function = function;
    return 0;
}

int gcs_conf_self_tstamp_on ()
{
    gcs_debug ("Turning self timestamping on");
    gcs_log_self_tstamp = 1;
    return 0;
}

int gcs_conf_self_tstamp_off ()
{
    gcs_debug ("Turning self timestamping off");
    gcs_log_self_tstamp = 0;
    return 0;
}

int gcs_conf_debug_on ()
{
    gcs_log_debug = 1;
    gcs_debug ("Turning debug on");
    return 0;
}

int gcs_conf_debug_off ()
{
    gcs_debug ("Turning debug off");
    gcs_log_debug = 0;
    return 0;
}

static inline int gcs_log_tstamp (char* const str, int const len)
{
    int            ret = 0;
    struct tm      date;
    struct timeval time;

    gettimeofday (&time, NULL);
    localtime_r  (&time.tv_sec, &date);

    /* 23 symbols */
    ret = snprintf (str, len, "%04d-%02d-%02d %02d:%02d:%02d.%03d ",
		    date.tm_year + 1900, date.tm_mon + 1, date.tm_mday,
		    date.tm_hour, date.tm_min, date.tm_sec,
		    (int)time.tv_usec / 1000);
    return ret;
}

char* gcs_log_string (const char    *file,
		      const char    *function,
		      const int      line,
		      const char    *format, ...)
{
    va_list ap;
    int   max_string = 2048;
    char  string[max_string];
    char* str = string;
    int   len;

    if (gcs_log_self_tstamp) {
	len = gcs_log_tstamp (str, max_string);
	str += len;
	max_string -= len;
    }

    if (max_string > 0) {
	len = snprintf (str, max_string, "%s: %s: %d: ",
			file, function, line);
	str += len;
	max_string -= len;
	if (max_string > 0 && format) {
	    va_start (ap, format);
	    vsnprintf (str, max_string, format, ap);
	    va_end (ap);
	}
    }

    /* must not forget to free it */
    return (char *) strdup (string);
}

void gcs_log (gcs_severity_t severity, char* string)
{
    FILE *log_file = stderr;
    char *sev      = NULL;
    int   tstamp_len = 128;
    char  tstamp[tstamp_len];

    if (gcs_log_function) {
	gcs_log_function (severity, string);
	return;
    }

    switch (severity) {
    case GCS_LOG_FATAL:   sev = "FATAL" ; break;
    case GCS_LOG_ERROR:   sev = "ERROR" ; break;
    case GCS_LOG_WARNING: sev = "WARN " ; break;
    case GCS_LOG_INFO:    sev = "INFO " ; break;
    case GCS_LOG_DEBUG:   sev = "DEBUG" ; break;
    default:              sev = "UNKNOWN";
    }

    if (gcs_log_file) log_file = gcs_log_file;

    if (!gcs_log_self_tstamp) {
	gcs_log_tstamp (tstamp, tstamp_len);
	fprintf (log_file, "%s", tstamp);
    }

    fprintf (log_file, "%s: %s\n", sev, string);
    fflush (log_file);
    free (string);
}
