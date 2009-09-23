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

BEGIN_GCOMM_NAMESPACE

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

static struct pollfd *pfd_find(struct pollfd *pfds, const size_t n_pfds, 
			       const int fd)
{
    for (size_t i = 0; i < n_pfds; i++) {
	if (pfds[i].fd == fd)
	    return &pfds[i];
    }
    return 0;
}

void EventLoop::insert(const int fd, EventContext *pctx)
{
    if (fd == -1) gcomm_throw_fatal << "invalid fd -1";

    if (ctx_map.insert(std::make_pair(fd, pctx)).second == false)
	gcomm_throw_fatal << "Insert";
}

void EventLoop::erase(const int fd)
{
    unset(fd, Event::E_ALL);


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

void EventLoop::set(const int fd, const int e)
{
    struct pollfd *pfd = 0;

    if (fd < 0) gcomm_throw_fatal << "Negative fd: " << fd;
    // was LOG_WARN("negative fd");

    log_debug << "Setting event(s) " << event_to_string(e) << " on fd " << fd;

    if ((pfd = pfd_find(pfds, n_pfds, fd)) == 0)
    {
	void* tmp = realloc(pfds, (n_pfds + 1) * sizeof (struct pollfd));

        if (tmp == 0 && n_pfds > 0) gcomm_throw_runtime (ENOMEM);

        pfds = reinterpret_cast<struct pollfd *>(tmp);
	pfd  = &pfds[n_pfds];

	pfd->fd      = fd;
	pfd->events  = 0;
	pfd->revents = 0;

	++n_pfds;
    }

    pfd->events |= map_event_to_mask(e);
}

void EventLoop::unset(const int fd, const int e)
{
    assert (e != Event::E_NONE);

    struct pollfd *pfd = 0;

    log_debug << "Clearing event(s) " << event_to_string(e) << " from fd "<<fd; 

    if ((pfd = pfd_find(pfds, n_pfds, fd)) != 0)
    {
	pfd->events &= ~map_event_to_mask(e);

	if (pfd->events == 0)
        {
	    memmove(&pfd[0], &pfd[1],
                    (n_pfds - (pfd - pfds)) * sizeof(struct pollfd));

	    void* tmp = realloc(pfds, n_pfds*sizeof(struct pollfd));

            if (tmp == 0 && n_pfds > 0) gcomm_throw_runtime(ENOMEM);

            pfds = reinterpret_cast<struct pollfd *>(tmp);
	    --n_pfds;
	}
    }

    for (size_t i = 0; i < n_pfds; ++i)
    {
        assert(pfds[i].fd != fd || pfds[i].events != 0);
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
            LOG_WARN("signal " + make_int(signo).to_string() 
                     + " already exists in " 
                     + make_int(fd).to_string() + " sig set");
        }
    }
}

void EventLoop::unset_signal(const int fd, const int signo)
{
    SigMap::iterator i = sig_map.find(fd);
    if (i == sig_map.end()) {
        LOG_WARN("fd " + make_int(fd).to_string() + " not found from sig map");
        
    } else {
        SigSet::iterator si = i->second.find(signo);
        if (si == i->second.end()) {
            LOG_WARN("signal " + make_int(signo).to_string() 
                     + " not found from fd " + make_int(fd).to_string() 
                     + " sig set");
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
            LOG_WARN("ctx " + make_int(i->second.first).to_string() + 
                     " associated to event not found from context map");
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
        LOG_DEBUG("return 0");
        return 0;
    }
    int diff = next.get_milliseconds() - now.get_milliseconds();
    
    assert(diff >= 0);
    
    int retval = max_val > 0 ? std::min<int>(diff, max_val) : diff;
    return retval;
}


int EventLoop::poll(const int timeout)
{
    int p_cnt = 0;
    interrupted = false;

    handle_queued_events();

    if (interrupted) return -1;

    int p_ret = ::poll(pfds, n_pfds, compute_timeout(timeout));
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
        for (size_t i = 0; i < n_pfds; )
        {
            size_t last_n_pfds = n_pfds;
            int e = map_mask_to_event(pfds[i].revents);

            if (e != Event::E_NONE)
            {
                CtxMap::iterator map_i;

                if ((map_i = ctx_map.find(pfds[i].fd)) != ctx_map.end())
                {
                    if (map_i->second == 0) gcomm_throw_fatal;

                    LOG_TRACE("handling "
                              + gu::to_string (pfds[i].fd) + " "
                              + Pointer(map_i->second).to_string() + " "
                              + gu::to_string (pfds[i].revents));

                    pfds[i].revents = 0;

                    gu_trace(map_i->second->handle_event(pfds[i].fd, Event(e)));

                    p_cnt++;

                    if (interrupted) goto out;
                }
                else 
                {
                    gcomm_throw_fatal << "No ctx for fd found";
                }
            }
            else
            {
                if (pfds[i].revents)
                {
                    gcomm_throw_fatal << "Unhandled poll events";
                }
            }

            if (last_n_pfds != n_pfds)
            {
                /* pfds has changed, lookup for first nonzero revents from 
                 * beginning */
                for (size_t j = 0; j < n_pfds; ++j)
                {
                    if (pfds[j].revents != 0)
                    {
                        i = j;
                        break;
                    }
                }
            }
            else
            {
                ++i;
            }
        }
    }
    
    // assert(p_ret == p_cnt);
    if (p_ret != p_cnt) {
        LOG_WARN("p_ret ("
                 + make_int(p_ret).to_string() 
                 + ") != p_cnt (" 
                 + make_int(p_cnt).to_string() 
                 + ")");
    }
    
    handle_queued_events();
    
    // Garbage collection
    for (std::list<Protolay*>::iterator i = released.begin(); i != released.end(); ++i)
    {
        delete *i;
    }
    released.clear();
out:
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
    n_pfds(0),
    pfds(0),
    released(),
    interrupted(false)
{
}


EventLoop::~EventLoop()
{
    event_map.clear();
    ctx_map.clear();
    free(pfds);
}

END_GCOMM_NAMESPACE
