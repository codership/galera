/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * This code is based on an excellent article at Dr.Dobb's:
 * http://www.ddj.com/cpp/201804215?pgno=1
 */

#ifndef __GCACHE_LOGGER__
#define __GCACHE_LOGGER__

#include <sstream>
//#include <wsrep.h>

namespace gcache
{
    // some portability stuff
#ifdef WSREP_H
    enum LogLevel { LOG_FATAL = WSREP_LOG_FATAL,
                    LOG_ERROR = WSREP_LOG_ERROR,
                    LOG_WARN  = WSREP_LOG_WARN,
                    LOG_INFO  = WSREP_LOG_INFO,
                    LOG_DEBUG = WSREP_LOG_DEBUG,
                    LOG_MAX };
    typedef wsrep_log_cb_t LogCallback;
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
        static void        enable_tstamp (bool);
        static void        enable_debug  (bool);
        static void        set_logger    (LogCallback);
        static inline bool no_log        (LogLevel lvl)
                                         { return (lvl > max_level); };
    protected:
        std::ostringstream os;
    private:
        Logger(const Logger&);
        Logger& operator =(const Logger&);
    private:
        static LogLevel     max_level;
        static bool         do_timestamp;
        static LogCallback  logger;
        static void         default_logger  (int, const char*);
        void                prepare_default ();
        LogLevel            level;
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
}

#define LOG(level)               \
    if (Logger::no_log(level)) ; \
    else Logger().get(level, __FILE__, __PRETTY_FUNCTION__, __LINE__)

#define log_fatal LOG(LOG_FATAL)
#define log_error LOG(LOG_ERROR)
#define log_warn  LOG(LOG_WARN)
#define log_info  LOG(LOG_INFO)
#define log_debug LOG(LOG_DEBUG)

#endif // __GCACHE_LOGGER__
