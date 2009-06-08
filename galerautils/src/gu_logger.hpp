/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * This code is based on an excellent article at Dr.Dobb's:
 * http://www.ddj.com/cpp/201804215?pgno=1
 */

#ifndef __GU_LOGGER__
#define __GU_LOGGER__

#include <sstream>

extern "C" {
#include "gu_log.h"
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
    public:
        Logger() {};
        virtual inline ~Logger();

        // this function returns a stream for further logging.
//        std::ostringstream& get(TLogLevel level = logINFO);
        inline std::ostringstream& get(const LogLevel lvl,
                                       const char*    file,
                                       const char*    func,
                                       const int      line);
    public:
#ifndef _gu_log_h_
        static void        enable_tstamp (bool);
        static void        enable_debug  (bool);
        static void        set_logger    (LogCallback);
#endif
    protected:
        std::ostringstream os;
    private:
        Logger(const Logger&);
        Logger& operator =(const Logger&);
    private:
        void               prepare_default ();
        LogLevel           level;
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
    public:
        static inline bool no_log          (LogLevel lvl)
        { return ((int)lvl > (int)max_level); };
    };

    Logger::~Logger()
    {
        os << std::endl;
        logger (level, os.str().c_str());
    }

    std::ostringstream&
    Logger::get(const LogLevel lvl,
                const char*    file,
                const char*    func,
                const int      line)
    {
        // fancy original stuff
        // os << std::string(level > logDEBUG ? 0 : level - logDEBUG, '\t');
        level = lvl;
        if (logger == default_logger) {
            // put in timestamp and log level line
            prepare_default();
        }
        return os;
    }

#define LOG(level)               \
    if (Logger::no_log(level)) ; \
    else Logger().get(level, __FILE__, __PRETTY_FUNCTION__, __LINE__)

// USAGE: LOG(level) << item_1 << item_2 << ... << item_n;

#define log_fatal LOG(LOG_FATAL)
#define log_error LOG(LOG_ERROR)
#define log_warn  LOG(LOG_WARN)
#define log_info  LOG(LOG_INFO)
#define log_debug LOG(LOG_DEBUG)

}

#endif // __GU_LOGGER__
