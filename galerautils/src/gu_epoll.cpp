
#include "gu_epoll.hpp"
#include "gu_network.hpp"
#include "gu_logger.hpp"

#include <stdexcept>

#include <sys/epoll.h>
#include <cerrno>
#include <cstring>

static inline int to_epoll_mask(const int mask)
{
    int ret = 0;
    ret |= (mask & gu::NetworkEvent::E_IN ? EPOLLIN : 0);
    ret |= (mask & gu::NetworkEvent::E_OUT ? EPOLLOUT : 0);
    return ret;
}

static inline int to_network_event_mask(const int mask)
{
    int ret = 0;
    if (mask & ~(EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP))
    {
        log_warn << "event mask " << mask << " has unrecognized bits set";
    }

    ret |= (mask & EPOLLIN ? gu::NetworkEvent::E_IN : 0);
    ret |= (mask & EPOLLOUT ? gu::NetworkEvent::E_OUT : 0);
    ret |= (mask & EPOLLERR ? gu::NetworkEvent::E_ERROR : 0);
    ret |= (mask & EPOLLHUP ? gu::NetworkEvent::E_ERROR : 0);
    return ret;
}

void gu::EPoll::resize(const size_t to_size)
{
    void* tmp = realloc(events, to_size*sizeof(struct epoll_event));
    if (to_size > 0 && tmp == 0)
    {
        log_fatal << "failed to allocate: " << to_size*sizeof(struct epoll_event);
        throw std::bad_alloc();
    }
    events = reinterpret_cast<struct epoll_event*>(tmp);
    events_size = to_size;
    current = end();
}

gu::EPoll::EPoll() :
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
    
gu::EPoll::~EPoll()
{
    int err = closefd(e_fd);
    if (err != 0)
    {
        log_warn << "Error closing epoll socket: " << err;
    }
    free(events);
}
    
void gu::EPoll::set_mask(Socket* s, const int mask)
{
    log_debug << "set_mask(): " << s->get_fd() << " " 
              << s->get_event_mask() << " -> " << mask;
    if (mask == s->get_event_mask())
    {
        log_debug << "socket: " << s->get_fd() 
                  << " no mask update required";
        return;
    }
    
    int op = EPOLL_CTL_MOD;
    struct epoll_event ev = {to_epoll_mask(mask), {s}};
    int err = epoll_ctl(e_fd, op, s->get_fd(), &ev);
    
    if (err == -1)
    {
        log_error << "epoll_ctl(" << op << "," << s->get_fd() << "): " << err 
                  << " '" << strerror(err) << "'";
        throw std::runtime_error("");
    }
}

void gu::EPoll::insert(Socket* s)
{
    int op = EPOLL_CTL_ADD;
    struct epoll_event ev = {s->get_event_mask(), {s}};
    int err = epoll_ctl(e_fd, op, s->get_fd(), &ev);
    if (err != 0)
    {
        log_error << "epoll_ctl(" << e_fd << "," << op << "): " 
                  << strerror(err);
        throw std::runtime_error("");
    }
    resize(events_size + 1);
}

void gu::EPoll::erase(Socket* s)
{
    int op = EPOLL_CTL_DEL;
    struct epoll_event ev = {0, {0}};
    int err = epoll_ctl(e_fd, op, s->get_fd(), &ev);
    if (err != 0)
    {
        log_debug << "epoll erase: " << err;
    }
    resize(events_size - 1);
}

int gu::EPoll::poll(const int timeout)
{
    int ret = epoll_wait(e_fd, events, events_size, timeout);
    if (ret == -1)
    {
        ret = errno;
        log_error << "epoll_wait(): " << ret;
        current = end();
    }
    else
    {
        n_events = ret;
        current = events;
    }
    return ret;
}
    
gu::EPoll::iterator gu::EPoll::begin()
{
    if (n_events)
    {
        return current;
    }
    return end();
}
    
gu::EPoll::iterator gu::EPoll::end()
{
    return events + events_size;
}

void gu::EPoll::pop_front()
{
    if (n_events == 0)
    {
        throw std::logic_error("no events available");
    }
    --n_events;
    ++current;
}

gu::Socket* gu::EPoll::get_socket(gu::EPoll::iterator i)
{
    return reinterpret_cast<gu::Socket*>(i->data.ptr);
}

int gu::EPoll::get_revents(gu::EPoll::iterator i)
{
    return to_network_event_mask(i->events);
}
