/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * This code is based on an excellent article at Dr.Dobb's:
 * http://www.ddj.com/cpp/201804215?pgno=1
 */

#include <iomanip>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sys/time.h>

#include "gu_logger.hpp"
#include "gu_string_utils.hpp" // strsplit

#include <string>
#include <vector>
#include <set>

using std::string;
using std::vector;
using std::set;

namespace gu
{

    class DebugFilter
    {

        set<string> filter;

    public:
        DebugFilter() :
            filter()
        {
            if (::getenv("LOGGER_DEBUG_FILTER"))
            {
                set_filter(::getenv("LOGGER_DEBUG_FILTER"));
            }
        }

        ~DebugFilter() {}

        void set_filter(const string& str)
        {
            vector<string> dvec = gu::strsplit(str, ',');
            for (vector<string>::const_iterator i = dvec.begin();
                 i != dvec.end(); ++i)
            {
                filter.insert(*i);
            }
        }

        size_t size() const { return filter.size(); }

        bool is_set(const string& str) const
        {
            return filter.find(str) != filter.end() ||
                filter.find(str.substr(0, str.find_first_of(":"))) != filter.end();
        }
    };

    static DebugFilter debug_filter;

    void
    Logger::set_debug_filter(const string& str)
    {
        debug_filter.set_filter(str);
    }

    bool
    Logger::no_debug(const string& file, const string& func, const int line)
    {
        return debug_filter.size() > 0 && debug_filter.is_set(func) == false;
    }

#ifndef _gu_log_h_
    void
    Logger::enable_tstamp (bool yes)
    {
        do_timestamp = yes;
    }

    void
    Logger::enable_debug (bool yes)
    {
        if (yes) {
            max_level = LOG_DEBUG;
        }
        else {
            max_level = LOG_INFO;
        }
    }

    void
    Logger::default_logger (int lvl, const char* msg)
    {
        fputs  (msg, stderr); fputc ('\n', stderr);
        fflush (stderr);
    }

    void
    Logger::set_logger (LogCallback cb)
    {
        if (0 == cb) {
            logger = default_logger;
        }
        else {
            logger = cb;
        }
    }

    static const char* level_str[LOG_MAX] = 
    {
        "FATAL: ",
        "ERROR: ",
        " WARN: ",
        " INFO: ",
        "DEBUG: "
    };

    bool        Logger::do_timestamp = false;
    LogLevel    Logger::max_level    = LOG_INFO;
    LogCallback Logger::logger       = default_logger;
#else
#define do_timestamp       (gu_log_self_tstamp == true)
#define level_str          gu_log_level_str
#endif // _gu_log_h_

    void
    Logger::prepare_default()
    {
        if (do_timestamp) {
            using namespace std;
            struct tm      date;
            struct timeval time;

            gettimeofday (&time, NULL);
            localtime_r  (&time.tv_sec, &date);

            os << date.tm_year + 1900 << '-'
               << setw(2) << setfill('0') << (date.tm_mon + 1) << '-'
               << setw(2) << setfill('0') << date.tm_mday << ' '
               << setw(2) << setfill('0') << date.tm_hour << ':'
               << setw(2) << setfill('0') << date.tm_min  << ':'
               << setw(2) << setfill('0') << date.tm_sec  << '.'
               << setw(3) << setfill('0') << (time.tv_usec / 1000) << ' ';
        }

        os << level_str[level];
    }
}
