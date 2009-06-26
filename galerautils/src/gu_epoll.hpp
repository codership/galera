
#ifndef __GU_EPOLL_HPP__
#define __GU_EPOLL_HPP__

#include <cstdlib>

namespace gu
{
    class EPollEvent;
    class EPoll;
    class Socket;
}

struct epoll_event;

class gu::EPollEvent
{
    int revents;
    Socket* socket;
public:
    EPollEvent(int revents_, Socket* socket) :
        revents(revents_),
        socket(socket)
    {
    }

    int get_revents() const
    {
        return revents;
    }

    Socket* get_socket() const
    {
        return socket;
    }
};

class gu::EPoll
{
    int e_fd;
    struct epoll_event* events;
    size_t events_size;
    size_t n_events;
    struct epoll_event* current;
    void resize(const size_t to_size);
public:
    EPoll();
    ~EPoll();
    
    void insert(Socket* s);
    void erase(Socket* s);
    void set_mask(Socket* s, const int mask);
    int poll(const int timeout);
    
    typedef struct epoll_event* iterator;
    
    iterator begin();
    iterator end();
    bool empty();
    EPollEvent front();
    void pop_front();

    static gu::Socket* get_socket(gu::EPoll::iterator i);
    static int get_revents(gu::EPoll::iterator i);

};

#endif /* __GU_EPOLL_HPP__ */

