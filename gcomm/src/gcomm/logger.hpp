#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <gcomm/common.hpp>
#include <gcomm/types.hpp>
#include <gcomm/time.hpp>
#include <gcomm/monitor.hpp>
#include <gcomm/util.hpp>
#include <gcomm/string.hpp>

#include <ostream>
#include <string>
#include <sstream>


BEGIN_GCOMM_NAMESPACE

class Logger_s;
class Logger {
    friend class Logger_s;
public:
    enum Level {
	Trace,
	Debug,
	Info,
	Warning,
	Error,
	Fatal,
	None
    };
private:
    std::ostream& os;
    Level level;
    static Logger *logger;
    Monitor mon;
    Logger() : os(std::cerr), level(Info) {}
    Logger(std::ostream& s) : os(s), level(Info) {}
    Logger(std::ostream& s, Level l) : os(s), level(l) {}
    Logger(Level l) : os(std::cerr), level(l) {}
    ~Logger() {}
    
public:

    Level get_level() const {
	return level;
    }
    static Logger& instance();
    static Logger& create(std::ostream&, Level l = Info);
    
    void print(const string& prefix, const string& s);
    
    void trace(const string& s) {
	if (level <= Trace)
	    print("TRACE", s);
    }

    void debug(const string& pf, const string& s);

    void info(const string& s) {
	if (level <= Info)
	    print("INFO", s);
    }

    void warning(const string& s) {
	if (level <= Warning)
	    print("WARN", s);
    }

    void error(const string& s) {
	if (level <= Error)
	    print("ERROR", s);
    }

    void fatal(const string& s) {
	if (level <= Fatal)
	    print("FATAL", s);
    }
    

};

END_GCOMM_NAMESPACE

#define TRACE_PREFIX string(__FILE__) + ":" + __FUNCTION__ + ":" + Int(__LINE__).to_string() + ": "
#define DEBUG_PREFIX string(__FUNCTION__) + ":" + Int(__LINE__).to_string()

#define LOG_TRACE(_a) do {				     \
	if (Logger::instance().get_level() <= Logger::Trace) \
	    Logger::instance().trace(TRACE_PREFIX + _a); \
    } while (0)

#define LOG_DEBUG(_a) do {                                              \
	if (Logger::instance().get_level() <= Logger::Debug)            \
	    Logger::instance().debug(DEBUG_PREFIX, _a);                  \
    } while (0)

#define LOG_INFO(_a) do {				     \
	if (Logger::instance().get_level() <= Logger::Info)  \
	    Logger::instance().info(_a);		     \
    } while (0)

#define LOG_WARN(_a) do {				     \
	if (Logger::instance().get_level() <= Logger::Warning) \
	    Logger::instance().warning(_a);		     \
    } while (0)

#define LOG_ERROR(_a) do {				     \
	if (Logger::instance().get_level() <= Logger::Error) \
	    Logger::instance().error(_a);		     \
    } while (0)


#define LOG_FATAL(_a) do {				     \
	if (Logger::instance().get_level() <= Logger::Fatal) \
	    Logger::instance().fatal(_a);		     \
    } while (0)




#endif // !LOGGER_HPP
