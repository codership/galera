/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_PROTOSTACK_HPP
#define GCOMM_PROTOSTACK_HPP

#include "gcomm/protolay.hpp"

#include "gu_lock.hpp"
#include "gu_network.hpp"

#include <deque>

namespace gcomm
{
    class Protostack;
    class Protonet;
}


class gcomm::Protostack
{
public:
    Protostack() : protos() { }
    
    void push_proto(Protolay* p);
    void pop_proto(Protolay* p);
    gu::datetime::Date handle_timers();
    void dispatch(gu::net::NetworkEvent& ev, const gu::net::Datagram& dg);
    
private:
    friend class Protonet;
    std::deque<Protolay*> protos;
};


class gcomm::Protonet
{
public:
    Protonet() : protos(), net(), mutex(), interrupted(false) { }
    gu::net::Network& get_net() { return net; }
    void insert(Protostack* pstack);
    void erase(Protostack* pstack);
    void event_loop(const gu::datetime::Period&);
    void interrupt()
    {
        gu::Lock lock(mutex);
        interrupted = true;
        net.interrupt();
    }
    gu::Mutex& get_mutex() { return mutex; }
private:
    Protonet(const Protonet&);
    void operator=(const Protonet&);
    std::deque<Protostack*> protos;
    gu::net::Network net;
    gu::Mutex mutex;
    bool interrupted;
};


#endif // GCOMM_PROTOSTACK_HPP
