/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */


#include "gu_epoll.hpp"
#include "gu_network.hpp"
#include "gu_logger.hpp"

#include <stdexcept>

#include <sys/epoll.h>
#include <cerrno>
#include <cstring>

/*
 * Mapping between NetworkEvent and EPoll events
 *
 * TODO: This mapping should be done elsewhere...
 */

static inline int to_epoll_mask(const int mask)
{
    int ret = 0;
    ret |= (mask & gu::net::NetworkEvent::E_IN ? EPOLLIN : 0);
    ret |= (mask & gu::net::NetworkEvent::E_OUT ? EPOLLOUT : 0);
    return ret;
}

static inline int to_network_event_mask(const int mask)
{
    int ret = 0;
    if (mask & ~(EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP))
    {
        log_warn << "event mask " << mask << " has unrecognized bits set";
    }

    ret |= (mask & EPOLLIN ? gu::net::NetworkEvent::E_IN : 0);
    ret |= (mask & EPOLLOUT ? gu::net::NetworkEvent::E_OUT : 0);
    ret |= (mask & EPOLLERR ? gu::net::NetworkEvent::E_ERROR : 0);
    ret |= (mask & EPOLLHUP ? gu::net::NetworkEvent::E_ERROR : 0);
    return ret;
}



void gu::net::EPoll::resize(const size_t to_size)
{
    void* tmp = realloc(events, to_size*sizeof(struct epoll_event));
    if (to_size > 0 && tmp == 0)
    {
        log_fatal << "failed to allocate: " << to_size*sizeof(struct epoll_event);
        throw std::bad_alloc();
    }
    events = reinterpret_cast<struct epoll_event*>(tmp);
    events_size = to_size;
    n_events = 0;
}

gu::net::EPoll::EPoll() :
    e_fd(-1),
    events(0),
    events_size(0),
    n_events(0),
    current(events)
{
    if ((e_fd = epoll_create(16)) == -1)
    {
        throw std::runtime_error("could not create epoll");
    }
}
    
gu::net::EPoll::~EPoll()
{
    int err = closefd(e_fd);
    if (err != 0)
    {
        log_warn << "Error closing epoll socket: " << err;
    }
    free(events);
}
    


void gu::net::EPoll::insert(const EPollEvent& epe)
{
    int op = EPOLL_CTL_ADD;
    struct epoll_event ev = {
        to_epoll_mask(epe.get_events()), 
        {epe.get_user_data()}
    };
    int err = epoll_ctl(e_fd, op, epe.get_fd(), &ev);
    if (err != 0)
    {
        err = errno;
        log_error << "epoll_ctl(" << e_fd << "," << op << "): " 
                  << strerror(err);
        throw std::runtime_error("");
    }
    resize(events_size + 1);
    
}

void gu::net::EPoll::erase(const EPollEvent& epe)
{
    int op = EPOLL_CTL_DEL;
    struct epoll_event ev = {0, {0}};
    int err = epoll_ctl(e_fd, op, epe.get_fd(), &ev);
    if (err != 0)
    {
        log_debug << "epoll erase: " << err;
    }
    resize(events_size - 1);
}

void gu::net::EPoll::modify(const EPollEvent& epe)
{
    int op = EPOLL_CTL_MOD;
    struct epoll_event ev = {
        to_epoll_mask(epe.get_events()), 
        {epe.get_user_data()}
    };
    int err = epoll_ctl(e_fd, op, epe.get_fd(), &ev);
    
    if (err != 0)
    {
        err = errno;
        log_error << "epoll_ctl(" << op << "," << epe.get_fd() << "): " << err 
                  << " '" << strerror(err) << "'";
        throw std::runtime_error("");
    }
}

void gu::net::EPoll::poll(const int timeout)
{
    int ret = epoll_wait(e_fd, events, events_size, timeout);
    if (ret == -1)
    {
        ret = errno;
        log_error << "epoll_wait(): " << ret;
        n_events = 0;
    }
    else
    {
        n_events = ret;
        current = events;
    }
}
    
void gu::net::EPoll::pop_front()
{
    if (n_events == 0)
    {
        throw std::logic_error("no events available");
    }
    --n_events;
    ++current;
}

bool gu::net::EPoll::empty() const
{
    return n_events == 0;
}

gu::net::EPollEvent gu::net::EPoll::front() const
{
    if (n_events == 0)
    {
        throw std::logic_error("no events available");
    }
    return EPollEvent(-1, to_network_event_mask(current->events), current->data.ptr);
}
