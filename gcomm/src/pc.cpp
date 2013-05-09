/*
 * Copyright (C) 2009-2012 Codership Oy <info@codership.com>
 */

#include "pc.hpp"

#include "pc_proto.hpp"
#include "evs_proto.hpp"
#include "evs_message2.hpp"
#include "gmcast.hpp"
#include "defaults.hpp"

#include "gcomm/conf.hpp"
#include "gcomm/util.hpp"

#include "gu_datetime.hpp"


void gcomm::PC::handle_up(const void* cid, const Datagram& rb,
                   const ProtoUpMeta& um)
{
    send_up(rb, um);
}


int gcomm::PC::handle_down(Datagram& wb, const ProtoDownMeta& dm)
{
    if (wb.len() == 0)
    {
        gu_throw_error(EMSGSIZE);
    }
    return send_down(wb, dm);
}


size_t gcomm::PC::mtu() const
{
    // TODO:
    if (gmcast_ == 0) gu_throw_fatal << "not open";

    evs::UserMessage evsm;
    pc::UserMessage  pcm(0, 0);

    if (gmcast_->mtu() < 2*evsm.serial_size() + pcm.serial_size())
    {
        gu_throw_fatal << "transport max msg size too small: "
                          << gmcast_->mtu();
    }

    return gmcast_->mtu() - 2*evsm.serial_size() - pcm.serial_size();
}

const gcomm::UUID& gcomm::PC::uuid() const
{
    return gmcast_->uuid();
}

std::string gcomm::PC::listen_addr() const
{
    return gmcast_->listen_addr();
}


void gcomm::PC::connect(bool start_prim)
{
    try
    {
        // for backward compatibility with old approach: gcomm://0.0.0.0
        start_prim = (start_prim || host_is_any (uri_.get_host()));
    }
    catch (gu::NotSet& ns)
    {
        start_prim = true;
    }

    const bool wait_prim(
        gu::from_string<bool>(
            uri_.get_option(Conf::PcWaitPrim, Defaults::PcWaitPrim)));

    const gu::datetime::Period wait_prim_timeout(
        gu::from_string<gu::datetime::Period>(
            uri_.get_option(Conf::PcWaitPrimTimeout,
                            Defaults::PcWaitPrimTimeout)));

    pstack_.push_proto(gmcast_);
    pstack_.push_proto(evs_);
    pstack_.push_proto(pc_);
    pstack_.push_proto(this);
    pnet().insert(&pstack_);

    gmcast_->connect();

    closed_ = false;

    evs_->shift_to(evs::Proto::S_JOINING);
    pc_->connect(start_prim);

    // Due to #658 there is limited announce period after which
    // node is allowed to proceed to non-prim if other nodes
    // are not detected.
    gu::datetime::Date try_until(gu::datetime::Date::now() + announce_timeout_);
    while (start_prim == false && evs_->known_size() <= 1)
    {
        // Send join messages without handling them
        evs_->send_join(false);
        pnet().event_loop(gu::datetime::Sec/2);

        if (try_until < gu::datetime::Date::now())
        {
            break;
        }
    }

    log_debug << "PC/EVS Proto initial state: " << *evs_;
    if (evs_->state() != evs::Proto::S_OPERATIONAL)
    {
        log_debug << "PC/EVS Proto sending join request";
        evs_->send_join();
    }
    gcomm_assert(evs_->state() == evs::Proto::S_GATHER ||
                 evs_->state() == evs::Proto::S_INSTALL ||
                 evs_->state() == evs::Proto::S_OPERATIONAL);

    // - Due to #658 we loop here only if node is told to start in prim.
    // - Fix for #680, bypass waiting prim only if explicitly required
    try_until = gu::datetime::Date::now() + wait_prim_timeout;
    while ((wait_prim == true || start_prim == true) &&
           pc_->state() != pc::Proto::S_PRIM)
    {
        pnet().event_loop(gu::datetime::Sec/2);
        if (try_until < gu::datetime::Date::now())
        {
            pc_->close();
            evs_->close();
            gmcast_->close();
            pnet().erase(&pstack_);
            pstack_.pop_proto(this);
            pstack_.pop_proto(pc_);
            pstack_.pop_proto(evs_);
            pstack_.pop_proto(gmcast_);
            gu_throw_error(ETIMEDOUT) << "failed to reach primary view";
        }
    }

    pc_->set_mtu(mtu());
}

void gcomm::PC::connect(const gu::URI& uri)
{
    uri_ = uri;
    connect();
}


void gcomm::PC::close(bool force)
{

    if (force == false)
    {
        log_debug << "PC/EVS Proto leaving";
        pc_->close();
        evs_->close();

        gu::datetime::Date wait_until(gu::datetime::Date::now() + linger_);

        do
        {
            pnet().event_loop(gu::datetime::Sec/2);
        }
        while (evs_->state()         != evs::Proto::S_CLOSED &&
               gu::datetime::Date::now() <  wait_until);

        if (evs_->state() != evs::Proto::S_CLOSED)
        {
            evs_->shift_to(evs::Proto::S_CLOSED);
        }

        if (pc_->state() != pc::Proto::S_CLOSED)
        {
            log_warn << "PCProto didn't reach closed state";
        }

        gmcast_->close();
    }
    else
    {
        log_info << "Forced PC close";
    }
    pnet().erase(&pstack_);
    pstack_.pop_proto(this);
    pstack_.pop_proto(pc_);
    pstack_.pop_proto(evs_);
    pstack_.pop_proto(gmcast_);

    closed_ = true;
}


gcomm::PC::PC(Protonet& net, const gu::URI& uri) :
    Transport (net, uri),
    gmcast_    (0),
    evs_       (0),
    pc_        (0),
    closed_    (true),
    linger_    (param<gu::datetime::Period>(
                    conf_, uri, Conf::PcLinger, "PT20S")),
    announce_timeout_(param<gu::datetime::Period>(
                          conf_, uri, Conf::PcAnnounceTimeout,
                          Defaults::PcAnnounceTimeout))
{
    if (uri_.get_scheme() != Conf::PcScheme)
    {
        log_fatal << "invalid uri: " << uri_.to_string();
    }

    gmcast_ = new GMCast(pnet(), uri_);

    const UUID& uuid(gmcast_->uuid());

    if (uuid == UUID::nil())
    {
        gu_throw_fatal << "invalid UUID: " << uuid;
    }
    evs::UserMessage evsum;
    evs_ = new evs::Proto(pnet().conf(),
                          uuid, gmcast_->segment(),
                          uri_, gmcast_->mtu() - 2*evsum.serial_size());
    pc_  = new pc::Proto (pnet().conf(), uuid, gmcast_->segment(), uri_);

    conf_.set(Conf::PcLinger, gu::to_string(linger_));
}


gcomm::PC::~PC()
{
    if (!closed_)
    {
        try
        {
            close();
        }
        catch (...)
        { }
        sleep(1); // half-hearted attempt to avoid race with client threads
    }

    delete gmcast_;
    delete evs_;
    delete pc_;
}
