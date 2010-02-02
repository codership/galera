/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "pc.hpp"

#include "pc_proto.hpp"
#include "evs_proto.hpp"
#include "evs_message2.hpp"
#include "gmcast.hpp"

#include "gcomm/conf.hpp"
#include "gcomm/util.hpp"

#include "gu_datetime.hpp"

using namespace std;
using namespace gcomm;

using namespace gu;
using namespace gu::net;
using namespace gu::datetime;


void PC::handle_up(int cid, const Datagram& rb,
                   const ProtoUpMeta& um)
{
    
    if (um.has_view() == true && um.get_view().get_type() == V_PRIM)
    {
        /* Call close gmcast transport for all nodes that have left 
         * or partitioned */
        for (NodeList::const_iterator i = um.get_view().get_left().begin();
             i != um.get_view().get_left().end(); ++i)
        {
            gmcast->close(NodeList::get_key(i));
        }
        
        for (NodeList::const_iterator i =
                 um.get_view().get_partitioned().begin();
             i != um.get_view().get_partitioned().end(); ++i)
        {
            gmcast->close(NodeList::get_key(i));
        }
    }
    
    send_up(rb, um);
}


int PC::handle_down(Datagram& wb, const ProtoDownMeta& dm)
{
    if (wb.get_len() == 0)
    {
        gu_throw_error(EMSGSIZE);
    }
    return send_down(wb, dm);
}


size_t PC::get_mtu() const
{
    // TODO: 
    if (gmcast == 0) gu_throw_fatal << "not open";
    
    evs::UserMessage evsm;
    pc::UserMessage  pcm(0);
    
    if (gmcast->get_mtu() < 2*evsm.serial_size() + pcm.serial_size())
    {
        gu_throw_fatal << "transport max msg size too small: "
                          << gmcast->get_mtu();
    }
    
    return gmcast->get_mtu() - 2*evsm.serial_size() - pcm.serial_size();
}


bool PC::supports_uuid() const
{
    if (gmcast->supports_uuid() == false)
    {
        // alex: what is the meaning of this ?
        gu_throw_fatal << "transport does not support UUID";
    }
    return true;
}


const UUID& PC::get_uuid() const
{
    return gmcast->get_uuid();
}


void PC::connect()
{
    const bool start_prim = host_is_any (uri.get_host());
    
    pstack.push_proto(gmcast);
    pstack.push_proto(evs);
    pstack.push_proto(pc);
    pstack.push_proto(this);
    get_pnet().insert(&pstack);
    
    gmcast->connect();
    
    closed = false;
    
    evs->shift_to(evs::Proto::S_JOINING);
    pc->connect(start_prim);
    
    while (start_prim == false && evs->get_known_size() <= 1)
    {
        // Send join messages without handling them
        evs->send_join(false);
        get_pnet().event_loop(Sec/2);
    }
    
    log_debug << "PC/EVS Proto initial state: " << *evs;
    log_debug << "PC/EVS Proto sending join request";
    
    evs->send_join();
    gcomm_assert(evs->get_state() == evs::Proto::S_RECOVERY ||
                 evs->get_state() == evs::Proto::S_OPERATIONAL);
    
    do
    {
        get_pnet().event_loop(Sec/2);
    }
    while (pc->get_state() != pc::Proto::S_PRIM);
}


void PC::close()
{
    
    log_debug << "PC/EVS Proto leaving";
    evs->close();
    
    Date wait_until(Date::now() + leave_grace_period);

    do
    {
        get_pnet().event_loop(Sec/2);
    } 
    while (evs->get_state() != evs::Proto::S_CLOSED &&
           Date::now()      <  wait_until);
    
    if (evs->get_state() != evs::Proto::S_CLOSED)
    {
        evs->shift_to(evs::Proto::S_CLOSED);
    }
    
    if (pc->get_state() != pc::Proto::S_CLOSED)
    {
        log_warn << "PCProto didn't reach closed state";
    }
    
    get_pnet().erase(&pstack);
    pstack.pop_proto(this);
    pstack.pop_proto(pc);
    pstack.pop_proto(evs);
    pstack.pop_proto(gmcast);
    
    gmcast->close();

    closed = true;
}


PC::PC(Protonet& net_, const string& uri_) :
    Transport (net_, uri_),
    gmcast    (0),
    evs       (0),
    pc        (0),
    closed    (true),
    leave_grace_period("PT5S")
{
    if (uri.get_scheme() != Conf::PcScheme)
    {
        log_fatal << "invalid uri: " << uri.to_string();
    }
    URI tp_uri(uri);
    
    tp_uri._set_scheme(Conf::GMCastScheme); // why do we need this?
    
    gmcast = new GMCast(get_pnet(), tp_uri.to_string());
    
    const UUID& uuid(gmcast->get_uuid());
    
    if (uuid == UUID::nil())
    {
        gu_throw_fatal << "invalid UUID: " << uuid;
    }
    evs::UserMessage evsum;
    evs = new evs::Proto(uuid, uri.to_string(), gmcast->get_mtu() - 2*evsum.serial_size());
    pc  = new pc::Proto (uuid);
}


PC::~PC()
{
    if (!closed)
    {
        close();
        sleep(1); // half-hearted attempt to avoid race with client threads
    }

    delete gmcast;
    delete evs;
    delete pc;
}
