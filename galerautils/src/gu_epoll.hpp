
#ifndef __GU_EPOLL_HPP__
#define __GU_EPOLL_HPP__

#include <cstdlib>



namespace gu
{
    class EPoll;
    class Socket;
}

struct epoll_event;

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
    
    int set_mask(Socket* s, const int mask);
    int poll(const int timeout);
    
    typedef struct epoll_event* iterator;
    
    iterator begin();
    iterator end();
    void pop_front();

    static gu::Socket* get_socket(gu::EPoll::iterator i);
    static int get_revents(gu::EPoll::iterator i);

};

#endif /* __GU_EPOLL_HPP__ */

