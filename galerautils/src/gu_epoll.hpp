/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * @file gu_epoll.hpp Poll interface implementation using epoll
 */

#ifndef __GU_EPOLL_HPP__
#define __GU_EPOLL_HPP__

#include <vector>
#include "gu_poll.hpp"

// Declarations
namespace gu
{
    namespace net
    {
        class EPoll;
    }
}

struct epoll_event;


/*!
 * Poll implementation using Linux epoll facility.
 */
class gu::net::EPoll : public Poll
{
public:
    EPoll();
    ~EPoll();
    void insert(const PollEvent& ev);
    void erase(const PollEvent& ev);
    void modify(const PollEvent& ev);
    void poll(const gu::datetime::Period& p);
    bool empty() const;
    PollEvent front() const throw (gu::Exception);
    void pop_front() throw (gu::Exception);
    
private:
    EPoll(const EPoll&);
    void operator=(const EPoll&);
    
    int e_fd;
    int n_events;
    std::vector<epoll_event> events;
    std::vector<epoll_event>::iterator current;
};

#endif /* __GU_EPOLL_HPP__ */

