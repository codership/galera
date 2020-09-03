/*
 * Copyright (C) 2009-2020 Codership Oy <info@codership.com>
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
    if (pc_recovery_ &&
        um.err_no() == 0 &&
        um.has_view() &&
        um.view().id().type() == V_PRIM)
    {
        ViewState vst(const_cast<UUID&>(uuid()),
                      const_cast<View&>(um.view()),
                      conf_);
        log_info << "save pc into disk";
        vst.write_file();
    }
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

    bool wait_prim(param<bool>(conf_, uri_, Conf::PcWaitPrim,
                               Defaults::PcWaitPrim));
    const gu::datetime::Period wait_prim_timeout(
        param<gu::datetime::Period>(conf_, uri_, Conf::PcWaitPrimTimeout,
                                    Defaults::PcWaitPrimTimeout));

    // --wsrep-new-cluster specified in command line
    // or cluster address as gcomm://0.0.0.0 or gcomm://
    // should take precedence. otherwise it's not able to bootstrap.
    if (start_prim) {
        log_info << "start_prim is enabled, turn off pc_recovery";
    } else if (rst_view_.type() == V_PRIM) {
        wait_prim = false;
    }

    pstack_.push_proto(gmcast_);
    pstack_.push_proto(evs_);
    pstack_.push_proto(pc_);
    pstack_.push_proto(this);
    pnet().insert(&pstack_);

    gmcast_->connect_precheck(start_prim);
    gmcast_->connect();

    closed_ = false;

    evs_->shift_to(evs::Proto::S_JOINING);
    pc_->connect(start_prim);

    // Due to #658 there is limited announce period after which
    // node is allowed to proceed to non-prim if other nodes
    // are not detected.
    gu::datetime::Date try_until(
        gu::datetime::Date::monotonic() + announce_timeout_);
    while (start_prim == false && evs_->known_size() <= 1)
    {
        // Send join messages without handling them
        evs_->send_join(false);
        pnet().event_loop(gu::datetime::Sec/2);

        if (try_until < gu::datetime::Date::monotonic())
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
    try_until = gu::datetime::Date::monotonic() + wait_prim_timeout;
    while ((wait_prim == true || start_prim == true) &&
           pc_->state() != pc::Proto::S_PRIM)
    {
        pnet().event_loop(gu::datetime::Sec/2);
        if (try_until < gu::datetime::Date::monotonic())
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
    if (force == true)
    {
        log_info << "Forced PC close";
        gmcast_->close();
        // Don't bother closing PC and EVS at this point. Currently
        // there is no way of knowing why forced close was issued,
        // so graceful close of PC and/or EVS may not be safe.
        // pc_->close();
        // evs_->close();
    }
    else
    {
        log_debug << "PC/EVS Proto leaving";
        pc_->close();
        evs_->close();

        gu::datetime::Date wait_until(
            gu::datetime::Date::monotonic() + linger_);

        do
        {
            pnet().event_loop(gu::datetime::Sec/2);
        }
        while (evs_->state()         != evs::Proto::S_CLOSED &&
               gu::datetime::Date::monotonic() <  wait_until);

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
    pnet().erase(&pstack_);
    pstack_.pop_proto(this);
    pstack_.pop_proto(pc_);
    pstack_.pop_proto(evs_);
    pstack_.pop_proto(gmcast_);
    ViewState::remove_file(conf_);

    closed_ = true;
}

void gcomm::PC::handle_get_status(gu::Status& status) const
{
    status.insert("gcomm_uuid", uuid().full_str());
    status.insert("cluster_weight", gu::to_string(
                      pc_ ? pc_->cluster_weight() : 0));
    status.insert("gmcast_segment", gu::to_string(int(gmcast_->segment())));
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
                          Defaults::PcAnnounceTimeout)),
    pc_recovery_ (param<bool>(conf_, uri,
                              Conf::PcRecovery, Defaults::PcRecovery)),
    rst_uuid_(),
    rst_view_()

{
    if (uri_.get_scheme() != Conf::PcScheme)
    {
        log_fatal << "invalid uri: " << uri_.to_string();
    }

    conf_.set(Conf::PcRecovery, gu::to_string(pc_recovery_));
    bool restored = false;
    ViewState vst(rst_uuid_, rst_view_, conf_);
    if (pc_recovery_) {
        if (vst.read_file()) {
            log_info << "restore pc from disk successfully";
            rst_uuid_.increment_incarnation();
            vst.write_file();
            restored = true;
        } else {
            log_info << "restore pc from disk failed";
        }
    } else {
        log_info << "skip pc recovery and remove state file";
        ViewState::remove_file(conf_);
    }

    gmcast_ = new GMCast(pnet(), uri_, restored ? &rst_uuid_ : NULL);
    const UUID& uuid(gmcast_->uuid());
    if (uuid == UUID::nil())
    {
        gu_throw_fatal << "invalid UUID: " << uuid;
    }
    evs::UserMessage evsum;
    evs_ = new evs::Proto(pnet().conf(),
                          uuid, gmcast_->segment(),
                          uri_, gmcast_->mtu() - 2*evsum.serial_size(),
                          restored ? &rst_view_ : NULL);
    pc_  = new pc::Proto (pnet().conf(), uuid, gmcast_->segment(), uri_,
                          restored ? &rst_view_ : NULL);
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
