/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_PROTOSTACK_HPP
#define GCOMM_PROTOSTACK_HPP

#include "gcomm/protolay.hpp"

#include "gu_lock.hpp"

#include <vector>
#include <deque>

namespace gcomm
{
    class Socket;
    class Acceptor;
    class Protostack;
    class Protonet;
    class BoostProtonet;
}


class gcomm::Protostack
{
public:
#ifdef HAVE_PSI_INTERFACE
    Protostack() : protos_(), mutex_(WSREP_PFS_INSTR_TAG_PROTSTACK_MUTEX) { }
#else
    Protostack() : protos_(), mutex_() { }
#endif /* HAVE_PSI_INTERFACE */
    void push_proto(Protolay* p);
    void pop_proto(Protolay* p);
    gu::datetime::Date handle_timers();
    void dispatch(const void* id, const Datagram& dg,
                  const ProtoUpMeta& um);
    bool set_param(const std::string&, const std::string&);
    void enter() { mutex_.lock(); }
    void leave() { mutex_.unlock(); }
private:
    friend class Protonet;
    std::deque<Protolay*> protos_;
#ifdef HAVE_PSI_INTERFACE
    gu::MutexWithPFS mutex_;
#else
    gu::Mutex mutex_;
#endif /* HAVE_PSI_INTERFACE */
};


#endif // GCOMM_PROTOSTACK_HPP
