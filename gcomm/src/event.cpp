/**
 * Default poll implementation 
 */

#include "gcomm/event.hpp"
#include "gcomm/logger.hpp"
#include "gcomm/util.hpp"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <poll.h>
#include <map>
#include <iostream>

BEGIN_GCOMM_NAMESPACE

static inline int map_event_to_mask(const int e)
{
    int ret = (e & Event::E_IN) ? POLLIN : 0;
    ret |= ((e & Event::E_OUT) ? POLLOUT : 0);
    return ret;
}

static inline int map_mask_to_event(const int m)
{
    int ret = Event::E_NONE;
    ret |= (m & POLLIN) ? Event::E_IN : Event::E_NONE;
    ret |= (m & POLLOUT) ? Event::E_OUT : Event::E_NONE;
    ret |= (m & POLLERR) ? Event::E_ERR : Event::E_NONE;
    ret |= (m & POLLHUP) ? Event::E_HUP : Event::E_NONE;
    ret |= (m & POLLNVAL) ? Event::E_INVAL : Event::E_NONE;
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
    if (fd == -1)
        throw FatalException("invalid fd -1");
    if (ctx_map.insert(std::make_pair(fd, pctx)).second == false)
	throw FatalException("Insert");
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
    if (ci == ctx_map.end())
        throw FatalException("");

    LOG_DEBUG("erase " + Int(fd).to_string() + " " 
              + Pointer(ci->second).to_string());    
    ctx_map.erase(ci);
}

void EventLoop::set(const int fd, const int e)
{
    struct pollfd *pfd = 0;

    if (fd < 0)
    {
        LOG_WARN("negative fd");
    }
    LOG_DEBUG("set " + Int(fd).to_string());

    if ((pfd = pfd_find(pfds, n_pfds, fd)) == 0) {
	++n_pfds;
	struct pollfd* tmp = reinterpret_cast<struct pollfd *>(realloc(pfds, n_pfds*sizeof(struct pollfd)));
        if (tmp == 0 && n_pfds > 0) {
            throw FatalException("out of memory");
        }
        pfds = tmp;
	pfd = &pfds[n_pfds - 1];
	pfd->fd = fd;
	pfd->events = 0;
	pfd->revents = 0;
    }
    pfd->events |= map_event_to_mask(e);
}

void EventLoop::unset(const int fd, const int e)
{
    struct pollfd *pfd = 0;
    LOG_DEBUG("unset " + Int(fd).to_string() 
              + " n_pfds " + Size(n_pfds).to_string());

    if ((pfd = pfd_find(pfds, n_pfds, fd)) != 0) {
	pfd->events &= ~map_event_to_mask(e);
	if (pfd->events == 0) {
	    --n_pfds;
	    memmove(&pfd[0], &pfd[1], (n_pfds - (pfd - pfds))*sizeof(struct pollfd));
	    struct pollfd* tmp = reinterpret_cast<struct pollfd *>(realloc(pfds, n_pfds*sizeof(struct pollfd)));
            if (tmp == 0 && n_pfds > 0) {
                throw FatalException("out of memory");
            }
            pfds = tmp;
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
            LOG_WARN("signal " + Int(signo).to_string() 
                     + " already exists in " 
                     + Int(fd).to_string() + " sig set");
        }
    }
}

void EventLoop::unset_signal(const int fd, const int signo)
{
    SigMap::iterator i = sig_map.find(fd);
    if (i == sig_map.end()) {
        LOG_WARN("fd " + Int(fd).to_string() + " not found from sig map");
        
    } else {
        SigSet::iterator si = i->second.find(signo);
        if (si == i->second.end()) {
            LOG_WARN("signal " + Int(signo).to_string() 
                     + " not found from fd " + Int(fd).to_string() 
                     + " sig set");
        }
        i->second.erase(signo);
    }
}

void EventLoop::queue_event(const int fd, const Event& e)
{
    event_map.insert(
        std::pair<const Time, std::pair<const int, Event> > (
            e.get_time(), std::pair<const int, Event>(fd, e)));
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
            LOG_WARN("ctx " + Int(i->second.first).to_string() + 
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
    int p_ret;
    int err = 0;
    int p_cnt = 0;
    interrupted = false;

    handle_queued_events();
    if (interrupted)
        goto out;



    p_ret = ::poll(pfds, n_pfds, compute_timeout(timeout));
    err = errno;

    if (p_ret == -1 && err == EINTR) {
        p_ret = 0;
    } else if (p_ret == -1 && err != EINTR) {
        throw FatalException("");
    } else {
        for (size_t i = 0; i < n_pfds; ) {
            size_t last_n_pfds = n_pfds;
            int e = map_mask_to_event(pfds[i].revents);
            if (e != Event::E_NONE) {

                CtxMap::iterator map_i;
                if ((map_i = ctx_map.find(pfds[i].fd)) != ctx_map.end()) {
                    if (map_i->second == 0)
                        throw FatalException("");
                    LOG_TRACE("handling "
                              + Int(pfds[i].fd).to_string() + " "
                              + Pointer(map_i->second).to_string() + " "
                              + Int(pfds[i].revents).to_string());
                    pfds[i].revents = 0;
                    map_i->second->handle_event(pfds[i].fd, Event(e));
                    p_cnt++;
                    if (interrupted)
                        goto out;
                } else {
                    throw FatalException("No ctx for fd found");
                }
            } else {
                if (pfds[i].revents) {
                    LOG_ERROR("Unhandled poll events");
                    throw FatalException("");
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
                 + Int(p_ret).to_string() 
                 + ") != p_cnt (" 
                 + Int(p_cnt).to_string() 
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
