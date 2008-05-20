#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <gcomm/time.hpp>
#include <gcomm/monitor.hpp>

#include <ostream>
#include <string>
#include <sstream>

inline std::string to_string(const uint64_t u)
{
    std::ostringstream os;
    os << u;
    return os.str();
}

inline std::string to_string(const uint32_t u)
{
    std::ostringstream os;
    os << u;
    return os.str();
}

inline std::string to_string(const uint16_t u)
{
    std::ostringstream os;
    os << u;
    return os.str();    
}

inline std::string to_string(const int64_t u)
{
    std::ostringstream os;
    os << u;
    return os.str();
}


inline std::string to_string(const int32_t u)
{
    std::ostringstream os;
    os << u;
    return os.str();
}


inline std::string to_string(const int16_t u)
{
    std::ostringstream os;
    os << u;
    return os.str();
}

inline std::string to_string(const double u)
{
    std::ostringstream os;
    os << u;
    return os.str();
}


class Logger {
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

    
    static Logger& instance();
    static Logger& create(std::ostream&, Level l = Info);
    
    void print(const char *prefix, const std::string s) {
	Critical crit(&mon);
	timeval tv;
	::gettimeofday(&tv, 0);
	Time time(tv.tv_sec, tv.tv_usec);
	os.width(16);
	os << time.to_string();
	os.width(0);
	os << " [";
	os.width(5);
	os << prefix;
	os.width(0);
	os << "]: " << s << "\n";
    }
    
    void trace(const std::string s) {
	if (level <= Trace)
	    print("TRACE", s);
    }

    void debug(const std::string s) {
	if (level <= Debug)
	    print("DEBUG", s);
    }

    void info(const std::string s) {
	if (level <= Info)
	    print("INFO", s);
    }

    void warning(const std::string s) {
	if (level <= Warning)
	    print("WARN", s);
    }

    void error(const std::string s) {
	if (level <= Error)
	    print("ERROR", s);
    }

    void fatal(const std::string s) {
	if (level <= Fatal)
	    print("FATAL", s);
    }
    

};

#endif // !LOGGER_HPP
