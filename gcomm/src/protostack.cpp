/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "gcomm/protostack.hpp"
#include "socket.hpp"
#include "gcomm/util.hpp"

void gcomm::Protostack::push_proto(Protolay* p)
{
    Critical<Protostack> crit(*this);
    std::deque<Protolay*>::iterator prev_begin(protos_.begin());
    protos_.push_front(p);
    if (prev_begin != protos_.end())
    {
        gcomm::connect(*prev_begin, p);
    }
}


void gcomm::Protostack::pop_proto(Protolay* p)
{
    Critical<Protostack> crit(*this);
    assert(protos_.front() == p);
    if (protos_.front() != p)
    {
        log_warn << "Protolay " << p << " is not protostack front";
        return;
    }
    protos_.pop_front();
    if (protos_.begin() != protos_.end())
    {
        gcomm::disconnect(*protos_.begin(), p);
    }
}


gu::datetime::Date gcomm::Protostack::handle_timers()
{

    gu::datetime::Date ret(gu::datetime::Date::max());
    Critical<Protostack> crit(*this);
    for (std::deque<Protolay*>::reverse_iterator i = protos_.rbegin();
         i != protos_.rend(); ++i)
    {
        gu::datetime::Date t((*i)->handle_timers());
        if (t < ret) ret = t;
    }
    return ret;
}


void gcomm::Protostack::dispatch(const void* id,
                                 const Datagram& dg,
                                 const ProtoUpMeta& um)
{
    Critical<Protostack> crit(*this);
    if (protos_.empty() == false)
    {
        protos_.back()->handle_up(id, dg, um);
    }
}


bool gcomm::Protostack::set_param(const std::string& key,
                                  const std::string& val)
{
    bool ret(false);
    for (std::deque<Protolay*>::iterator i(protos_.begin());
         i != protos_.end(); ++i)
    {
        ret |= (*i)->set_param(key, val);
    }
    return ret;
}
