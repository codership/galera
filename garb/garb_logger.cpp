/* Copyright (C) 2011 Codership Oy <info@codership.com> */

#include "garb_logger.hpp"

#include <cstdio>

#include <syslog.h>

namespace garb
{
    void set_logfile (const std::string& fname)
    {
        FILE* log_file = fopen (fname.c_str(), "a");

        if (!log_file)
        {
            gu_throw_error (ENOENT) << "Failed to open '" << fname
                                    << "' for appending";
        }

        gu_conf_set_log_file (log_file);
    }

    static void log_to_syslog (int level, const char* msg)
    {
        int p = LOG_NOTICE;

        switch (level)
        {
        case GU_LOG_FATAL: p = LOG_CRIT;    break;
        case GU_LOG_ERROR: p = LOG_ERR;     break;
        case GU_LOG_WARN:  p = LOG_WARNING; break;
        case GU_LOG_INFO:  p = LOG_INFO;    break;
        case GU_LOG_DEBUG: p = LOG_DEBUG;   break;
        }

        syslog (p | LOG_DAEMON, "%s", msg);
    }

    void set_syslog ()
    {
        openlog ("garbd", LOG_PID, LOG_DAEMON);
        gu_conf_set_log_callback (log_to_syslog);
    }

} /* namespace garb */
