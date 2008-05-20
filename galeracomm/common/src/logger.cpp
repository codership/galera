#include "gcomm/logger.hpp"

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
    }
    return *Logger::logger;
}
