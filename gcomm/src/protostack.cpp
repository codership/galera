/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "gcomm/protostack.hpp"
#include "socket.hpp"
#include "gcomm/util.hpp"

void gcomm::Protostack::push_proto(Protolay* p)
{
    Critical<Protostack> crit(*this);
    protos_.push_front(p);
	
    // connect the pushed Protolay that's now on top
    // with the one that was previously on top,
    // if we had one, of course.
    if (protos_.size() > 1)
    {
        gcomm::connect(protos_[1], p);
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
                                  const std::string& val,
                                  Protolay::sync_param_cb_t& sync_param_cb)
{
    bool ret(false);
    for (std::deque<Protolay*>::iterator i(protos_.begin());
         i != protos_.end(); ++i)
    {
        ret |= (*i)->set_param(key, val, sync_param_cb);
    }
    return ret;
}
