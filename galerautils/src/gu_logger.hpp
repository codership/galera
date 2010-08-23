/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * This code is based on an excellent article at Dr.Dobb's:
 * http://www.ddj.com/cpp/201804215?pgno=1
 *
 * It looks ugly because it has to integrate with C logger -
 * in order to produce identical output
 */

#ifndef __GU_LOGGER__
#define __GU_LOGGER__

#include <sstream>

extern "C" {
#include "gu_log.h"
#include "gu_conf.h"
}

namespace gu
{
    // some portability stuff
#ifdef _gu_log_h_
    enum LogLevel { LOG_FATAL = GU_LOG_FATAL,
                    LOG_ERROR = GU_LOG_ERROR,
                    LOG_WARN  = GU_LOG_WARN,
                    LOG_INFO  = GU_LOG_INFO,
                    LOG_DEBUG = GU_LOG_DEBUG,
                    LOG_MAX };
    typedef gu_log_cb_t LogCallback;
#else
    enum LogLevel { LOG_FATAL,
                    LOG_ERROR,
                    LOG_WARN,
                    LOG_INFO,
                    LOG_DEBUG,
                    LOG_MAX };
    typedef void (*LogCallback) (int, const char*);
#endif

    class Logger
    {
    private:

        Logger(const Logger&);
        Logger& operator =(const Logger&);

        void               prepare_default ();
        const LogLevel     level;

#ifndef _gu_log_h_
        static LogLevel    max_level;
        static bool        do_timestamp;
        static LogCallback logger;
        static void        default_logger  (int, const char*);
#else
#define max_level          gu_log_max_level
#define logger             gu_log_cb
#define default_logger     gu_log_cb_default
#endif

    protected:

        std::ostringstream os;

    public:

        Logger(LogLevel _level = LOG_INFO) :
            level  (_level),
            os     ()
        {}

        virtual ~Logger() { logger (level, os.str().c_str()); }

        std::ostringstream& get(const char* file,
                                const char* func,
                                int         line)
        {
            if (default_logger == logger)
            {
                prepare_default();       // prefix with timestamp and log level
            }

            /* provide file:func():line info only when debug logging is on */
            if (static_cast<int>(LOG_DEBUG) == static_cast<int>(max_level))
            {
                os << file << ':' << func << "():" << line << ": ";
            }

            return os;
        }

        static bool no_log (LogLevel lvl)
        {
            return (static_cast<int>(lvl) > static_cast<int>(max_level));
        }

        static void set_debug_filter(const std::string&);

        static bool no_debug(const std::string&, const std::string&, const int);

#ifndef _gu_log_h_
        static void enable_tstamp (bool);
        static void enable_debug  (bool);
        static void set_logger    (LogCallback);
#endif
    };


#define GU_LOG_CPP(level)                                               \
    if (gu::Logger::no_log(level) ||                                    \
        (level == gu::LOG_DEBUG &&                                      \
         gu::Logger::no_debug(__FILE__, __FUNCTION__, __LINE__))) {}    \
    else gu::Logger(level).get(__FILE__, __FUNCTION__, __LINE__)

// USAGE: LOG(level) << item_1 << item_2 << ... << item_n;

#define log_fatal GU_LOG_CPP(gu::LOG_FATAL)
#define log_error GU_LOG_CPP(gu::LOG_ERROR)
#define log_warn  GU_LOG_CPP(gu::LOG_WARN)
#define log_info  GU_LOG_CPP(gu::LOG_INFO)
#define log_debug GU_LOG_CPP(gu::LOG_DEBUG)

}

#endif // __GU_LOGGER__
