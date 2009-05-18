
#include "gcomm/logger.hpp"
#include "gcomm/util.hpp"
#include <cstdlib>

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
    }
    return *Logger::logger;
}

END_GCOMM_NAMESPACE
