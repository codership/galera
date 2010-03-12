/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "gcomm/protostack.hpp"

using namespace std;
using namespace std::rel_ops;
using namespace gu;
using namespace gu::datetime;
using namespace gu::net;
using namespace gcomm;

    
void gcomm::Protostack::push_proto(Protolay* p)
{ 
    
    std::deque<Protolay*>::iterator prev_begin(protos.begin());
    protos.push_front(p);
    if (prev_begin != protos.end())
    {
        gcomm::connect(*prev_begin, p);
    }
}

    
void gcomm::Protostack::pop_proto(Protolay* p)
{
    assert(protos.front() == p);
    if (protos.front() != p)
    {
        log_warn << "Protolay " << p << " is not protostack front";
        return;
    }
    protos.pop_front();
    if (protos.begin() != protos.end())
    {
        gcomm::disconnect(*protos.begin(), p);
    }
}

    
gu::datetime::Date gcomm::Protostack::handle_timers()
{
    gu::datetime::Date ret(gu::datetime::Date::max());
    for (std::deque<Protolay*>::reverse_iterator i = protos.rbegin(); 
         i != protos.rend(); ++i)
    {
        gu::datetime::Date t((*i)->handle_timers());
        if (t < ret) ret = t;
    }
    return ret;
}


void gcomm::Protostack::dispatch(gu::net::NetworkEvent& ev, 
                                 const gu::net::Datagram& dg)
{
    assert(ev.get_socket() != 0);
    gu::net::Socket& s(*ev.get_socket());
    if (protos.empty() == false)
    {
        protos.back()->handle_up(s.get_fd(), dg,
                                 ProtoUpMeta(s.get_errno()));
    }
}


void gcomm::Protonet::insert(Protostack* pstack) 
{ 
    if (find(protos.begin(), protos.end(), pstack) != protos.end())
    {
        gu_throw_fatal;
    }
    protos.push_back(pstack); 
}


void gcomm::Protonet::erase(Protostack* pstack)
{
    std::deque<Protostack*>::iterator i;
    if ((i = find(protos.begin(), protos.end(), pstack)) == protos.end())
    {
        gu_throw_fatal;
    }
    protos.erase(i);
}


void gcomm::Protonet::event_loop(const Period& p)
{
    Date stop = Date::now() + p;
    do
    {
        Date next_time(Date::max());
        
        {
            Lock lock(mutex);
            for (deque<Protostack*>::iterator i = protos.begin(); i != protos.end();
                 ++i)
            {
                next_time = min(next_time, (*i)->handle_timers());
            }
        }
        
        Period sleep_p(min(stop - Date::now(), next_time - Date::now()));
        
        if (sleep_p < 0)
            sleep_p = 0;
        
        NetworkEvent ev(net.wait_event(sleep_p, false));
        if ((ev.get_event_mask() & E_OUT) != 0)
        {
            Lock lock(mutex);
            ev.get_socket()->send();
        }
        else if ((ev.get_event_mask() & E_EMPTY) == 0)
        {
            const int mask(ev.get_event_mask());
            Socket& s(*ev.get_socket());
            const Datagram* dg(0);
            if (s.get_state() == Socket::S_CONNECTED &&
                (mask & E_IN))
            {
                dg = s.recv();
                gcomm_assert(dg != 0 
                             || s.get_state() == Socket::S_CLOSED
                             || s.get_state() == Socket::S_FAILED);
            }
            
            Lock lock(mutex);
            for (deque<Protostack*>::iterator i = protos.begin();
                 i != protos.end(); ++i)
            {
                (*i)->dispatch(ev, dg != 0 ? *dg : Datagram());
            }
        }
        Lock lock(mutex);
        if (interrupted == true)
        {
            interrupted = false;
            break;
        }
    }
    while (stop >= Date::now());
}
