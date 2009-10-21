/**
 * Default poll implementation 
 */

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <poll.h>
#include <string>
#include <map>
#include <iostream>

#include "gcomm/event.hpp"
#include "gcomm/logger.hpp"
#include "gcomm/util.hpp"

using namespace std;
using namespace std::rel_ops;
using namespace gcomm;


static inline int map_event_to_mask(const int e)
{
    int ret = ((e & Event::E_IN)  ? POLLIN  : 0);
    ret    |= ((e & Event::E_OUT) ? POLLOUT : 0);
    return ret;
}

static inline int map_mask_to_event(const int m)
{
    int ret = Event::E_NONE;
    ret |= (m & POLLIN)   ? Event::E_IN    : Event::E_NONE;
    ret |= (m & POLLOUT)  ? Event::E_OUT   : Event::E_NONE;
    ret |= (m & POLLERR)  ? Event::E_ERR   : Event::E_NONE;
    ret |= (m & POLLHUP)  ? Event::E_HUP   : Event::E_NONE;
    ret |= (m & POLLNVAL) ? Event::E_INVAL : Event::E_NONE;
    return ret;
}

static std::string event_to_string (int e)
{
    if (Event::E_NONE == e) return "E_NONE";

    std::string ret;

    ret.reserve(32); // should be enough for most situations

    if (e & Event::E_IN)
    {                                                        ret += "E_IN";    }
    if (e & Event::E_OUT)
    {                        if (!ret.empty()) {ret += '|';} ret += "E_OUT";   }
    if (e & Event::E_ERR)
    {                        if (!ret.empty()) {ret += '|';} ret += "E_ERR";   }
    if (e & Event::E_HUP)
    {                        if (!ret.empty()) {ret += '|';} ret += "E_HUP";   }
    if (e & Event::E_INVAL)
    {                        if (!ret.empty()) {ret += '|';} ret += "E_INVAL"; }
    if (e & Event::E_TIMED)
    {                        if (!ret.empty()) {ret += '|';} ret += "E_TIMED"; }
    if (e & Event::E_SIGNAL)
    {                        if (!ret.empty()) {ret += '|';} ret += "E_SIGNAL";}
    if (e & Event::E_USER)
    {                        if (!ret.empty()) {ret += '|';} ret += "E_USER";  }

    return ret;
}


void EventLoop::insert(const int fd, EventContext *pctx)
{
    if (fd == -1) gcomm_throw_fatal << "invalid fd -1";

    if (ctx_map.insert(std::make_pair(fd, pctx)).second == false)
	gcomm_throw_fatal << "Insert";
}

void EventLoop::erase(const int fd)
{
    if (fd >= 0)
    {
        unset(fd, Event::E_ALL);
    }
    
    EventMap::iterator i, i_next;
    for (i = event_map.begin(); i != event_map.end(); i = i_next)
    {
        i_next = i, ++i_next;
        if (i->second.first == fd && i != active_event)
        {
            event_map.erase(i);
        }
    }

    CtxMap::iterator ci = ctx_map.find(fd);
    
    if (ci == ctx_map.end()) gcomm_throw_fatal << "Unknown fd";
    
    log_debug << "fd: " << fd << " " << ci->second;
    
    ctx_map.erase(ci);
}

class PollFdCmpOp
{
public:
    PollFdCmpOp(const int fd_) : fd(fd_) { }
    bool operator()(const pollfd& cmp)
    {
        return (fd == cmp.fd);
    } 
private:
    int const fd;
};

void EventLoop::set(const int fd, const int e)
{
    vector<pollfd>::iterator pfd;

    if (fd < 0) gcomm_throw_fatal << "Negative fd: " << fd;
    
    log_debug << "Setting event(s) " << event_to_string(e) << " on fd " << fd;

    if ((pfd = find_if(pfds.begin(), pfds.end(), PollFdCmpOp(fd))) == pfds.end())
    {
        pollfd ins;
        ins.fd = fd;
        ins.events = static_cast<short int>(e);
        ins.revents = 0;
        pfds.push_back(ins);
        pfds_changed = true;
    }
    else
    {
        short int mask = static_cast<short int>(
            pfd->events | static_cast<short int>(map_event_to_mask(e)));
        pfd->events = mask;
    }
}

void EventLoop::unset(const int fd, const int e)
{
    if (fd < 0) gcomm_throw_fatal << "negative fd";
    assert (e != Event::E_NONE);
    
    vector<pollfd>::iterator pfd = find_if(pfds.begin(), pfds.end(), PollFdCmpOp(fd));
    if (pfd != pfds.end())
    {
        short int mask = static_cast<short int>(
            pfd->events & ~static_cast<short int>(map_event_to_mask(e)));
        log_debug << "Clearing event(s) " << event_to_string(e) 
                  << " from fd "<<fd; 
        pfd->events = mask;
        if (pfd->events == 0)
        {
            unset_fds++;
        }
    }
    else
    {
        log_warn << "descriptor " << fd << " not found from poll set";
    }
}

void EventLoop::set_signal(const int fd, const int signo)
{
    SigMap::iterator i = sig_map.find(fd);
    if (i == sig_map.end()) {
        SigSet sigs;
        sigs.insert(signo);
        std::pair<SigMap::iterator, bool> ret = sig_map.insert(std::pair<const int, SigSet>(fd, sigs));
        assert(ret.second == true);
    } else {
        std::pair<SigSet::iterator, bool> ret = i->second.insert(signo);
        if (ret.second == false) {
            log_warn << "signal " << signo << " already exists in " 
                     << fd << " sig set";
        }
    }
}

void EventLoop::unset_signal(const int fd, const int signo)
{
    SigMap::iterator i = sig_map.find(fd);
    if (i == sig_map.end()) {
        log_warn << fd << " not found from sig map";
        
    } else {
        SigSet::iterator si = i->second.find(signo);
        if (si == i->second.end()) {
            log_warn << "signal " << signo
                     << " not found from fd " << fd << " sig set";
        }
        i->second.erase(signo);
    }
}

void EventLoop::queue_event(const int fd, const Event& e)
{
    event_map.insert(std::make_pair(e.get_time(), std::make_pair(fd, e)));
}

void EventLoop::handle_queued_events()
{
    Time now = Time::now();
    // Deliver queued events
    int cnt = 0;
    for (EventMap::iterator i = event_map.begin(); 
         i != event_map.end() && i->first <= now && cnt++ < 10 &&
             interrupted == false; 
         i = event_map.begin()) {
        CtxMap::iterator map_i = ctx_map.find(i->second.first);
        if (map_i == ctx_map.end()) {
            log_warn << "ctx " << i->second.first
                     << " associated to event not found from context map";
        } else {
            active_event = i;
            map_i->second->handle_event(i->second.first, i->second.second);
        }
        event_map.erase(i);
    }
    active_event = event_map.end();
}

/*
 * Compute timeout to the next event
 */
int EventLoop::compute_timeout(const int max_val)
{
    EventMap::const_iterator i = event_map.begin();
    if (i == event_map.end())
        return max_val;
    
    
    Time next = i->second.second.get_time();
    Time now = Time::now();
    
    if (next < now)
    {
        log_debug << "return 0";
        return 0;
    }
    int diff = static_cast<int>(next.get_utc() - now.get_utc())/gu::datetime::MSec;
    
    assert(diff >= 0);
    
    int retval = max_val > 0 ? std::min<int>(diff, max_val) : diff;
    return retval;
}



class ZeroMaskCmpOp
{
public:
    ZeroMaskCmpOp() { }
    
    bool operator()(const pollfd& pfd) const
    {
        return (pfd.events == 0);
    }
};

int EventLoop::poll(const int timeout)
{
    interrupted = false;
    handle_queued_events();
    
    if (interrupted) return -1;

    int p_ret = ::poll(&pfds[0], pfds.size(), compute_timeout(timeout));
    int err   = errno;

    if (p_ret == -1 && err == EINTR)
    {
        p_ret = 0;
    }
    else if (p_ret == -1 && err != EINTR)
    {
        gcomm_throw_fatal;
    }
    else
    {
        p_ret = 0;
        pfds_changed = false;
        for (vector<pollfd>::iterator i = pfds.begin(); i != pfds.end(); ++i)
        {
            if (i->revents != 0)
            {
                EventLoop::CtxMap::iterator ctx_i = ctx_map.find(i->fd);
                if (ctx_i != ctx_map.end())
                {
                    ctx_i->second->handle_event(i->fd, 
                                                map_mask_to_event(i->revents));
                    ++p_ret;
                }
                if (pfds_changed == true)
                {
                    break;
                }
            }
        }
    }   
    if (unset_fds > 0)
    {
        vector<pollfd>::iterator i;
        while ((i = find_if(pfds.begin(), pfds.end(), ZeroMaskCmpOp())) 
               != pfds.end())
        {
            pfds.erase(i);
        }
        unset_fds = 0;
    }
    
    handle_queued_events();
    
    // Garbage collection
    for_each(released.begin(), released.end(), DeleteObjectOp());
    released.clear();
    if (interrupted)
        return -1;
    return p_ret;
}

void EventLoop::release_protolay(Protolay* p)
{
    p->release();
    released.push_back(p);
}

EventLoop::EventLoop() :
    ctx_map(),
    sig_map(),
    event_map(),
    active_event(event_map.end()),
    pfds_changed(false),
    pfds(0),
    released(),
    unset_fds(0),
    interrupted(false)

{
}


EventLoop::~EventLoop()
{
    event_map.clear();
    ctx_map.clear();
    pfds.clear();
}


