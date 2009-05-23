
#include "gcomm/logger.hpp"
#include "gcomm/util.hpp"
#include "gcomm/string.hpp"

#include <set>
#include <cstdlib>

using std::set;

BEGIN_GCOMM_NAMESPACE

struct Logger_s
{
    Logger *logger;
    Logger_s() : logger(0) {}
    ~Logger_s() {
	delete logger;
    }
    void set(Logger *l) {
	logger = l;
    }
};

static Logger_s logger_s;

Logger *Logger::logger = 0;

static set<string> debug_filter;

Logger& Logger::instance()
{
    if (Logger::logger == 0) {
	int l = Logger::Warning;
	if (::getenv("LOGGER_LEVEL"))
        {
            l = read_int(::getenv("LOGGER_LEVEL"));
	}
        
	Logger::logger = new Logger(std::cerr, Logger::Level(l));
	logger_s.set(Logger::logger);
        LOG_INFO("Logger init: (stderr," + Int(l).to_string() + ")");
        if (::getenv("LOGGER_DEBUG_FILTER"))
        {
            vector<string> dvec = strsplit(::getenv("LOGGER_DEBUG_FILTER"), ',');
            for (vector<string>::const_iterator i = dvec.begin();
                 i != dvec.end(); ++i)
            {
                debug_filter.insert(*i);
                LOG_INFO("adding debug filter: " + *i);
            }
        }
    }
    return *Logger::logger;
}

void Logger::print(const string& prefix, const string& s)
{
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
    os << "]: " << s << std::endl;
}



void Logger::debug(const string& pf, const string& s)
{

    if (debug_filter.size() == 0 || 
        debug_filter.find(pf) != debug_filter.end() ||
        debug_filter.find(pf.substr(0, pf.find_first_of(":"))) != debug_filter.end())
    {
        print("DEBUG", pf + ": " + s);
    }
}

END_GCOMM_NAMESPACE
