
#include "gcomm/timer.hpp"
#include "gcomm/logger.hpp"
#include "gcomm/pseudofd.hpp"

#include <cassert>

BEGIN_GCOMM_NAMESPACE

Timer::Timer(EventLoop *p) : 
    fd(-1),
    event_loop(p),
    timer_map(),
    expiration_map()
{
    fd = PseudoFd::alloc_fd();
    event_loop->insert(fd, this);
}

Timer::~Timer()
{
    event_loop->erase(fd);
    PseudoFd::release_fd(fd);
    expiration_map.clear();
    timer_map.clear();
}

typedef std::pair<const char *, ExpirationMap::iterator> TimerPair;
typedef std::pair<const Time, TimerHandler*> ExpirationPair;

void Timer::set(TimerHandler* h, const Period& p)
{
    
    Time expiry = Time::now() + p;
    ExpirationMap::iterator eret =
	expiration_map.insert(ExpirationPair(expiry, h));
    
    std::pair<TimerMap::iterator, bool> tret = 
	timer_map.insert(TimerPair(h->get_name(), eret));
    if (tret.second == false)
	throw FatalException("Timer already set");
    event_loop->queue_event(fd, Event(Event::E_USER, Time::now() + p));
}

void Timer::unset(const TimerHandler* h)
{
    TimerMap::iterator i = timer_map.find(h->get_name());
    if (i != timer_map.end()) {
	expiration_map.erase(i->second);
	timer_map.erase(i);
    } else {
	LOG_WARN(std::string("Timer ") + h->get_name() + " not set");
    }
}

bool Timer::is_set(const TimerHandler* h)
{
    return timer_map.find(h->get_name()) != timer_map.end();
}

void Timer::handle_event(int fd, const Event& e)
{
    assert(fd == this->fd);
    Time now = Time::now();
    ExpirationMap::iterator i = expiration_map.begin();
    while (i != expiration_map.end() && i->first <= now)
    {
        TimerHandler* th = i->second;
        unset(i->second);
        th->handle();
        i = expiration_map.begin();
    }
}

END_GCOMM_NAMESPACE
