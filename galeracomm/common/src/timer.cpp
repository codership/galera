
#include "gcomm/timer.hpp"
#include "gcomm/logger.hpp"



/*
 * TODO: Make Poll handle negative fd values as timer handlers.
 *
 */


Timer::Timer(Poll *p)
{
    
}

Timer::~Timer()
{
    
}

typedef std::pair<const char *, ExpirationMap::iterator> TimerPair;
typedef std::pair<const Time, TimerHandler *> ExpirationPair;

void Timer::set(TimerHandler *h, const Period& p)
{
    
    Time expiry = Time::now() + p;
    ExpirationMap::iterator eret =
	expiration_map.insert(ExpirationPair(expiry, h));
    
    std::pair<TimerMap::iterator, bool> tret = 
	timer_map.insert(TimerPair(h->get_name(), eret));
    if (tret.second == false)
	throw FatalException("Timer already set");
}

void Timer::unset(const TimerHandler *h)
{
    TimerMap::iterator i = timer_map.find(h->get_name());
    if (i != timer_map.end()) {
	expiration_map.erase(i->second);
	timer_map.erase(i);
    } else {
	Logger::instance().debug(std::string("Timer ") + h->get_name() + " not set");
    }
}
