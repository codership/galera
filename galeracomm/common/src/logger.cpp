#include "galeracomm/logger.hpp"
#include <cstdlib>

class Logger_s
{
    Logger_s (const Logger_s&);
    void operator= (const Logger_s&);

public:

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
	int l = Logger::Info;
	if (::getenv("LOGGER_LEVEL")) {
	    std::istringstream is(::getenv("LOGGER_LEVEL"));
	    is >> l;
	}
	if (l <= Logger::Warning)
	    std::cerr << "Logger init: (stderr," << l << ")\n";
	Logger::logger = new Logger(std::cerr, Logger::Level(l));
	logger_s.set(Logger::logger);
    }
    return *Logger::logger;
}
