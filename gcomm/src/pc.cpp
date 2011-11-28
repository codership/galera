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


void PC::handle_up(const void* cid, const Datagram& rb,
                   const ProtoUpMeta& um)
{

#if 0
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
#endif // 0
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
    pc::UserMessage  pcm(0, 0);

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
    const bool start_prim = host_is_any (uri_.get_host());

    pstack_.push_proto(gmcast);
    pstack_.push_proto(evs);
    pstack_.push_proto(pc);
    pstack_.push_proto(this);
    get_pnet().insert(&pstack_);

    gmcast->connect();

    closed = false;

    evs->shift_to(evs::Proto::S_JOINING);
    pc->connect(start_prim);

    Date try_until(Date::now() + Period("PT30S"));
    while (start_prim == false && evs->get_known_size() <= 1)
    {
        // Send join messages without handling them
        evs->send_join(false);
        get_pnet().event_loop(Sec/2);
        if (try_until < Date::now())
        {
            pc->close();
            evs->close();
            gmcast->close();

            get_pnet().erase(&pstack_);
            pstack_.pop_proto(this);
            pstack_.pop_proto(pc);
            pstack_.pop_proto(evs);
            pstack_.pop_proto(gmcast);

            gu_throw_error(ETIMEDOUT) << "failed to join group";
        }
    }

    log_debug << "PC/EVS Proto initial state: " << *evs;
    if (evs->get_state() != evs::Proto::S_OPERATIONAL)
    {
        log_debug << "PC/EVS Proto sending join request";
        evs->send_join();
    }
    gcomm_assert(evs->get_state() == evs::Proto::S_GATHER ||
                 evs->get_state() == evs::Proto::S_INSTALL ||
                 evs->get_state() == evs::Proto::S_OPERATIONAL);


    // #596 Time out if PRIM view cannot be reached before connect
    //      timeout expires.
    while (pc->get_state() != pc::Proto::S_PRIM)
    {
        get_pnet().event_loop(Sec/2);
        if (try_until < Date::now())
        {
            pc->close();
            evs->close();
            gmcast->close();
            get_pnet().erase(&pstack_);
            pstack_.pop_proto(this);
            pstack_.pop_proto(pc);
            pstack_.pop_proto(evs);
            pstack_.pop_proto(gmcast);
            gu_throw_error(ETIMEDOUT) << "failed to reach primary view";
        }

    }

    pc->set_mtu(get_mtu());
}


void PC::close()
{

    log_debug << "PC/EVS Proto leaving";
    pc->close();
    evs->close();

    Date wait_until(Date::now() + linger);

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

    gmcast->close();

    get_pnet().erase(&pstack_);
    pstack_.pop_proto(this);
    pstack_.pop_proto(pc);
    pstack_.pop_proto(evs);
    pstack_.pop_proto(gmcast);

    closed = true;
}


PC::PC(Protonet& net, const gu::URI& uri) :
    Transport (net, uri),
    gmcast    (0),
    evs       (0),
    pc        (0),
    closed    (true),
    linger    (param<Period>(conf_, uri, Conf::PcLinger, "PT2S"))
{
    if (uri_.get_scheme() != Conf::PcScheme)
    {
        log_fatal << "invalid uri: " << uri_.to_string();
    }

    gmcast = new GMCast(get_pnet(), uri_);

    const UUID& uuid(gmcast->get_uuid());

    if (uuid == UUID::nil())
    {
        gu_throw_fatal << "invalid UUID: " << uuid;
    }
    evs::UserMessage evsum;
    evs = new evs::Proto(get_pnet().conf(),
                         uuid, uri_, gmcast->get_mtu() - 2*evsum.serial_size());
    pc  = new pc::Proto (get_pnet().conf(), uuid, uri_);

    conf_.set(Conf::PcLinger, gu::to_string(linger));
}


PC::~PC()
{
    if (!closed)
    {
        try
        {
            close();
        }
        catch (...)
        { }
        sleep(1); // half-hearted attempt to avoid race with client threads
    }

    delete gmcast;
    delete evs;
    delete pc;
}
