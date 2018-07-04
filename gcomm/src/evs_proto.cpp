/*
 * Copyright (C) 2009-2014 Codership Oy <info@codership.com>
 */

#ifdef PROFILE_EVS_PROTO
#define GCOMM_PROFILE 1
#else
#undef GCOMM_PROFILE
#endif // PROFILE_EVS_PROTO

#include "evs_proto.hpp"
#include "evs_message2.hpp"
#include "evs_input_map2.hpp"

#include "gcomm/transport.hpp"
#include "gcomm/conf.hpp"
#include "gcomm/util.hpp"

#include "defaults.hpp"

#include <cmath>

#include <stdexcept>
#include <algorithm>
#include <numeric>
#include <iterator>
#include <set>

using namespace std::rel_ops;

// Convenience macros for debug and info logging
#define evs_log_debug(__mask__)             \
    if ((debug_mask_ & (__mask__)) == 0) { } \
    else log_debug << self_string() << ": "

#define evs_log_info(__mask__)              \
    if ((info_mask_ & (__mask__)) == 0) { }  \
    else log_info << self_string() << ": "


gcomm::evs::Proto::Proto(gu::Config&    conf,
                         const UUID&    my_uuid,
                         SegmentId      segment,
                         const gu::URI& uri,
                         const size_t   mtu,
                         const View*    rst_view)
    :
    Protolay(conf),
    timers_(),
    version_(check_range(Conf::EvsVersion,
                         param<int>(conf, uri, Conf::EvsVersion, Defaults::EvsVersion),
                         0, GCOMM_PROTOCOL_MAX_VERSION + 1)),
    debug_mask_(param<int>(conf, uri, Conf::EvsDebugLogMask, "0x1", std::hex)),
    info_mask_(param<int>(conf, uri, Conf::EvsInfoLogMask, "0x0", std::hex)),
    last_stats_report_(gu::datetime::Date::now()),
    collect_stats_(true),
    hs_agreed_("0.0,0.0001,0.00031623,0.001,0.0031623,0.01,0.031623,0.1,0.31623,1.,3.1623,10.,31.623"),
    hs_safe_("0.0,0.0001,0.00031623,0.001,0.0031623,0.01,0.031623,0.1,0.31623,1.,3.1623,10.,31.623"),
    hs_local_causal_("0.0,0.0001,0.00031623,0.001,0.0031623,0.01,0.031623,0.1,0.31623,1.,3.1623,10.,31.623"),
    safe_deliv_latency_(),
    send_queue_s_(0),
    n_send_queue_s_(0),
    sent_msgs_(7, 0),
    retrans_msgs_(0),
    recovered_msgs_(0),
    recvd_msgs_(7, 0),
    delivered_msgs_(O_LOCAL_CAUSAL + 1),
    send_user_prof_    ("send_user"),
    send_gap_prof_     ("send_gap"),
    send_join_prof_    ("send_join"),
    send_install_prof_ ("send_install"),
    send_leave_prof_   ("send_leave"),
    consistent_prof_   ("consistent"),
    consensus_prof_    ("consensus"),
    shift_to_prof_     ("shift_to"),
    input_map_prof_    ("input_map"),
    delivery_prof_     ("delivery"),
    delivering_(false),
    my_uuid_(my_uuid),
    segment_(segment),
    known_(),
    self_i_(),
    view_forget_timeout_(
        check_range(Conf::EvsViewForgetTimeout,
                    param<gu::datetime::Period>(
                        conf, uri, Conf::EvsViewForgetTimeout,
                        Defaults::EvsViewForgetTimeout),
                    gu::from_string<gu::datetime::Period>(
                        Defaults::EvsViewForgetTimeoutMin),
                    gu::datetime::Period::max())),
    inactive_timeout_(
        check_range(Conf::EvsInactiveTimeout,
                    param<gu::datetime::Period>(
                        conf, uri, Conf::EvsInactiveTimeout,
                        Defaults::EvsInactiveTimeout),
                    gu::from_string<gu::datetime::Period>(
                        Defaults::EvsInactiveTimeoutMin),
                    gu::datetime::Period::max())),
    suspect_timeout_(
        check_range(Conf::EvsSuspectTimeout,
                    param<gu::datetime::Period>(
                        conf, uri, Conf::EvsSuspectTimeout,
                        Defaults::EvsSuspectTimeout),
                    gu::from_string<gu::datetime::Period>(
                        Defaults::EvsSuspectTimeoutMin),
                    gu::datetime::Period::max())),
    inactive_check_period_(
        check_range(Conf::EvsInactiveCheckPeriod,
                    param<gu::datetime::Period>(
                        conf, uri, Conf::EvsInactiveCheckPeriod,
                        Defaults::EvsInactiveCheckPeriod),
                    gu::datetime::Period::min(),
                    suspect_timeout_/2 + 1)),
    retrans_period_(
        check_range(Conf::EvsKeepalivePeriod,
                    param<gu::datetime::Period>(
                        conf, uri, Conf::EvsKeepalivePeriod,
                        Defaults::EvsRetransPeriod),
                    gu::from_string<gu::datetime::Period>(
                        Defaults::EvsRetransPeriodMin),
                    suspect_timeout_/3 + 1)),
    install_timeout_(
        check_range(Conf::EvsInstallTimeout,
                    param<gu::datetime::Period>(
                        conf, uri, Conf::EvsInstallTimeout,
                        gu::to_string(inactive_timeout_/2)),
                    retrans_period_, inactive_timeout_ + 1)),
    join_retrans_period_(
        check_range(Conf::EvsJoinRetransPeriod,
                    param<gu::datetime::Period>(
                        conf, uri, Conf::EvsJoinRetransPeriod,
                        Defaults::EvsJoinRetransPeriod),
                    gu::from_string<gu::datetime::Period>(
                        Defaults::EvsRetransPeriodMin),
                    gu::datetime::Period::max())),
    stats_report_period_(
        check_range(Conf::EvsStatsReportPeriod,
                    param<gu::datetime::Period>(
                        conf, uri, Conf::EvsStatsReportPeriod,
                        Defaults::EvsStatsReportPeriod),
                    gu::from_string<gu::datetime::Period>(
                        Defaults::EvsStatsReportPeriodMin),
                    gu::datetime::Period::max())),
    causal_keepalive_period_(retrans_period_),
    delay_margin_(param<gu::datetime::Period>(
                      conf, uri, Conf::EvsDelayMargin,
                      Defaults::EvsDelayMargin)),
    delayed_keep_period_(param<gu::datetime::Period>(
                             conf, uri, Conf::EvsDelayedKeepPeriod,
                             Defaults::EvsDelayedKeepPeriod)),
    last_inactive_check_   (gu::datetime::Date::now()),
    last_causal_keepalive_ (gu::datetime::Date::now()),
    current_view_(0, ViewId(V_TRANS, my_uuid,
                         rst_view ? rst_view -> id().seq() + 1 : 0)),
    previous_view_(),
    previous_views_(),
    gather_views_(),
    input_map_(new InputMap()),
    causal_queue_(),
    consensus_(*this, known_, *input_map_, current_view_),
    install_message_(0),
    max_view_id_seq_(0),
    attempt_seq_(1),
    new_view_logged_(false),
    max_install_timeouts_(
        check_range(Conf::EvsMaxInstallTimeouts,
                    param<int>(conf, uri, Conf::EvsMaxInstallTimeouts,
                               Defaults::EvsMaxInstallTimeouts),
                    0, std::numeric_limits<int>::max())),
    install_timeout_count_(0),
    fifo_seq_(-1),
    last_sent_(-1),
    send_window_(
        check_range(Conf::EvsSendWindow,
                    param<seqno_t>(conf, uri, Conf::EvsSendWindow,
                                   Defaults::EvsSendWindow),
                    gu::from_string<seqno_t>(Defaults::EvsSendWindowMin),
                    std::numeric_limits<seqno_t>::max())),
    user_send_window_(
        check_range(Conf::EvsUserSendWindow,
                    param<seqno_t>(conf, uri, Conf::EvsUserSendWindow,
                                   Defaults::EvsUserSendWindow),
                    gu::from_string<seqno_t>(Defaults::EvsUserSendWindowMin),
                    send_window_ + 1)),
    output_(),
    send_buf_(),
    max_output_size_(128),
    mtu_(mtu),
    use_aggregate_(param<bool>(conf, uri, Conf::EvsUseAggregate, "true")),
    self_loopback_(false),
    state_(S_CLOSED),
    shift_to_rfcnt_(0),
    pending_leave_(false),
    isolation_end_(gu::datetime::Date::zero()),
    delayed_list_(),
    auto_evict_(param<size_t>(conf, uri, Conf::EvsAutoEvict,
                              Defaults::EvsAutoEvict))
{
    log_info << "EVS version " << version_;

    conf.set(Conf::EvsVersion, gu::to_string(version_));
    conf.set(Conf::EvsViewForgetTimeout, gu::to_string(view_forget_timeout_));
    conf.set(Conf::EvsSuspectTimeout, gu::to_string(suspect_timeout_));
    conf.set(Conf::EvsInactiveTimeout, gu::to_string(inactive_timeout_));
    conf.set(Conf::EvsKeepalivePeriod, gu::to_string(retrans_period_));
    conf.set(Conf::EvsInactiveCheckPeriod,
             gu::to_string(inactive_check_period_));
    conf.set(Conf::EvsJoinRetransPeriod, gu::to_string(join_retrans_period_));
    conf.set(Conf::EvsInstallTimeout, gu::to_string(install_timeout_));
    conf.set(Conf::EvsStatsReportPeriod, gu::to_string(stats_report_period_));
    conf.set(Conf::EvsCausalKeepalivePeriod,
             gu::to_string(causal_keepalive_period_));
    conf.set(Conf::EvsSendWindow, gu::to_string(send_window_));
    conf.set(Conf::EvsUserSendWindow, gu::to_string(user_send_window_));
    conf.set(Conf::EvsUseAggregate, gu::to_string(use_aggregate_));
    conf.set(Conf::EvsDebugLogMask, gu::to_string(debug_mask_, std::hex));
    conf.set(Conf::EvsInfoLogMask, gu::to_string(info_mask_, std::hex));
    conf.set(Conf::EvsMaxInstallTimeouts, gu::to_string(max_install_timeouts_));
    conf.set(Conf::EvsDelayMargin, gu::to_string(delay_margin_));
    conf.set(Conf::EvsDelayedKeepPeriod, gu::to_string(delayed_keep_period_));
    conf.set(Conf::EvsAutoEvict, gu::to_string(auto_evict_));
    //

    known_.insert_unique(
        std::make_pair(my_uuid_, Node(*this)));
    self_i_ = known_.begin();
    assert(NodeMap::value(self_i_).operational() == true);

    NodeMap::value(self_i_).set_index(0);
    input_map_->reset(1);
    current_view_.add_member(my_uuid_, segment_);
    // we don't need to store previous views, do we ?
    if (rst_view) {
        previous_view_ = *rst_view;
        previous_views_.insert(
            std::make_pair(rst_view -> id(), gu::datetime::Date::now()));
    }
    if (mtu_ != std::numeric_limits<size_t>::max())
    {
        send_buf_.reserve(mtu_);
    }
}


gcomm::evs::Proto::~Proto()
{
    output_.clear();
    delete install_message_;
    delete input_map_;
}


bool
gcomm::evs::Proto::set_param(const std::string& key, const std::string& val, 
                            Protolay::sync_param_cb_t& sync_param_cb)
{
    if (key == gcomm::Conf::EvsVersion)
    {
        version_ = check_range(Conf::EvsVersion,
                               gu::from_string<int>(val),
                               0, GCOMM_PROTOCOL_MAX_VERSION + 1);
        conf_.set(Conf::EvsVersion, gu::to_string(version_));
        // trigger configuration change to propagate version
        shift_to(S_GATHER, true);
        return true;
    }
    else if (key == gcomm::Conf::EvsSendWindow)
    {
        send_window_ = check_range(Conf::EvsSendWindow,
                                   gu::from_string<seqno_t>(val),
                                   user_send_window_,
                                   std::numeric_limits<seqno_t>::max());
        conf_.set(Conf::EvsSendWindow, gu::to_string(send_window_));
        return true;
    }
    else if (key == gcomm::Conf::EvsUserSendWindow)
    {
        user_send_window_ = check_range(
            Conf::EvsUserSendWindow,
            gu::from_string<seqno_t>(val),
            gu::from_string<seqno_t>(Defaults::EvsUserSendWindowMin),
            send_window_ + 1);
        conf_.set(Conf::EvsUserSendWindow, gu::to_string(user_send_window_));
        return true;
    }
    else if (key == gcomm::Conf::EvsMaxInstallTimeouts)
    {
        max_install_timeouts_ = check_range(
            Conf::EvsMaxInstallTimeouts,
            gu::from_string<int>(val),
            0, std::numeric_limits<int>::max());
        conf_.set(Conf::EvsMaxInstallTimeouts, gu::to_string(max_install_timeouts_));
        return true;
    }
    else if (key == Conf::EvsStatsReportPeriod)
    {
        stats_report_period_ = check_range(
            Conf::EvsStatsReportPeriod,
            gu::from_string<gu::datetime::Period>(val),
            gu::from_string<gu::datetime::Period>(Defaults::EvsStatsReportPeriodMin),
            gu::datetime::Period::max());
        conf_.set(Conf::EvsStatsReportPeriod, gu::to_string(stats_report_period_));
        reset_timer(T_STATS);
        return true;
    }
    else if (key == Conf::EvsInfoLogMask)
    {
        info_mask_ = gu::from_string<int>(val, std::hex);
        conf_.set(Conf::EvsInfoLogMask, gu::to_string<int>(info_mask_, std::hex));
        return true;
    }
    else if (key == Conf::EvsDebugLogMask)
    {
        debug_mask_ = gu::from_string<int>(val, std::hex);
        conf_.set(Conf::EvsDebugLogMask, gu::to_string<int>(debug_mask_, std::hex));
        return true;
    }
    else if (key == Conf::EvsSuspectTimeout)
    {
        suspect_timeout_ = check_range(
            Conf::EvsSuspectTimeout,
            gu::from_string<gu::datetime::Period>(val),
            gu::from_string<gu::datetime::Period>(Defaults::EvsSuspectTimeoutMin),
            gu::datetime::Period::max());
        conf_.set(Conf::EvsSuspectTimeout, gu::to_string(suspect_timeout_));
        reset_timer(T_INACTIVITY);
        return true;
    }
    else if (key == Conf::EvsInactiveTimeout)
    {
        inactive_timeout_ = check_range(
            Conf::EvsInactiveTimeout,
            gu::from_string<gu::datetime::Period>(val),
            gu::from_string<gu::datetime::Period>(Defaults::EvsInactiveTimeoutMin),
            gu::datetime::Period::max());
        conf_.set(Conf::EvsInactiveTimeout, gu::to_string(inactive_timeout_));
        reset_timer(T_INACTIVITY);
        return true;
    }
    else if (key == Conf::EvsKeepalivePeriod)
    {
        retrans_period_ = check_range(
            Conf::EvsKeepalivePeriod,
            gu::from_string<gu::datetime::Period>(val),
            gu::from_string<gu::datetime::Period>(Defaults::EvsRetransPeriodMin),
            gu::datetime::Period::max());
        conf_.set(Conf::EvsKeepalivePeriod, gu::to_string(retrans_period_));
        reset_timer(T_RETRANS);
        return true;
    }
    else if (key == Conf::EvsCausalKeepalivePeriod)
    {
        causal_keepalive_period_ = check_range(
            Conf::EvsCausalKeepalivePeriod,
            gu::from_string<gu::datetime::Period>(val),
            gu::datetime::Period(0),
            gu::datetime::Period::max());
        conf_.set(Conf::EvsCausalKeepalivePeriod,
                  gu::to_string(causal_keepalive_period_));
        // no timer reset here, causal keepalives don't rely on timer
        return true;
    }
    else if (key == Conf::EvsJoinRetransPeriod)
    {
        join_retrans_period_ = check_range(
            Conf::EvsJoinRetransPeriod,
            gu::from_string<gu::datetime::Period>(val),
            gu::from_string<gu::datetime::Period>(Defaults::EvsRetransPeriodMin),
            gu::datetime::Period::max());
        conf_.set(Conf::EvsJoinRetransPeriod, gu::to_string(join_retrans_period_));
        reset_timer(T_RETRANS);
        return true;
    }
    else if (key == Conf::EvsInstallTimeout)
    {
        install_timeout_ = check_range(
            Conf::EvsInstallTimeout,
            gu::from_string<gu::datetime::Period>(val),
            retrans_period_*2, inactive_timeout_ + 1);
        conf_.set(Conf::EvsInstallTimeout, gu::to_string(install_timeout_));
        reset_timer(T_INSTALL);
        return true;
    }
    else if (key == Conf::EvsUseAggregate)
    {
        use_aggregate_ = gu::from_string<bool>(val);
        conf_.set(Conf::EvsUseAggregate, gu::to_string(use_aggregate_));
        return true;
    }
    else if (key == Conf::EvsDelayMargin)
    {
        delay_margin_ = gu::from_string<gu::datetime::Period>(val);
        conf_.set(Conf::EvsDelayMargin, gu::to_string(delay_margin_));
        return true;
    }
    else if (key == Conf::EvsDelayedKeepPeriod)
    {
        delayed_keep_period_ = gu::from_string<gu::datetime::Period>(val);
        conf_.set(Conf::EvsDelayedKeepPeriod,
                  gu::to_string(delayed_keep_period_));
        return true;
    }
    else if (key == Conf::EvsEvict)
    {
        if (val.size())
        {
            UUID uuid;
            std::istringstream is(val);
            is >> uuid;
            log_info << "Evicting node " << uuid << " permanently from cluster";
            evict(uuid);
            if (state() == S_OPERATIONAL && current_view_.is_member(uuid) == true)
            {
                shift_to(S_GATHER, true);
            }
        }
        else
        {
            Protolay::EvictList::const_iterator i, i_next;
            for (i = evict_list().begin(); i != evict_list().end(); i = i_next)
            {
                i_next = i, ++i_next;
                log_info << "unevicting " << Protolay::EvictList::key(i);
                unevict(Protolay::EvictList::key(i));
            }
        }
        return true;
    }
    else if (key == Conf::EvsAutoEvict)
    {
        auto_evict_ = gu::from_string<size_t>(val);
        conf_.set(Conf::EvsAutoEvict, gu::to_string(auto_evict_));
        return true;
    }
    else if (key == Conf::EvsViewForgetTimeout ||
             key == Conf::EvsInactiveCheckPeriod)
    {
        gu_throw_error(EPERM) << "can't change value for '"
                              << key << "' during runtime";
    }
    return false;
}


void gcomm::evs::Proto::handle_get_status(gu::Status& status) const
{
    status.insert("evs_state", to_string(state_));
    status.insert("evs_repl_latency", safe_deliv_latency_.to_string());
    std::string delayed_list_str;
    for (DelayedList::const_iterator i(delayed_list_.begin());
         i != delayed_list_.end(); ++i)
    {
        if (is_evicted(i->first)              == false ||
            current_view_.is_member(i->first) == true)
        {
            delayed_list_str += i->first.full_str()
                + ":"
                + i->second.addr()
                + ":"
                + gu::to_string(i->second.state_change_cnt());
            delayed_list_str += ",";
        }
    }
    // Strip trailing comma
    if (delayed_list_str.empty() == false)
    {
        delayed_list_str.resize(delayed_list_str.size() - 1);
    }
    status.insert("evs_delayed", delayed_list_str);

    std::string evict_list_str;
    for (Protolay::EvictList::const_iterator i(evict_list().begin());
         i != evict_list().end(); )
    {
        evict_list_str += EvictList::key(i).full_str();
        if (++i != evict_list().end()) evict_list_str += ",";
    }
    status.insert("evs_evict_list", evict_list_str);

    if (info_mask_ & I_STATISTICS)
    {
        status.insert("evs_safe_hs", hs_safe_.to_string());
        status.insert("evs_causal_hs", hs_local_causal_.to_string());
        status.insert("evs_outq_avg",
                      gu::to_string(std::fabs(double(send_queue_s_)/
                                              double(n_send_queue_s_))));
        status.insert("evs_sent_user",
                      gu::to_string(sent_msgs_[Message::EVS_T_USER]));
        status.insert("evs_sent_delegate",
                      gu::to_string(sent_msgs_[Message::EVS_T_DELEGATE]));
        status.insert("evs_sent_gap",
                      gu::to_string(sent_msgs_[Message::EVS_T_GAP]));
        status.insert("evs_sent_join",
                      gu::to_string(sent_msgs_[Message::EVS_T_JOIN]));
        status.insert("evs_sent_install",
                      gu::to_string(sent_msgs_[Message::EVS_T_INSTALL]));
        status.insert("evs_sent_leave",
                      gu::to_string(sent_msgs_[Message::EVS_T_LEAVE]));
        status.insert("evs_retransmitted", gu::to_string(retrans_msgs_));
        status.insert("evs_recovered", gu::to_string(recovered_msgs_));
        status.insert("evs_deliv_safe",
                      gu::to_string(delivered_msgs_[O_SAFE]));
    }
}


std::ostream& gcomm::evs::operator<<(std::ostream& os, const Proto& p)
{
    os << "evs::proto("
       << p.self_string() << ", "
       << p.to_string(p.state()) << ") {\n";
    os << "current_view=" << p.current_view_ << ",\n";
    os << "input_map=" << *p.input_map_ << ",\n";
    os << "fifo_seq=" << p.fifo_seq_ << ",\n";
    os << "last_sent=" << p.last_sent_ << ",\n";
    os << "known:\n";
    for (NodeMap::const_iterator i(p.known_.begin()); i != p.known_.end(); ++i)
    {
        os << NodeMap::key(i) << " at "
           << p.get_address(NodeMap::key(i)) << "\n";
        os << NodeMap::value(i) << "\n";
    }
    if (p.install_message_ != 0)
        os << "install msg=" << *p.install_message_ << "\n";
    os << " }";
    return os;
}

std::string gcomm::evs::Proto::stats() const
{
    std::ostringstream os;
    os << "\n\tnodes " << current_view_.members().size();
    os << "\n\tagreed deliv hist {" << hs_agreed_ << "} ";
    os << "\n\tsafe deliv hist {" << hs_safe_ << "} ";
    os << "\n\tcaus deliv hist {" << hs_local_causal_ << "} ";
    os << "\n\toutq avg " << double(send_queue_s_)/double(n_send_queue_s_);
    os << "\n\tsent {";
    std::copy(sent_msgs_.begin(), sent_msgs_.end(),
         std::ostream_iterator<long long int>(os, ","));
    os << "}\n\tsent per sec {";
    const double norm(double(gu::datetime::Date::now().get_utc()
                             - last_stats_report_.get_utc())/gu::datetime::Sec);
    std::vector<double> result(7, norm);
    std::transform(sent_msgs_.begin(), sent_msgs_.end(),
                   result.begin(), result.begin(), std::divides<double>());
    std::copy(result.begin(), result.end(),
              std::ostream_iterator<double>(os, ","));
    os << "}\n\trecvd { ";
    std::copy(recvd_msgs_.begin(), recvd_msgs_.end(),
              std::ostream_iterator<long long int>(os, ","));
    os << "}\n\trecvd per sec {";
    std::fill(result.begin(), result.end(), norm);
    std::transform(recvd_msgs_.begin(), recvd_msgs_.end(),
                   result.begin(), result.begin(), std::divides<double>());
    std::copy(result.begin(), result.end(),
              std::ostream_iterator<double>(os, ","));
    os << "}\n\tretransmitted " << retrans_msgs_ << " ";
    os << "\n\trecovered " << recovered_msgs_;
    os << "\n\tdelivered {";
    std::copy(delivered_msgs_.begin(), delivered_msgs_.end(),
              std::ostream_iterator<long long int>(os, ", "));
    os << "}\n\teff(delivered/sent) " <<
        double(accumulate(delivered_msgs_.begin() + 1,
                          delivered_msgs_.begin() + O_SAFE + 1, 0))
        /double(accumulate(sent_msgs_.begin(), sent_msgs_.end(), 0));
    return os.str();
}

void gcomm::evs::Proto::reset_stats()
{
    hs_agreed_.clear();
    hs_safe_.clear();
    hs_local_causal_.clear();
    safe_deliv_latency_.clear();
    send_queue_s_ = 0;
    n_send_queue_s_ = 0;
    last_stats_report_ = gu::datetime::Date::now();
}


bool gcomm::evs::Proto::is_msg_from_previous_view(const Message& msg)
{
    ViewList::const_iterator i;
    if ((i = previous_views_.find(msg.source_view_id()))
        != previous_views_.end())
    {
        evs_log_debug(D_FOREIGN_MSGS) << " message " << msg
                                      << " from previous view " << i->first;
        return true;
    }

    // If node is in current view, check message source view seq, if it is
    // smaller than current view seq then the message is also from some
    // previous (but unknown to us) view
    NodeList::const_iterator ni(current_view_.members().find(msg.source()));
    if (ni != current_view_.members().end())
    {
        if (msg.source_view_id().seq() <
            current_view_.id().seq())
        {
            log_warn << "stale message from unknown origin " << msg;
            return true;
        }
    }

    return false;
}


void gcomm::evs::Proto::handle_inactivity_timer()
{
    gu_trace(check_inactive());
    gu_trace(cleanup_views());
    gu_trace(cleanup_evicted());
}


void gcomm::evs::Proto::handle_retrans_timer()
{
    evs_log_debug(D_TIMERS) << "retrans timer";
    if (state() == S_GATHER)
    {
        if (install_message_ != 0)
        {
            // Retransmit install message if representative and all commit
            // gaps have not been received yet.
            if (is_all_committed()         == false &&
                install_message_->source() == uuid())
            {
                evs_log_debug(D_INSTALL_MSGS) << "retrans install";
                gu::Buffer buf;
                install_message_->set_flags(
                    install_message_->flags() | Message::F_RETRANS);
                (void)serialize(*install_message_, buf);
                Datagram dg(buf);
                // Must not be sent as delegate, newly joining node
                // will filter them out in handle_msg().
                gu_trace(send_down(dg, ProtoDownMeta()));
            }
            evs_log_debug(D_GAP_MSGS) << "resend commit gap";
            // Resend commit gap
            gu_trace(send_gap(EVS_CALLER, UUID::nil(),
                              install_message_->install_view_id(),
                              Range(), true));
        }
        else
        {
            evs_log_debug(D_JOIN_MSGS) << "retrans join";
            gu_trace(send_join(true));
        }
    }
    else if (state() == S_INSTALL)
    {
        gcomm_assert(install_message_ != 0);
        gu_trace(send_gap(EVS_CALLER, UUID::nil(),
                          install_message_->install_view_id(),
                          Range(), true));
        gu_trace(send_gap(EVS_CALLER, UUID::nil(),
                          install_message_->install_view_id(),
                          Range()));
    }
    else if (state() == S_OPERATIONAL)
    {
        const seqno_t prev_last_sent(last_sent_);
        evs_log_debug(D_TIMERS) << "send user timer, last_sent=" << last_sent_;
        Datagram dg;
        gu_trace((void)send_user(dg, 0xff, O_DROP, -1, -1));
        if (prev_last_sent == last_sent_)
        {
            log_warn << "could not send keepalive";
        }
    }
    else if (state() == S_LEAVING)
    {
        evs_log_debug(D_TIMERS) << "send leave timer";
        profile_enter(send_leave_prof_);
        send_leave(false);
        profile_leave(send_leave_prof_);
    }
}

void gcomm::evs::Proto::isolate(gu::datetime::Period period)
{
    isolation_end_ = gu::datetime::Date::now() + period;
}


void gcomm::evs::Proto::handle_install_timer()
{
    gcomm_assert(state() == S_GATHER || state() == S_INSTALL);
    log_warn << self_string() << " install timer expired";

    bool is_cons(consensus_.is_consensus());
    bool is_repr(is_representative(uuid()));
    evs_log_info(I_STATE) << "before inspection:";
    evs_log_info(I_STATE) << "consensus: " << is_cons;
    evs_log_info(I_STATE) << "repr     : " << is_repr;
    evs_log_info(I_STATE) << "state dump for diagnosis:";
    std::cerr << *this << std::endl;

    if (install_timeout_count_ < max_install_timeouts_ )
    {
        // before reaching max_install_timeouts, declare only inconsistent
        // nodes as inactive
        for (NodeMap::iterator i = known_.begin(); i != known_.end(); ++i)
        {
            const UUID& node_uuid(NodeMap::key(i));
            const Node& node(NodeMap::value(i));
            if (node_uuid != uuid() &&
                (node.join_message() == 0 ||
                 consensus_.is_consistent(*node.join_message()) == false))
            {
                evs_log_info(I_STATE)
                    << " setting source " << NodeMap::key(i)
                    << " as inactive due to expired install timer";
                set_inactive(NodeMap::key(i));
            }
        }
    }
    else if (install_timeout_count_ == max_install_timeouts_)
    {
        // max install timeouts reached, declare all other nodes
        // as inactive
        for (NodeMap::iterator i = known_.begin(); i != known_.end(); ++i)
        {
            if (NodeMap::key(i) != uuid())
            {
                evs_log_info(I_STATE)
                    << " setting source " << NodeMap::key(i)
                    << " as inactive due to expired install timer";
                set_inactive(NodeMap::key(i));
            }
        }
        log_info << "max install timeouts reached, will isolate node "
                 << "for " << suspect_timeout_ + inactive_timeout_;
        isolate(suspect_timeout_ + inactive_timeout_);
    }
    else if (install_timeout_count_ > max_install_timeouts_)
    {
        log_info << "going to give up, state dump for diagnosis:";
        std::cerr << *this << std::endl;
        gu_throw_fatal << self_string()
                       << " failed to form singleton view after exceeding "
                       << "max_install_timeouts " << max_install_timeouts_
                       << ", giving up";
    }


    if (install_message_ != 0)
    {
        for (NodeMap::iterator i = known_.begin(); i != known_.end(); ++i)
        {
            if (NodeMap::value(i).committed() == false)
            {
                log_info << self_string() << " node " << NodeMap::key(i)
                         << " failed to commit for install message, "
                         << "declaring inactive";
                if (NodeMap::key(i) != uuid())
                {
                    set_inactive(NodeMap::key(i));
                }
            }
        }
    }
    else
    {
        log_info << "no install message received";
    }

    shift_to(S_GATHER, true);
    is_cons = consensus_.is_consensus();
    is_repr = is_representative(uuid());
    evs_log_info(I_STATE) << "after inspection:";
    evs_log_info(I_STATE) << "consensus: " << is_cons;
    evs_log_info(I_STATE) << "repr     : " << is_repr;
    if (is_cons == true && is_repr == true)
    {
        send_install(EVS_CALLER);
    }
    install_timeout_count_++;
}

void gcomm::evs::Proto::handle_stats_timer()
{
    reset_stats();
#ifdef GCOMM_PROFILE
    evs_log_info(I_PROFILING) << "\nprofiles:\n";
    evs_log_info(I_PROFILING) << send_user_prof_    << "\n";
    evs_log_info(I_PROFILING) << send_gap_prof_     << "\n";
    evs_log_info(I_PROFILING) << send_join_prof_    << "\n";
    evs_log_info(I_PROFILING) << send_install_prof_ << "\n";
    evs_log_info(I_PROFILING) << send_leave_prof_   << "\n";
    evs_log_info(I_PROFILING) << consistent_prof_   << "\n";
    evs_log_info(I_PROFILING) << consensus_prof_    << "\n";
    evs_log_info(I_PROFILING) << shift_to_prof_     << "\n";
    evs_log_info(I_PROFILING) << input_map_prof_    << "\n";
    evs_log_info(I_PROFILING) << delivery_prof_     << "\n";
#endif // GCOMM_PROFILE
}



class TimerSelectOp
{
public:
    TimerSelectOp(const gcomm::evs::Proto::Timer t_) : t(t_) { }
    bool operator()(const gcomm::evs::Proto::TimerList::value_type& vt) const
    {
        return (gcomm::evs::Proto::TimerList::value(vt) == t);
    }
private:
    gcomm::evs::Proto::Timer const t;
};


gu::datetime::Date gcomm::evs::Proto::next_expiration(const Timer t) const
{
    gcomm_assert(state() != S_CLOSED);
    gu::datetime::Date now(gu::datetime::Date::now());
    switch (t)
    {
    case T_INACTIVITY:
        return (now + inactive_check_period_);
    case T_RETRANS:
        switch (state())
        {
        case S_OPERATIONAL:
        case S_LEAVING:
            return (now + retrans_period_);
        case S_GATHER:
        case S_INSTALL:
            return (now + join_retrans_period_);
        default:
            gu_throw_fatal;
        }
    case T_INSTALL:

        switch (state())
        {
        case S_GATHER:
        case S_INSTALL:
            return (now + install_timeout_);
        default:
            return gu::datetime::Date::max();
        }
    case T_STATS:
        return (now + stats_report_period_);
    }
    gu_throw_fatal;
}


void timer_list_erase_by_type(gcomm::evs::Proto::TimerList& timer_list,
                              gcomm::evs::Proto::Timer timer)
{
    gcomm::evs::Proto::TimerList::iterator i, i_next;
    for (i = timer_list.begin(); i != timer_list.end(); i = i_next)
    {
        i_next = i, ++i_next;
        if (gcomm::evs::Proto::TimerList::value(i) == timer)
        {
            timer_list.erase(i);
        }
    }
}

void gcomm::evs::Proto::reset_timer(Timer t)
{
    timer_list_erase_by_type(timers_, t);
    timers_.insert(std::make_pair(next_expiration(t), t));
}

void gcomm::evs::Proto::cancel_timer(Timer t)
{
    timer_list_erase_by_type(timers_, t);
}

gu::datetime::Date gcomm::evs::Proto::handle_timers()
{
    gu::datetime::Date now(gu::datetime::Date::now());

    while (timers_.empty() == false &&
           TimerList::key(timers_.begin()) <= now)
    {
        Timer t(TimerList::value(timers_.begin()));
        timers_.erase(timers_.begin());
        switch (t)
        {
        case T_INACTIVITY:
            handle_inactivity_timer();
            break;
        case T_RETRANS:
            handle_retrans_timer();
            break;
        case T_INSTALL:
            handle_install_timer();
            break;
        case T_STATS:
            handle_stats_timer();
            break;
        }
        if (state() == S_CLOSED)
        {
            return gu::datetime::Date::max();
        }
        reset_timer(t);
    }

    if (timers_.empty() == true)
    {
        evs_log_debug(D_TIMERS) << "no timers set";
        return gu::datetime::Date::max();
    }
    return TimerList::key(timers_.begin());
}


void gcomm::evs::Proto::check_inactive()
{
    const gu::datetime::Date now(gu::datetime::Date::now());
    if (last_inactive_check_ + inactive_check_period_*3 < now)
    {
        log_warn << "last inactive check more than " << inactive_check_period_*3
                 << " ago (" << (now - last_inactive_check_)
                 << "), skipping check";
        last_inactive_check_ = now;
        return;
    }

    NodeMap::value(self_i_).set_tstamp(gu::datetime::Date::now());
    std::for_each(known_.begin(), known_.end(), InspectNode());

    bool has_inactive(false);
    size_t n_suspected(0);
    bool do_send_delayed_list(false);

    // Iterate over known nodes and check inactive/suspected/delayed status
    for (NodeMap::iterator i(known_.begin()); i != known_.end(); ++i)
    {
        if (i == self_i_) continue; // No need to check self

        const UUID& node_uuid(NodeMap::key(i));
        Node& node(NodeMap::value(i));
        if (node_uuid                  != uuid()    &&
            (node.is_inactive()     == true      ||
             node.is_suspected()    == true           ))
        {
            if (node.operational() == true && node.is_inactive() == true)
            {
                log_info << self_string() << " detected inactive node: " << node_uuid;
            }
            else if (node.is_suspected() == true && node.is_inactive() == false)
            {
                log_info << self_string() << " suspecting node: " << node_uuid;
            }
            if (node.is_inactive() == true)
            {
                set_inactive(node_uuid);
            }
            if (node.is_suspected() == true && node.operational() == true)
            {
                ++n_suspected;
                if (node.join_message() == 0)
                {
                    log_info << self_string()
                             << " suspected node without join message, declaring inactive";
                    set_inactive(node_uuid);
                }
            }
            has_inactive = true;
        }

        DelayedList::iterator dli(delayed_list_.find(node_uuid));
        if (node.seen_tstamp() + retrans_period_ + delay_margin_ <= now)
        {
            if (node.index() != std::numeric_limits<size_t>::max())
            {
                // Delayed node in group, check input map state and request
                // message recovery if necessary
                Range range(input_map_->range(node.index()));
                evs_log_info(I_STATE) << "delayed "
                                      << node_uuid << " requesting range "
                                      << Range(range.lu(), last_sent_);
                if (last_sent_ >= range.lu())
                {
                    // Request recovering message from all nodes (indicated
                    // by last arg) to increase probablity of receiving the
                    // message.
                    gu_trace(send_gap(EVS_CALLER, node_uuid, current_view_.id(),
                                      Range(range.lu(), last_sent_),
                                      false, true));
                }
            }

            if (dli == delayed_list_.end())
            {
                delayed_list_.insert(
                    std::make_pair(node_uuid,
                                   DelayedEntry(get_address(node_uuid))));
            }
            else
            {
                dli->second.set_tstamp(now);
                dli->second.set_state(DelayedEntry::S_DELAYED,
                                      delayed_keep_period_, now);
                evs_log_debug(D_STATE) << "set '" << dli->first <<
                        "' delayed state to S_DELAYED , cnt = " <<
                        dli->second.state_change_cnt();
                // todo(dirlt): make threshold as a configurable variable ?
                if (dli->second.state_change_cnt() > 0)
                {
                    do_send_delayed_list = true;
                }
            }
        }
        else if (dli != delayed_list_.end())
        {
            const size_t prev_cnt(dli->second.state_change_cnt());
            dli->second.set_state(DelayedEntry::S_OK,
                                  delayed_keep_period_, now);
            if (prev_cnt != dli->second.state_change_cnt())
            {
                dli->second.set_tstamp(now);
            }
            evs_log_debug(D_STATE) << "set '" << dli->first <<
                    "' delayed state to S_OK. prev_cnt = " << prev_cnt <<
                    ", cur_cnt = " << dli->second.state_change_cnt();
            if (dli->second.state_change_cnt() > 0)
            {
                do_send_delayed_list = true;
            }
        }
    }

    // Clean up delayed list and evict list messages
    {
        DelayedList::iterator i, i_next;
        for (i = delayed_list_.begin(); i != delayed_list_.end(); i = i_next)
        {
            i_next = i, ++i_next;
            // State change count has decayed back to zero
            // or node is already evicted and not in the current view
            // anymore.
            if ((i->second.state_change_cnt() == 0 &&
                 i->second.state() == DelayedEntry::S_OK) ||
                (is_evicted(i->first) == true &&
                 current_view_.is_member(i->first) == false))
            {
                log_debug << "remove '" << i->first << "' from delayed_list";
                delayed_list_.erase(i);
            }
        }
        for (NodeMap::iterator i(known_.begin()); i != known_.end(); ++i)
        {
            Node& node(NodeMap::value(i));
            const DelayedListMessage* const elm(node.delayed_list_message());
            if (elm != 0 && elm->tstamp() + delayed_keep_period_ < now)
            {
                log_debug << "discarding expired elm from " << elm->source();
                node.set_delayed_list_message(0);
            }
        }
    }

    if (current_view_.version() > 0 &&
        do_send_delayed_list == true && auto_evict_ > 0)
    {
        send_delayed_list();
    }

    // All other nodes are under suspicion, set all others as inactive.
    // This will speed up recovery when this node has been isolated from
    // other group. Note that this should be done only if known size is
    // greater than 2 in order to avoid immediate split brain.
    if (known_.size() > 2 && n_suspected + 1 == known_.size())
    {
        for (NodeMap::iterator i = known_.begin(); i != known_.end(); ++i)
        {
            if (NodeMap::key(i) != uuid())
            {
                evs_log_info(I_STATE)
                    << " setting source " << NodeMap::key(i)
                    << " inactive (other nodes under suspicion)";
                set_inactive(NodeMap::key(i));
            }
        }
    }

    if (has_inactive == true && state() == S_OPERATIONAL)
    {
        profile_enter(shift_to_prof_);
        gu_trace(shift_to(S_GATHER, true));
        profile_leave(shift_to_prof_);
    }
    else if (has_inactive    == true &&
             state()     == S_LEAVING &&
             n_operational() == 1)
    {
        profile_enter(shift_to_prof_);
        gu_trace(shift_to(S_CLOSED));
        profile_leave(shift_to_prof_);
    }

    last_inactive_check_ = now;


    // Check if isolation period has ended
    if (isolation_end_ != gu::datetime::Date::zero() &&
        isolation_end_ <= now)
    {
        log_info << "ending isolation";
        isolation_end_ = gu::datetime::Date::zero();
    }
}


void gcomm::evs::Proto::set_inactive(const UUID& node_uuid)
{
    NodeMap::iterator i;
    gcomm_assert(node_uuid != uuid());
    gu_trace(i = known_.find_checked(node_uuid));
    evs_log_debug(D_STATE) << "setting " << node_uuid << " inactive";
    Node& node(NodeMap::value(i));
    node.set_tstamp(gu::datetime::Date::zero());
    node.set_join_message(0);
    // node.set_leave_message(0);
    node.set_operational(false);
}


bool gcomm::evs::Proto::is_inactive(const UUID& uuid) const
{
    NodeMap::const_iterator i;
    gu_trace(i = known_.find_checked(uuid));
    const Node& node(NodeMap::value(i));
    return (node.operational() == false);
}

void gcomm::evs::Proto::cleanup_foreign(const InstallMessage& im)
{
    NodeMap::iterator i, i_next;
    for (i = known_.begin(); i != known_.end(); i = i_next)
    {
        const UUID& uuid(NodeMap::key(i));
        i_next = i, ++i_next;
        const MessageNodeList::const_iterator mni(im.node_list().find(uuid));
        if (mni == im.node_list().end() ||
            MessageNodeList::value(mni).operational() == false)
        {
            known_.erase(i);
        }
    }
}

void gcomm::evs::Proto::cleanup_views()
{
    gu::datetime::Date now(gu::datetime::Date::now());

    ViewList::iterator i, i_next;
    for (i = previous_views_.begin(); i != previous_views_.end(); i = i_next)
    {
        i_next = i, ++i_next;
        if (i->second + view_forget_timeout_ <= now)
        {
            evs_log_debug(D_STATE) << " erasing view: " << i->first;
            previous_views_.erase(i);
        }
    }
}

void gcomm::evs::Proto::cleanup_evicted()
{
    gu::datetime::Date now(gu::datetime::Date::now());
    Protolay::EvictList::const_iterator i, i_next;
    for (i = evict_list().begin(); i != evict_list().end(); i = i_next)
    {
        i_next = i, ++i_next;
        if (Protolay::EvictList::value(i) + view_forget_timeout_ <= now)
        {
            log_info << "unevicting " << Protolay::EvictList::key(i);
            unevict(Protolay::EvictList::key(i));
        }
    }
}

size_t gcomm::evs::Proto::n_operational() const
{
    NodeMap::const_iterator i;
    size_t ret = 0;
    for (i = known_.begin(); i != known_.end(); ++i) {
        if (i->second.operational() == true)
            ret++;
    }
    return ret;
}

void gcomm::evs::Proto::deliver_reg_view(const InstallMessage& im,
                                         const View& prev_view)
{

    View view(im.version(), im.install_view_id());
    for (MessageNodeList::const_iterator i(im.node_list().begin());
         i != im.node_list().end(); ++i)
    {
        const UUID& uuid(MessageNodeList::key(i));
        const MessageNode& mn(MessageNodeList::value(i));

        // 1) Operational nodes will be members of new view
        // 2) Operational nodes that were not present in previous
        //    view are going also to joined set
        // 3) Leaving nodes go to left set
        // 4) All other nodes present in previous view but not in
        //    member of left set are considered partitioned
        if (mn.operational() == true)
        {
            view.add_member(uuid, mn.segment());
            if (prev_view.is_member(uuid) == false)
            {
                view.add_joined(uuid, mn.segment());
            }
        }
        else if (mn.leaving() == true)
        {
            view.add_left(uuid, mn.segment());
        }
        else
        {
            // Partitioned set is constructed after this loop
        }

        // If node has been evicted, it should have been added to
        // evicted list via JOIN messages.
        assert(mn.evicted() == false || is_evicted(uuid) == true);
    }

    // Loop over previous view and add each node not in new view
    // member of left set as partitioned.
    for (NodeList::const_iterator i(prev_view.members().begin());
         i != prev_view.members().end(); ++i)
    {
        const UUID& uuid(NodeList::key(i));
        const gcomm::Node& mn(NodeList::value(i));
        if (view.is_member(uuid)  == false &&
            view.is_leaving(uuid) == false)
        {
            view.add_partitioned(uuid, mn.segment());
        }
    }

    evs_log_info(I_VIEWS) << "delivering view " << view;

    // This node must be a member of the view it delivers and
    // view id UUID must be of one of the members.
    gcomm_assert(view.is_member(uuid()) == true);
    gcomm_assert(view.is_member(view.id().uuid()) == true)
        << "view id UUID " << view.id().uuid()
        << " not found from reg view members "
        << view.members()
        << " must abort to avoid possibility of two groups "
        << "with the same view id";

    set_stable_view(view);
    ProtoUpMeta up_meta(UUID::nil(), ViewId(), &view);
    send_up(Datagram(), up_meta);
}

void gcomm::evs::Proto::deliver_trans_view(const InstallMessage& im,
                                           const View& curr_view)
{

    // Trans view is intersection of members in curr_view
    // and members going to be in the next view that come from
    // curr_view according to install message

    View view(current_view_.version(),
              ViewId(V_TRANS,
                     curr_view.id().uuid(),
                     curr_view.id().seq()));

    for (MessageNodeList::const_iterator i(im.node_list().begin());
         i != im.node_list().end(); ++i)
    {
        const UUID& uuid(MessageNodeList::key(i));
        const MessageNode& mn(MessageNodeList::value(i));

        if (curr_view.id()            == mn.view_id() &&
            curr_view.is_member(uuid) == true)
        {
            // 1) Operational nodes go to next view
            // 2) Leaving nodes go to left set
            // 3) All other nodes present in previous view but not in
            //    member of left set are considered partitioned
            if (mn.operational() == true)
            {
                view.add_member(uuid, mn.segment());
            }
            else if (mn.leaving() == true)
            {
                view.add_left(uuid, mn.segment());
            }
            else
            {
                // Partitioned set is constructed after this loop
            }
        }
    }

    // Loop over current view and add each node not in new view
    // member of left set as partitioned.
    for (NodeList::const_iterator i(curr_view.members().begin());
         i != curr_view.members().end(); ++i)
    {
        const UUID& uuid(NodeList::key(i));
        const gcomm::Node& mn(NodeList::value(i));

        if (view.is_member(uuid)  == false &&
            view.is_leaving(uuid) == false)
        {
            view.add_partitioned(uuid, mn.segment());
        }
    }

    // This node must be a member of the view it delivers and
    // if the view is the last transitional, view must have
    // exactly one member and no-one in left set.
    gcomm_assert(view.is_member(uuid()) == true);

    evs_log_info(I_VIEWS) << " delivering view " << view;

    ProtoUpMeta up_meta(UUID::nil(), ViewId(), &view);
    gu_trace(send_up(Datagram(), up_meta));
}


void gcomm::evs::Proto::deliver_empty_view()
{
    View view(0, V_REG);

    evs_log_info(I_VIEWS) << "delivering view " << view;

    ProtoUpMeta up_meta(UUID::nil(), ViewId(), &view);
    send_up(Datagram(), up_meta);
}


void gcomm::evs::Proto::setall_committed(bool val)
{
    for (NodeMap::iterator i = known_.begin(); i != known_.end(); ++i)
    {
        NodeMap::value(i).set_committed(val);
    }
}

// Check if commit gaps from all known nodes found from install message have
// been seen.
bool gcomm::evs::Proto::is_all_committed() const
{
    gcomm_assert(install_message_ != 0);
    for (NodeMap::const_iterator i(known_.begin()); i != known_.end(); ++i)
    {
        const UUID& uuid(NodeMap::key(i));
        const Node& inst(NodeMap::value(i));
        if (install_message_->node_list().find(uuid) !=
            install_message_->node_list().end()               &&
            inst.operational()                        == true &&
            inst.committed()                          == false)
        {
            return false;
        }
    }
    return true;
}

void gcomm::evs::Proto::setall_installed(bool val)
{
    for (NodeMap::iterator i = known_.begin(); i != known_.end(); ++i)
    {
        NodeMap::value(i).set_installed(val);
    }
}

// Check if gaps from new view from all known nodes found from install
// message have been seen.
bool gcomm::evs::Proto::is_all_installed() const
{
    gcomm_assert(install_message_ != 0);
    for (NodeMap::const_iterator i = known_.begin(); i != known_.end(); ++i)
    {
        const UUID& uuid(NodeMap::key(i));
        const Node& inst(NodeMap::value(i));
        if (install_message_->node_list().find(uuid) !=
            install_message_->node_list().end()              &&
            inst.operational()                       == true &&
            inst.installed()                         == false)
        {
            return false;
        }
    }
    return true;
}

void gcomm::evs::Proto::cleanup_joins()
{
    for (NodeMap::iterator i = known_.begin(); i != known_.end(); ++i)
    {
        NodeMap::value(i).set_join_message(0);
    }
}

bool gcomm::evs::Proto::is_representative(const UUID& uuid) const
{
    for (NodeMap::const_iterator i = known_.begin(); i != known_.end(); ++i)
    {
        if (NodeMap::value(i).operational() == true &&
            NodeMap::value(i).is_inactive() == false)
        {
            assert(NodeMap::value(i).leave_message() == 0);
            if (NodeMap::value(i).leave_message() != 0)
            {
                log_warn << "operational node " << NodeMap::key(i)
                         << " with leave message: " << NodeMap::value(i);
                continue;
            }
            return (uuid == NodeMap::key(i));
        }
    }

    return false;
}

bool gcomm::evs::Proto::is_all_suspected(const UUID& uuid) const
{
    for (NodeMap::const_iterator i = known_.begin(); i != known_.end(); ++i)
    {
        const Node& node(NodeMap::value(i));
        if (node.operational() == true) {
            const JoinMessage* jm(node.join_message());
            if (!jm) return false;
            const MessageNodeList::const_iterator j(jm->node_list().find(uuid));
            if (!(j != jm->node_list().end() &&
                  MessageNodeList::value(j).suspected()))
                return false;
        }
    }
    return true;
}



/////////////////////////////////////////////////////////////////////////////
// Message sending
/////////////////////////////////////////////////////////////////////////////




bool gcomm::evs::Proto::is_flow_control(const seqno_t seq, const seqno_t win) const
{
    gcomm_assert(seq != -1 && win != -1);

    const seqno_t base(input_map_->safe_seq());
    if (seq > base + win)
    {
        return true;
    }
    return false;
}

int gcomm::evs::Proto::send_user(Datagram& dg,
                                 uint8_t const user_type,
                                 Order  const order,
                                 seqno_t const win,
                                 seqno_t const up_to_seqno,
                                 size_t const n_aggregated)
{
    assert(state() == S_LEAVING ||
           state() == S_GATHER ||
           state() == S_OPERATIONAL);
    assert(dg.offset() == 0);
    assert(n_aggregated == 1 || output_.size() >= n_aggregated);

    gcomm_assert(up_to_seqno == -1 || up_to_seqno >= last_sent_);
    gcomm_assert(up_to_seqno == -1 || win == -1);

    int ret;
    const seqno_t seq(last_sent_ + 1);

    if (win                       != -1   &&
        is_flow_control(seq, win) == true)
    {
        return EAGAIN;
    }

    // seq_range max 0xff because of Message seq_range_ field limitation
    seqno_t seq_range(
        std::min(up_to_seqno == -1 ? 0 : up_to_seqno - seq,
                 evs::seqno_t(0xff)));
    seqno_t last_msg_seq(seq + seq_range);
    uint8_t flags;

    // If output queue wont contain messages after this patch,
    // up_to_seqno is given (msg completion) or flow contol would kick in
    // at next batch, don't set F_MSG_MORE.
    if (output_.size() <= n_aggregated ||
        up_to_seqno != -1 ||
        (win != -1 && is_flow_control(last_msg_seq + 1, win) == true))
    {
        flags = 0;
    }
    else
    {
        flags = Message::F_MSG_MORE;
    }
    if (n_aggregated > 1)
    {
        flags |= Message::F_AGGREGATE;
    }

    // Maximize seq range in the case next message batch won't be sent
    // immediately.
    if ((flags & Message::F_MSG_MORE) == 0 && up_to_seqno == -1)
    {
        seq_range = input_map_->max_hs() - seq;
        seq_range = std::max(static_cast<seqno_t>(0), seq_range);
        seq_range = std::min(static_cast<seqno_t>(0xff), seq_range);
        if (seq_range != 0)
        {
            log_debug << "adjusted seq range to: " << seq_range;
            last_msg_seq = seq + seq_range;
        }
    }

    gcomm_assert(last_msg_seq >= seq && last_msg_seq - seq <= 0xff);
    gcomm_assert(seq_range >= 0 && seq_range <= 0xff);

    UserMessage msg(version_,
                    uuid(),
                    current_view_.id(),
                    seq,
                    input_map_->aru_seq(),
                    seq_range,
                    order,
                    ++fifo_seq_,
                    user_type,
                    flags);

    // Insert first to input map to determine correct aru seq
    Range range;
    gu_trace(range = input_map_->insert(NodeMap::value(self_i_).index(),
                                       msg, dg));

    gcomm_assert(range.hs() == last_msg_seq)
        << msg << " " << *input_map_ << " " << *this;

    last_sent_ = last_msg_seq;
    assert(range.hs() == last_sent_);

    update_im_safe_seq(NodeMap::value(self_i_).index(),
                       input_map_->aru_seq());

    msg.set_aru_seq(input_map_->aru_seq());
    evs_log_debug(D_USER_MSGS) << " sending " << msg;
    gu_trace(push_header(msg, dg));
    if ((ret = send_down(dg, ProtoDownMeta())) != 0)
    {
        log_debug << "send failed: "  << strerror(ret);
    }
    gu_trace(pop_header(msg, dg));
    sent_msgs_[Message::EVS_T_USER]++;

    if (delivering_ == false)
    {
        gu_trace(deliver());
        gu_trace(deliver_local());
    }

    return 0;
}

size_t gcomm::evs::Proto::aggregate_len() const
{
    bool is_aggregate(false);
    size_t ret(0);
    AggregateMessage am;
    std::deque<std::pair<Datagram, ProtoDownMeta> >::const_iterator
        i(output_.begin());
    const Order ord(i->second.order());
    ret += i->first.len() + am.serial_size();
    for (++i; i != output_.end() && i->second.order() == ord; ++i)
    {
        if (ret + i->first.len() + am.serial_size() <= mtu())
        {
            ret += i->first.len() + am.serial_size();
            is_aggregate = true;
        }
        else
        {
            break;
        }
    }
    evs_log_debug(D_USER_MSGS) << "is aggregate " << is_aggregate << " ret " << ret;
    return (is_aggregate == true ? ret : 0);
}

int gcomm::evs::Proto::send_user(const seqno_t win)
{
    gcomm_assert(output_.empty() == false);
    gcomm_assert(state() == S_OPERATIONAL);
    gcomm_assert(win <= send_window_);
    int ret;
    size_t alen;
    if (use_aggregate_ == true && (alen = aggregate_len()) > 0)
    {
        // Messages can be aggregated into single message
        send_buf_.resize(alen);
        size_t offset(0);
        size_t n(0);

        std::deque<std::pair<Datagram, ProtoDownMeta> >::iterator
            i(output_.begin());
        Order ord(i->second.order());
        while ((alen > 0 && i != output_.end()))
        {
            const Datagram& dg(i->first);
            const ProtoDownMeta dm(i->second);
            AggregateMessage am(0, dg.len(), dm.user_type());
            gcomm_assert(alen >= dg.len() + am.serial_size());

            gu_trace(offset = am.serialize(&send_buf_[0],
                                           send_buf_.size(), offset));
            std::copy(dg.header() + dg.header_offset(),
                      dg.header() + dg.header_size(),
                      &send_buf_[0] + offset);
            offset += (dg.header_len());
            std::copy(dg.payload().begin(), dg.payload().end(),
                      &send_buf_[0] + offset);
            offset += dg.payload().size();
            alen -= dg.len() + am.serial_size();
            ++n;
            ++i;
        }
        Datagram dg(gu::SharedBuffer(new gu::Buffer(send_buf_.begin(),
                                                        send_buf_.end())));
        if ((ret = send_user(dg, 0xff, ord, win, -1, n)) == 0)
        {
            while (n-- > 0)
            {
                output_.pop_front();
            }
        }
    }
    else
    {
        std::pair<Datagram, ProtoDownMeta> wb(output_.front());
        if ((ret = send_user(wb.first,
                             wb.second.user_type(),
                             wb.second.order(),
                             win,
                             -1)) == 0)
        {
            output_.pop_front();
        }
    }
    return ret;
}


void gcomm::evs::Proto::complete_user(const seqno_t high_seq)
{
    gcomm_assert(state() == S_OPERATIONAL || state() == S_GATHER);

    evs_log_debug(D_USER_MSGS) << "completing seqno to " << high_seq;;

    Datagram wb;
    int err;
    profile_enter(send_user_prof_);
    err = send_user(wb, 0xff, O_DROP, -1, high_seq);
    profile_leave(send_user_prof_);
    if (err != 0)
    {
        log_debug << "failed to send completing msg " << strerror(err)
                  << " seq=" << high_seq << " send_window=" << send_window_
                  << " last_sent=" << last_sent_;
    }

}


int gcomm::evs::Proto::send_delegate(Datagram& wb)
{
    DelegateMessage dm(version_, uuid(), current_view_.id(),
                       ++fifo_seq_);
    push_header(dm, wb);
    int ret = send_down(wb, ProtoDownMeta());
    pop_header(dm, wb);
    sent_msgs_[Message::EVS_T_DELEGATE]++;
    return ret;
}


void gcomm::evs::Proto::send_gap(EVS_CALLER_ARG,
                                 const UUID&   range_uuid,
                                 const ViewId& source_view_id,
                                 const Range   range,
                                 const bool    commit,
                                 const bool    req_all)
{
    gcomm_assert((commit == false && source_view_id == current_view_.id())
                 || install_message_ != 0);
    // TODO: Investigate if gap sending can be somehow limited,
    // message loss happen most probably during congestion and
    // flooding network with gap messages won't probably make
    // conditions better

    uint8_t flags(0);
    if (commit == true) flags |= Message::F_COMMIT;
    if (req_all) flags |= Message::F_RETRANS;

    GapMessage gm(version_,
                  uuid(),
                  source_view_id,
                  (source_view_id == current_view_.id() ? last_sent_ :
                   (commit == true ? install_message_->fifo_seq() : -1)),
                  (source_view_id == current_view_.id() ?
                   input_map_->aru_seq() : -1),
                  ++fifo_seq_,
                  range_uuid,
                  range,
                  flags);

    evs_log_debug(D_GAP_MSGS) << EVS_LOG_METHOD << gm;
    gu::Buffer buf;
    serialize(gm, buf);
    Datagram dg(buf);
    int err = send_down(dg, ProtoDownMeta());
    if (err != 0)
    {
        log_debug << "send failed: " << strerror(err);
    }
    sent_msgs_[Message::EVS_T_GAP]++;
    gu_trace(handle_gap(gm, self_i_));
}


void gcomm::evs::Proto::populate_node_list(MessageNodeList* node_list) const
{
    for (NodeMap::const_iterator i = known_.begin(); i != known_.end(); ++i)
    {
        const UUID& node_uuid(NodeMap::key(i));
        const Node& node(NodeMap::value(i));
        MessageNode mnode(node.operational(), node.suspected(),
                          is_evicted(node_uuid));
        if (node_uuid != uuid())
        {
            const JoinMessage* jm(node.join_message());
            const LeaveMessage* lm(node.leave_message());

            //
            if (jm != 0)
            {
                const ViewId& nsv(jm->source_view_id());
                const MessageNode& mn(MessageNodeList::value(jm->node_list().find_checked(node_uuid)));
                mnode = MessageNode(node.operational(),
                                    node.is_suspected(),
                                    node.segment(),
                                    is_evicted(node_uuid),
                                    -1,
                                    jm->source_view_id(),
                                    (nsv == current_view_.id() ?
                                     input_map_->safe_seq(node.index()) :
                                     mn.safe_seq()),
                                    (nsv == current_view_.id() ?
                                     input_map_->range(node.index()) :
                                     mn.im_range()));
            }
            else if (lm != 0)
            {
                const ViewId& nsv(lm->source_view_id());
                mnode = MessageNode(node.operational(),
                                    node.is_suspected(),
                                    node.segment(),
                                    is_evicted(node_uuid),
                                    lm->seq(),
                                    nsv,
                                    (nsv == current_view_.id() ?
                                     input_map_->safe_seq(node.index()) :
                                     -1),
                                    (nsv == current_view_.id() ?
                                     input_map_->range(node.index()) :
                                     Range()));
            }
            else if (current_view_.is_member(node_uuid) == true)
            {
                mnode = MessageNode(node.operational(),
                                    node.is_suspected(),
                                    node.segment(),
                                    is_evicted(node_uuid),
                                    -1,
                                    current_view_.id(),
                                    input_map_->safe_seq(node.index()),
                                    input_map_->range(node.index()));
            }
        }
        else
        {
            mnode = MessageNode(true,
                                false,
                                node.segment(),
                                is_evicted(node_uuid),
                                -1,
                                current_view_.id(),
                                input_map_->safe_seq(node.index()),
                                input_map_->range(node.index()));
        }
        gu_trace((void)node_list->insert_unique(std::make_pair(node_uuid, mnode)));
    }

    // Iterate over evicted_list and add evicted nodes not yet in node list.
    for (Protolay::EvictList::const_iterator i(evict_list().begin());
         i != evict_list().end(); ++i)
    {
        if (node_list->find(Protolay::EvictList::key(i)) == node_list->end())
        {
            // default arguments are evil.
            MessageNode mnode(false, false, 0, true);
            gu_trace((void)node_list->insert_unique(
                         std::make_pair(Protolay::EvictList::key(i), mnode)));
        }
    }

    evs_log_debug(D_CONSENSUS) << "populate node list:\n" << *node_list;
}

const gcomm::evs::JoinMessage& gcomm::evs::Proto::create_join()
{

    MessageNodeList node_list;

    gu_trace(populate_node_list(&node_list));
    JoinMessage jm(version_,
                   uuid(),
                   current_view_.id(),
                   input_map_->safe_seq(),
                   input_map_->aru_seq(),
                   ++fifo_seq_,
                   node_list);
    NodeMap::value(self_i_).set_join_message(&jm);

    evs_log_debug(D_JOIN_MSGS) << " created join message " << jm;

    return *NodeMap::value(self_i_).join_message();
}


void gcomm::evs::Proto::set_join(const JoinMessage& jm, const UUID& source)
{
    NodeMap::iterator i;
    gu_trace(i = known_.find_checked(source));
    NodeMap::value(i).set_join_message(&jm);;
}


void gcomm::evs::Proto::set_leave(const LeaveMessage& lm, const UUID& source)
{
    NodeMap::iterator i;
    gu_trace(i = known_.find_checked(source));
    Node& inst(NodeMap::value(i));

    if (inst.leave_message())
    {
        evs_log_debug(D_LEAVE_MSGS) << "Duplicate leave:\told: "
                                    << *inst.leave_message()
                                    << "\tnew: " << lm;
    }
    else
    {
        inst.set_leave_message(&lm);
    }
}


void gcomm::evs::Proto::send_join(bool handle)
{
    assert(output_.empty() == true);

    JoinMessage jm(create_join());

    gu::Buffer buf;
    serialize(jm, buf);
    Datagram dg(buf);
    int err = send_down(dg, ProtoDownMeta());

    if (err != 0)
    {
        log_debug << "send failed: " << strerror(err);
    }
    sent_msgs_[Message::EVS_T_JOIN]++;
    if (handle == true)
    {
        handle_join(jm, self_i_);
    }
}


void gcomm::evs::Proto::send_leave(bool handle)
{
    gcomm_assert(state() == S_LEAVING);

    // If no messages have been sent, generate one dummy to
    // trigger message acknowledgement mechanism
    if (last_sent_ == -1 && output_.empty() == true)
    {
        Datagram wb;
        gu_trace(send_user(wb, 0xff, O_DROP, -1, -1));
    }

    /* Move all pending messages from output to input map */
    while (output_.empty() == false)
    {
        std::pair<Datagram, ProtoDownMeta> wb = output_.front();
        if (send_user(wb.first,
                      wb.second.user_type(),
                      wb.second.order(),
                      -1, -1) != 0)
        {
            gu_throw_fatal << "send_user() failed";
        }
        output_.pop_front();
    }


    LeaveMessage lm(version_,
                    uuid(),
                    current_view_.id(),
                    last_sent_,
                    input_map_->aru_seq(),
                    ++fifo_seq_);

    evs_log_debug(D_LEAVE_MSGS) << "sending leave msg " << lm;

    gu::Buffer buf;
    serialize(lm, buf);
    Datagram dg(buf);
    int err = send_down(dg, ProtoDownMeta());
    if (err != 0)
    {
        log_debug << "send failed " << strerror(err);
    }

    sent_msgs_[Message::EVS_T_LEAVE]++;

    if (handle == true)
    {
        handle_leave(lm, self_i_);
    }
}


struct ViewIdCmp
{
    bool operator()(const gcomm::evs::NodeMap::value_type& a,
                    const gcomm::evs::NodeMap::value_type& b) const
    {
        using gcomm::evs::NodeMap;
        gcomm_assert(NodeMap::value(a).join_message() != 0 &&
                     NodeMap::value(b).join_message() != 0);
        return (NodeMap::value(a).join_message()->source_view_id().seq() <
                NodeMap::value(b).join_message()->source_view_id().seq());

    }
};


struct ProtoVerCmp
{
    bool operator()(const gcomm::evs::NodeMap::value_type& a,
                    const gcomm::evs::NodeMap::value_type& b) const
    {
        using gcomm::evs::NodeMap;
        gcomm_assert(NodeMap::value(a).join_message() != 0 &&
                     NodeMap::value(b).join_message() != 0);
        return (NodeMap::value(a).join_message()->version() <
                NodeMap::value(b).join_message()->version());

    }
};

void gcomm::evs::Proto::send_install(EVS_CALLER_ARG)
{
    gcomm_assert(consensus_.is_consensus() == true &&
                 is_representative(uuid()) == true) << *this;

    // Select list of operational nodes from known
    NodeMap oper_list;
    for_each(known_.begin(), known_.end(), OperationalSelect(oper_list));
    NodeMap::const_iterator max_node =
        max_element(oper_list.begin(), oper_list.end(), ViewIdCmp());

    // Compute maximum known view id seq
    max_view_id_seq_ =
        std::max(max_view_id_seq_,
                 NodeMap::value(max_node).join_message()->source_view_id().seq());

    // Compute highest commonly supported protocol version.
    // Oper_list is non-empty, join message existence is asserted.
    const int version(
        NodeMap::value(
            std::min_element(oper_list.begin(), oper_list.end(),
                             ProtoVerCmp())).join_message()->version());

    MessageNodeList node_list;
    populate_node_list(&node_list);

    InstallMessage imsg(version,
                        uuid(),
                        current_view_.id(),
                        ViewId(V_REG, uuid(), max_view_id_seq_ + attempt_seq_),
                        input_map_->safe_seq(),
                        input_map_->aru_seq(),
                        ++fifo_seq_,
                        node_list);
    ++attempt_seq_;
    evs_log_debug(D_INSTALL_MSGS) << EVS_LOG_METHOD << imsg;
    evs_log_info(I_STATE) << "sending install message" << imsg;
    gcomm_assert(consensus_.is_consistent(imsg));

    gu::Buffer buf;
    serialize(imsg, buf);
    Datagram dg(buf);
    int err = send_down(dg, ProtoDownMeta());
    if (err != 0)
    {
        log_debug << "send failed: " << strerror(err);
    }

    sent_msgs_[Message::EVS_T_INSTALL]++;
    handle_install(imsg, self_i_);
}


void gcomm::evs::Proto::send_delayed_list()
{
    DelayedListMessage elm(version_, uuid(), current_view_.id(), ++fifo_seq_);
    for (DelayedList::const_iterator i(delayed_list_.begin());
         i != delayed_list_.end(); ++i)
    {
        elm.add(i->first, i->second.state_change_cnt());
    }
    gu::Buffer buf;
    serialize(elm, buf);
    Datagram dg(buf);
    (void)send_down(dg, ProtoDownMeta());
    handle_delayed_list(elm, self_i_);
}

void gcomm::evs::Proto::resend(const UUID& gap_source, const Range range)
{
    gcomm_assert(gap_source != uuid());
    gcomm_assert(range.lu() <= range.hs()) <<
        "lu (" << range.lu() << ") > hs(" << range.hs() << ")";

    if (range.lu() <= input_map_->safe_seq())
    {
        evs_log_debug(D_RETRANS) << self_string() << "lu (" << range.lu()
                                 << ") <= safe_seq("
                                 << input_map_->safe_seq()
                                 << "), can't recover message";
        return;
    }

    evs_log_debug(D_RETRANS) << " retrans requested by "
                             << gap_source
                             << " "
                             << range.lu() << " -> "
                             << range.hs();

    seqno_t seq(range.lu());
    while (seq <= range.hs())
    {
        InputMap::iterator msg_i = input_map_->find(
            NodeMap::value(self_i_).index(), seq);
        if (msg_i == input_map_->end())
        {
            try
            {
                gu_trace(msg_i = input_map_->recover(
                             NodeMap::value(self_i_).index(), seq));
            }
            catch (...)
            {
                evs_log_debug(D_RETRANS) << "could not recover message "
                                         << gap_source << ":" << seq;
                seq = seq + 1;
                continue;
            }
        }

        const UserMessage& msg(InputMapMsgIndex::value(msg_i).msg());
        gcomm_assert(msg.source() == uuid());
        Datagram rb(InputMapMsgIndex::value(msg_i).rb());
        assert(rb.offset() == 0);

        UserMessage um(msg.version(),
                       msg.source(),
                       msg.source_view_id(),
                       msg.seq(),
                       input_map_->aru_seq(),
                       msg.seq_range(),
                       msg.order(),
                       msg.fifo_seq(),
                       msg.user_type(),
                       static_cast<uint8_t>(
                           Message::F_RETRANS |
                           (msg.flags() & Message::F_AGGREGATE)));

        push_header(um, rb);

        int err = send_down(rb, ProtoDownMeta());
        if (err != 0)
        {
            log_debug << "send failed: " << strerror(err);
            break;
        }
        else
        {
            evs_log_debug(D_RETRANS) << "retransmitted " << um;
        }
        seq = seq + msg.seq_range() + 1;
        retrans_msgs_++;
    }
}


void gcomm::evs::Proto::recover(const UUID& gap_source,
                                const UUID& range_uuid,
                                const Range range)
{
    gcomm_assert(gap_source != uuid())
        << "gap_source (" << gap_source << ") == uuid() (" << uuid()
        << " state " << *this;
    gcomm_assert(range.lu() <= range.hs())
        << "lu (" << range.lu() << ") > hs (" << range.hs() << ")";

    if (range.lu() <= input_map_->safe_seq())
    {
        evs_log_debug(D_RETRANS) << "lu (" << range.lu()
                                 << ") <= safe_seq(" << input_map_->safe_seq()
                                 << "), can't recover message";
        return;
    }

    const Node& range_node(NodeMap::value(known_.find_checked(range_uuid)));
    const Range im_range(input_map_->range(range_node.index()));

    evs_log_debug(D_RETRANS) << " recovering message from "
                             << range_uuid
                             << " requested by "
                             << gap_source
                             << " requested range " << range
                             << " available " << im_range;


    seqno_t seq(range.lu());
    while (seq <= range.hs() && seq <= im_range.hs())
    {
        InputMap::iterator msg_i = input_map_->find(range_node.index(), seq);
        if (msg_i == input_map_->end())
        {
            try
            {
                gu_trace(msg_i = input_map_->recover(range_node.index(), seq));
            }
            catch (...)
            {
                seq = seq + 1;
                continue;
            }
        }

        const UserMessage& msg(InputMapMsgIndex::value(msg_i).msg());
        assert(msg.source() == range_uuid);

        Datagram rb(InputMapMsgIndex::value(msg_i).rb());
        assert(rb.offset() == 0);
        UserMessage um(msg.version(),
                       msg.source(),
                       msg.source_view_id(),
                       msg.seq(),
                       msg.aru_seq(),
                       msg.seq_range(),
                       msg.order(),
                       msg.fifo_seq(),
                       msg.user_type(),
                       static_cast<uint8_t>(
                           Message::F_SOURCE |
                           Message::F_RETRANS |
                           (msg.flags() & Message::F_AGGREGATE)));

        push_header(um, rb);

        int err = send_delegate(rb);
        if (err != 0)
        {
            log_debug << "send failed: " << strerror(err);
            break;
        }
        else
        {
            evs_log_debug(D_RETRANS) << "recover " << um;
        }
        seq = seq + msg.seq_range() + 1;
        recovered_msgs_++;
    }
}


void gcomm::evs::Proto::handle_foreign(const Message& msg)
{
    // no need to handle foreign LEAVE message
    if (msg.type() == Message::EVS_T_LEAVE)
    {
        return;
    }

    // Don't handle foreing messages in install phase.
    // This includes not only INSTALL state, but also
    // GATHER state after receiving install message.
    if (install_message_ != 0)
    {
        evs_log_debug(D_FOREIGN_MSGS)
            << " dropping foreign message from "
            << msg.source() << " in install state";
        return;
    }

    if (is_msg_from_previous_view(msg) == true)
    {
        return;
    }

    const UUID& source(msg.source());

    evs_log_info(I_STATE) << " detected new message source "
                          << source;

    NodeMap::iterator i;
    gu_trace(i = known_.insert_unique(
                 std::make_pair(source, Node(*this))));
    assert(NodeMap::value(i).operational() == true);

    if (state() == S_JOINING || state() == S_GATHER ||
        state() == S_OPERATIONAL)
    {
        evs_log_info(I_STATE)
            << " shift to GATHER due to foreign message from "
            << msg.source();
        gu_trace(shift_to(S_GATHER, false));
        // Reset install timer each time foreign message is seen to
        // synchronize install timers.
        reset_timer(T_INSTALL);
    }

    // Set join message after shift to recovery, shift may clean up
    // join messages
    if (msg.type() == Message::EVS_T_JOIN)
    {
        set_join(static_cast<const JoinMessage&>(msg), msg.source());
    }
    send_join(true);
}

void gcomm::evs::Proto::handle_msg(const Message& msg,
                                   const Datagram& rb,
                                   bool direct)
{
    assert(msg.type() <= Message::EVS_T_DELAYED_LIST);
    if (msg.type() > Message::EVS_T_DELAYED_LIST)
    {
        return;
    }

    if (state() == S_CLOSED)
    {
        return;
    }

    if (isolation_end_ != gu::datetime::Date::zero())
    {
        evs_log_debug(D_STATE) << " dropping message due to isolation";
        // Isolation period is on
        return;
    }

    if (msg.source() == uuid())
    {
        evs_log_debug(D_FOREIGN_MSGS) << " dropping own message";
        return;
    }

    if (msg.version() > GCOMM_PROTOCOL_MAX_VERSION)
    {
        log_info << "incompatible protocol version "
                 << static_cast<int>(msg.version());
        return;
    }

    gcomm_assert(msg.source() != UUID::nil());

    // Figure out if the message is from known source
    NodeMap::iterator ii = known_.find(msg.source());

    if (ii == known_.end())
    {
        gu_trace(handle_foreign(msg));
        return;
    }

    Node& node(NodeMap::value(ii));
    if (direct == true)
    {
        node.set_seen_tstamp(gu::datetime::Date::now());
    }

    if (node.operational()                 == false &&
        node.leave_message()               == 0     &&
        (msg.flags() & Message::F_RETRANS) == 0)
    {
        // We have set this node unoperational and there was
        // probably good reason to do so. Don't accept messages
        // from it before new view has been formed.
        // Exceptions:
        // - Node that is leaving
        // - Retransmitted messages.

        // why we accept retransimted messages?
        // a node sends a message, some nodes(A) get it, but some(B) don't
        // then this node is non-operational(or unreachable)
        // so A need to send B the missing message(in envelope as delegate message)
        // otherwise the input map will not be consistent forever.
        // and user message in delegate message always comes with F_RETRANS flag.
        evs_log_debug(D_FOREIGN_MSGS)
            << " dropping message from unoperational source " << node;
        return;
    }

    // Filter out non-fifo messages
    if (msg.fifo_seq() != -1 && (msg.flags() & Message::F_RETRANS) == 0)
    {

        if (node.fifo_seq() >= msg.fifo_seq())
        {
            evs_log_debug(D_FOREIGN_MSGS)
                << "droppoing non-fifo message " << msg
                << " fifo seq " << node.fifo_seq();
            return;
        }
        else
        {
            node.set_fifo_seq(msg.fifo_seq());
        }
    }

    // Accept non-membership messages only from current view
    // or from view to be installed
    if (msg.is_membership()                  == false                    &&
        msg.source_view_id()                 != current_view_.id()    &&
        (install_message_                    == 0                     ||
         install_message_->install_view_id() != msg.source_view_id()))
    {
        // If source node seems to be operational but it has proceeded
        // into new view, mark it as unoperational in order to create
        // intermediate views before re-merge.
        if (node.installed()           == true      &&
            node.operational()         == true      &&
            is_msg_from_previous_view(msg) == false     &&
            state()                    != S_LEAVING)
        {
            if (new_view_logged_ == false)
            {
                evs_log_info(I_STATE)
                    << " detected new view from operational source "
                    << msg.source() << ": "
                    << msg.source_view_id();
                new_view_logged_ = true;
            }
            // Note: Commented out, this causes problems with
            // attempt_seq. Newly (remotely?) generated install message
            // followed by commit gap may cause undesired
            // node inactivation and shift to gather.
            //
            // set_inactive(msg.source());
            // gu_trace(shift_to(S_GATHER, true));
        }
        evs_log_debug(D_FOREIGN_MSGS)
            << "dropping non-membership message from foreign view";
        return;
    }
    else if (NodeMap::value(ii).index() == std::numeric_limits<size_t>::max() &&
             msg.source_view_id()       == current_view_.id())
    {
        log_warn << "Message from node that claims to come from same view but is not in current view " << msg;
        assert(0);
        return;
    }

    recvd_msgs_[msg.type()]++;

    switch (msg.type())
    {
    case Message::EVS_T_USER:
        gu_trace(handle_user(static_cast<const UserMessage&>(msg), ii, rb));
        break;
    case Message::EVS_T_DELEGATE:
        gu_trace(handle_delegate(static_cast<const DelegateMessage&>(msg), ii, rb));
        break;
    case Message::EVS_T_GAP:
        gu_trace(handle_gap(static_cast<const GapMessage&>(msg), ii));
        break;
    case Message::EVS_T_JOIN:
        gu_trace(handle_join(static_cast<const JoinMessage&>(msg), ii));
        break;
    case Message::EVS_T_LEAVE:
        gu_trace(handle_leave(static_cast<const LeaveMessage&>(msg), ii));
        break;
    case Message::EVS_T_INSTALL:
        gu_trace(handle_install(static_cast<const InstallMessage&>(msg), ii));
        break;
    case Message::EVS_T_DELAYED_LIST:
        gu_trace(handle_delayed_list(
                     static_cast<const DelayedListMessage&>(msg), ii));
        break;
    default:
        log_warn << "invalid message type " << msg.type();
    }
}

////////////////////////////////////////////////////////////////////////
// Protolay interface
////////////////////////////////////////////////////////////////////////

size_t gcomm::evs::Proto::unserialize_message(const UUID& source,
                                              const Datagram& rb,
                                              Message* msg)
{
    size_t offset;
    const gu::byte_t* begin(gcomm::begin(rb));
    const size_t available(gcomm::available(rb));
    gu_trace(offset = msg->unserialize(begin,
                                       available,
                                       0));
    if ((msg->flags() & Message::F_SOURCE) == 0)
    {
        assert(source != UUID::nil());
        gcomm_assert(source != UUID::nil());
        msg->set_source(source);
    }

    switch (msg->type())
    {
    case Message::EVS_T_NONE:
        gu_throw_fatal;
        break;
    case Message::EVS_T_USER:
        gu_trace(offset = static_cast<UserMessage&>(*msg).unserialize(
                     begin, available, offset, true));
        break;
    case Message::EVS_T_DELEGATE:
        gu_trace(offset = static_cast<DelegateMessage&>(*msg).unserialize(
                     begin, available, offset, true));
        break;
    case Message::EVS_T_GAP:
        gu_trace(offset = static_cast<GapMessage&>(*msg).unserialize(
                     begin, available, offset, true));
        break;
    case Message::EVS_T_JOIN:
        gu_trace(offset = static_cast<JoinMessage&>(*msg).unserialize(
                     begin, available, offset, true));
        break;
    case Message::EVS_T_INSTALL:
        gu_trace(offset = static_cast<InstallMessage&>(*msg).unserialize(
                     begin, available, offset, true));
        break;
    case Message::EVS_T_LEAVE:
        gu_trace(offset = static_cast<LeaveMessage&>(*msg).unserialize(
                     begin, available, offset, true));
        break;
    case Message::EVS_T_DELAYED_LIST:
        gu_trace(offset = static_cast<DelayedListMessage&>(*msg).unserialize(
                     begin, available, offset, true));
        break;
    }
    return (offset + rb.offset());
}

void gcomm::evs::Proto::handle_up(const void* cid,
                                  const Datagram& rb,
                                  const ProtoUpMeta& um)
{

    Message msg;

    if (state() == S_CLOSED || um.source() == uuid() || is_evicted(um.source()))
    {
        // Silent drop
        return;
    }

    gcomm_assert(um.source() != UUID::nil());

    try
    {
        size_t offset;
        gu_trace(offset = unserialize_message(um.source(), rb, &msg));
        handle_msg(msg, Datagram(rb, offset),
                   (msg.flags() & Message::F_RETRANS) == 0);
    }
    catch (gu::Exception& e)
    {
        switch (e.get_errno())
        {
        case EPROTONOSUPPORT:
            log_warn << e.what();
            break;

        case EINVAL:
            log_warn << "invalid message: " << msg;
            break;

        default:
            log_fatal << "exception caused by message: " << msg;
            std::cerr << " state after handling message: " << *this;
            throw;
        }
    }
}


int gcomm::evs::Proto::handle_down(Datagram& wb, const ProtoDownMeta& dm)
{
    if (state() == S_GATHER || state() == S_INSTALL)
    {
        return EAGAIN;
    }

    else if (state() != S_OPERATIONAL)
    {
        log_warn << "user message in state " << to_string(state());
        return ENOTCONN;
    }

    if (dm.order() == O_LOCAL_CAUSAL)
    {
        gu::datetime::Date now(gu::datetime::Date::now());
        if (causal_queue_.empty() == true &&
            last_sent_ == input_map_->safe_seq() &&
            causal_keepalive_period_ > gu::datetime::Period(0) &&
            last_causal_keepalive_ + causal_keepalive_period_ > now)
        {

            assert(last_sent_ == input_map_->aru_seq());
            // Input map should either be empty (all messages
            // delivered) or the undelivered messages have higher
            // seqno than safe_seq. Even if the delivry is
            // done below if needed, this assertion should stay
            // to catch errors in logic elsewhere in the code.
            assert(input_map_->begin() == input_map_->end() ||
                   input_map_->is_safe(input_map_->begin()) == false);


            if (input_map_->begin() != input_map_->end() &&
                input_map_->is_safe(input_map_->begin()) == true)
            {
                gu_trace(deliver());
                if (input_map_->begin() != input_map_->end() &&
                    input_map_->is_safe(input_map_->begin()) == true)
                {
                    // If the input map state is still not good for fast path,
                    // the situation is not likely to clear immediately. Retur
                    // error to retry later.
                    return EAGAIN;
                }
            }

            hs_local_causal_.insert(0.0);
            deliver_causal(dm.user_type(), last_sent_, wb);
        }
        else
        {
            seqno_t causal_seqno(input_map_->aru_seq());
            if (causal_keepalive_period_ == gu::datetime::Period(0) ||
                last_causal_keepalive_ + causal_keepalive_period_ <= now)
            {
                // generate traffic to make sure that group is live
                Datagram dg;
                int err(send_user(dg, 0xff, O_DROP, -1, -1));
                if (err != 0)
                {
                    return err;
                }
                // reassign causal_seqno to be last_sent:
                // in order to make sure that the group is live,
                // safe seqno must be advanced and in this case
                // safe seqno equals to aru seqno.
                causal_seqno = last_sent_;
                last_causal_keepalive_ = now;
            }
            causal_queue_.push_back(CausalMessage(dm.user_type(),
                                                  causal_seqno, wb));
        }
        return 0;
    }


    send_queue_s_ += output_.size();
    ++n_send_queue_s_;

    int ret = 0;

    if (output_.empty() == true)
    {
        int err;
        err = send_user(wb,
                        dm.user_type(),
                        dm.order(),
                        user_send_window_,
                        -1);

        switch (err)
        {
        case EAGAIN:
            output_.push_back(std::make_pair(wb, dm));
            // fall through
        case 0:
            ret = 0;
            break;
        default:
            log_error << "send error: " << err;
            ret = err;
        }
    }
    else if (output_.size() < max_output_size_)
    {
        output_.push_back(std::make_pair(wb, dm));
    }
    else
    {
        ret = EAGAIN;
    }
    return ret;
}

int gcomm::evs::Proto::send_down(Datagram& dg, const ProtoDownMeta& dm)
{
    if (isolation_end_ != gu::datetime::Date::zero())
    {
        // Node has isolated itself, don't emit any messages
        return 0;
    }
    else
    {
        return Protolay::send_down(dg, dm);
    }
}


/////////////////////////////////////////////////////////////////////////////
// State handler
/////////////////////////////////////////////////////////////////////////////

void gcomm::evs::Proto::shift_to(const State s, const bool send_j)
{
    if (shift_to_rfcnt_ > 0) gu_throw_fatal << *this;

    shift_to_rfcnt_++;

    static const bool allowed[S_MAX][S_MAX] = {
        // CLOSED JOINING LEAVING GATHER INSTALL OPERAT
        {  false,  true,   false, false, false,  false }, // CLOSED

        {  false,  false,  true,  true,  false,  false }, // JOINING

        {  true,   false,  false, false, false,  false }, // LEAVING

        {  false,  false,  true,  true,  true,   false }, // GATHER

        {  false,  false,  false, true,  false,  true  },  // INSTALL

        {  false,  false,  true,  true,  false,  false }  // OPERATIONAL
    };

    assert(s < S_MAX);

    if (allowed[state_][s] == false) {
        gu_throw_fatal << "Forbidden state transition: "
                       << to_string(state_) << " -> " << to_string(s);
    }

    if (state() != s)
    {
        evs_log_info(I_STATE) << " state change: "
                              << to_string(state_) << " -> " << to_string(s);
    }
    switch (s) {
    case S_CLOSED:
    {
        gcomm_assert(state() == S_LEAVING);
        gu_trace(deliver());
        gu_trace(deliver_local());
        setall_installed(false);
        NodeMap::value(self_i_).set_installed(true);
        // Construct install message containing only one node for
        // last trans view.
        MessageNodeList node_list;
        (void)node_list.insert_unique(
            std::make_pair(uuid(),
                           MessageNode(true,
                                       false,
                                       NodeMap::value(self_i_).segment(),
                                       false,
                                       -1,
                                       current_view_.id(),
                                       input_map_->safe_seq(
                                           NodeMap::value(self_i_).index()),
                                       input_map_->range(
                                           NodeMap::value(self_i_).index()))));
        InstallMessage im(0,
                          uuid(),
                          current_view_.id(),
                          ViewId(V_REG, uuid(), current_view_.id().seq() + 1),
                          input_map_->safe_seq(),
                          input_map_->aru_seq(),
                          ++fifo_seq_,
                          node_list);
        gu_trace(deliver_trans_view(im, current_view_));
        gu_trace(deliver_trans());
        gu_trace(deliver_local(true));
        gcomm_assert(causal_queue_.empty() == true);
        if (collect_stats_ == true)
        {
            handle_stats_timer();
        }
        gu_trace(deliver_empty_view());
        cleanup_foreign(im);
        cleanup_views();
        timers_.clear();
        state_ = S_CLOSED;
        break;
    }
    case S_JOINING:
        state_ = S_JOINING;
        reset_timer(T_STATS);
        break;
    case S_LEAVING:
        state_ = S_LEAVING;
        reset_timer(T_INACTIVITY);
        reset_timer(T_RETRANS);
        reset_timer(T_INSTALL);
        break;
    case S_GATHER:
    {
        setall_committed(false);
        setall_installed(false);
        delete install_message_;
        install_message_ = 0;

        if (state() == S_OPERATIONAL)
        {
            profile_enter(send_user_prof_);
            while (output_.empty() == false)
            {
                int err;
                gu_trace(err = send_user(-1));
                if (err != 0)
                {
                    gu_throw_fatal << self_string()
                                   << "send_user() failed in shifto "
                                   << "to S_GATHER: "
                                   << strerror(err);
                }
            }
            profile_leave(send_user_prof_);
        }
        else
        {
            gcomm_assert(output_.empty() == true);
        }

        State prev_state(state_);
        state_ = S_GATHER;
        if (send_j == true)
        {
            profile_enter(send_join_prof_);
            gu_trace(send_join(false));
            profile_leave(send_join_prof_);
        }
        gcomm_assert(state() == S_GATHER);
        reset_timer(T_INACTIVITY);
        if (prev_state == S_OPERATIONAL || prev_state == S_JOINING)
        {
            reset_timer(T_RETRANS);
            reset_timer(T_INSTALL);
        }
        break;
    }
    case S_INSTALL:
    {
        gcomm_assert(install_message_ != 0);
        gcomm_assert(is_all_committed() == true);
        state_ = S_INSTALL;
        reset_timer(T_INACTIVITY);
        reset_timer(T_RETRANS);
        break;
    }
    case S_OPERATIONAL:
    {
        gcomm_assert(output_.empty() == true);
        gcomm_assert(install_message_ != 0);
        gcomm_assert(NodeMap::value(self_i_).join_message() != 0 &&
                     consensus_.equal(
                         *NodeMap::value(self_i_).join_message(),
                         *install_message_))
            << "install message not consistent with own join, state: " << *this;
        gcomm_assert(is_all_installed() == true);
        gu_trace(deliver());
        gu_trace(deliver_local());
        gu_trace(deliver_trans_view(*install_message_, current_view_));
        gu_trace(deliver_trans());
        gu_trace(deliver_local(true));
        gcomm_assert(causal_queue_.empty() == true);
        input_map_->clear();
        if (collect_stats_ == true)
        {
            handle_stats_timer();
        }
        // End of previous view

        // Construct new view and shift to S_OPERATIONAL before calling
        // deliver_reg_view(). Reg view delivery may trigger message
        // exchange on upper layer and operating view is needed to
        // handle messages.

        previous_view_ = current_view_;
        std::copy(gather_views_.begin(), gather_views_.end(),
                  std::inserter(previous_views_, previous_views_.end()));
        gather_views_.clear();

        if (install_message_->version() > current_view_.version())
        {
            log_info << "EVS version upgrade " << current_view_.version()
                     << " -> " << static_cast<int>(install_message_->version());
        }
        else if (install_message_->version() < current_view_.version())
        {
            log_info << "EVS version downgrade " << current_view_.version()
                     << " -> " << static_cast<int>(install_message_->version());
        }

        current_view_ = View(install_message_->version(),
                             install_message_->install_view_id());
        size_t idx = 0;

        const MessageNodeList& imnl(install_message_->node_list());

        for (MessageNodeList::const_iterator i(imnl.begin());
             i != imnl.end(); ++i)
        {
            const UUID& uuid(MessageNodeList::key(i));
            const MessageNode& n(MessageNodeList::value(i));

            // Add operational nodes to new view, assign input map index
            NodeMap::iterator nmi(known_.find(uuid));
            gcomm_assert(nmi != known_.end()) << "node " << uuid
                                              << " not found from known map";
            if (n.operational() == true)
            {
                current_view_.add_member(uuid, NodeMap::value(nmi).segment());
                NodeMap::value(nmi).set_index(idx++);
            }
            else
            {
                NodeMap::value(nmi).set_index(
                    std::numeric_limits<size_t>::max());
            }

        }

        if (previous_view_.id().type() == V_REG &&
            previous_view_.members() == current_view_.members())
        {
            evs_log_info(I_VIEWS)
                << "subsequent views have same members, prev view "
                << previous_view_ << " current view " << current_view_;
        }

        input_map_->reset(current_view_.members().size());
        last_sent_ = -1;
        state_ = S_OPERATIONAL;
        deliver_reg_view(*install_message_, previous_view_);

        cleanup_foreign(*install_message_);
        cleanup_views();
        cleanup_joins();

        delete install_message_;
        install_message_ = 0;
        attempt_seq_ = 1;
        install_timeout_count_ = 0;
        profile_enter(send_gap_prof_);
        gu_trace(send_gap(EVS_CALLER, UUID::nil(), current_view_.id(), Range()));;
        profile_leave(send_gap_prof_);
        gcomm_assert(state() == S_OPERATIONAL);
        reset_timer(T_INACTIVITY);
        reset_timer(T_RETRANS);
        cancel_timer(T_INSTALL);
        new_view_logged_ = false;
        break;
    }
    default:
        gu_throw_fatal << "invalid state";
    }
    shift_to_rfcnt_--;
}

////////////////////////////////////////////////////////////////////////////
// Message delivery
////////////////////////////////////////////////////////////////////////////

void gcomm::evs::Proto::deliver_causal(uint8_t user_type,
                                       seqno_t seqno,
                                       const Datagram& datagram)
{
    send_up(datagram, ProtoUpMeta(uuid(),
                                  current_view_.id(),
                                  0,
                                  user_type,
                                  O_LOCAL_CAUSAL,
                                  seqno));
    ++delivered_msgs_[O_LOCAL_CAUSAL];
}


void gcomm::evs::Proto::deliver_local(bool trans)
{
    // local causal
    const seqno_t causal_seq(trans == false ? input_map_->safe_seq() : last_sent_);
    gu::datetime::Date now(gu::datetime::Date::now());

    assert(input_map_->begin() == input_map_->end() ||
           input_map_->is_safe(input_map_->begin()) == false);

    while (causal_queue_.empty() == false &&
           causal_queue_.front().seqno() <= causal_seq)
    {
        const CausalMessage& cm(causal_queue_.front());
        hs_local_causal_.insert(double(now.get_utc() - cm.tstamp().get_utc())/gu::datetime::Sec);
        deliver_causal(cm.user_type(), cm.seqno(), cm.datagram());
        causal_queue_.pop_front();
    }
}

void gcomm::evs::Proto::validate_reg_msg(const UserMessage& msg)
{
    if (msg.source_view_id() != current_view_.id())
    {
        // Note: This implementation should guarantee same view delivery,
        // this is sanity check for that.
        gu_throw_fatal << "reg validate: not current view";
    }

    // Update statistics for locally generated messages
    if (msg.source() == uuid())
    {
        if (msg.order() == O_SAFE)
        {
            gu::datetime::Date now(gu::datetime::Date::now());
            double lat(double(now.get_utc() - msg.tstamp().get_utc())/
                       gu::datetime::Sec);
            if (info_mask_ & I_STATISTICS) hs_safe_.insert(lat);
            safe_deliv_latency_.insert(lat);
        }
        else if (msg.order() == O_AGREED)
        {
            if (info_mask_ & I_STATISTICS)
            {
                gu::datetime::Date now(gu::datetime::Date::now());
                hs_agreed_.insert(double(now.get_utc() - msg.tstamp().get_utc())/gu::datetime::Sec);
            }
        }
    }
}


void gcomm::evs::Proto::deliver_finish(const InputMapMsg& msg)
{
    if ((msg.msg().flags() & Message::F_AGGREGATE) == 0)
    {
        ++delivered_msgs_[msg.msg().order()];
        if (msg.msg().order() != O_DROP)
        {
            gu_trace(validate_reg_msg(msg.msg()));
            profile_enter(delivery_prof_);
            ProtoUpMeta um(msg.msg().source(),
                           msg.msg().source_view_id(),
                           0,
                           msg.msg().user_type(),
                           msg.msg().order(),
                           msg.msg().seq());
            try
            {
                send_up(msg.rb(), um);
            }
            catch (...)
            {
                log_info << msg.msg() << " " << msg.rb().len();
                throw;
            }
            profile_leave(delivery_prof_);
        }
    }
    else
    {
        gu_trace(validate_reg_msg(msg.msg()));
        size_t offset(0);
        while (offset < msg.rb().len())
        {
            ++delivered_msgs_[msg.msg().order()];
            AggregateMessage am;
            gu_trace(am.unserialize(&msg.rb().payload()[0],
                                    msg.rb().payload().size(),
                                    offset));
            Datagram dg(
                gu::SharedBuffer(
                    new gu::Buffer(
                        &msg.rb().payload()[0]
                        + offset
                        + am.serial_size(),
                        &msg.rb().payload()[0]
                        + offset
                        + am.serial_size()
                        + am.len())));
            ProtoUpMeta um(msg.msg().source(),
                           msg.msg().source_view_id(),
                           0,
                           am.user_type(),
                           msg.msg().order(),
                           msg.msg().seq());
            gu_trace(send_up(dg, um));
            offset += am.serial_size() + am.len();
        }
        gcomm_assert(offset == msg.rb().len());
    }
}

void gcomm::evs::Proto::deliver()
{
    if (delivering_ == true)
    {
        gu_throw_fatal << "Recursive enter to delivery";
    }

    delivering_ = true;

    if (state() != S_OPERATIONAL &&
        state() != S_GATHER      &&
        state() != S_INSTALL     &&
        state() != S_LEAVING)
    {
        gu_throw_fatal << "invalid state: " << to_string(state());
    }

    evs_log_debug(D_DELIVERY)
        << " aru_seq="   << input_map_->aru_seq()
        << " safe_seq=" << input_map_->safe_seq();

    // Read input map head until a message which cannot be
    // delivered is enountered.
    InputMapMsgIndex::iterator i;
    while ((i = input_map_->begin()) != input_map_->end())
    {
        const InputMapMsg& msg(InputMapMsgIndex::value(i));
        if ((msg.msg().order() <= O_SAFE &&
             input_map_->is_safe(i) == true) ||
            (msg.msg().order() <= O_AGREED &&
             input_map_->is_agreed(i) == true) ||
            (msg.msg().order() <= O_FIFO &&
             input_map_->is_fifo(i) == true))
        {
            deliver_finish(msg);
            gu_trace(input_map_->erase(i));
        }
        else
        {
            if (msg.msg().order() > O_SAFE)
            {
                gu_throw_fatal << "Message with order " << msg.msg().order()
                               << " in input map, cannot continue safely";
            }
            break;
        }
    }
    delivering_ = false;

    assert(input_map_->begin() == input_map_->end() ||
           input_map_->is_safe(input_map_->begin()) == false);

}


void gcomm::evs::Proto::deliver_trans()
{
    if (delivering_ == true)
    {
        gu_throw_fatal << "Recursive enter to delivery";
    }

    delivering_ = true;

    if (state() != S_INSTALL &&
        state() != S_LEAVING)
        gu_throw_fatal << "invalid state";

    evs_log_debug(D_DELIVERY)
        << " aru_seq="  << input_map_->aru_seq()
        << " safe_seq=" << input_map_->safe_seq();

    // In transitional configuration we must deliver all messages that
    // are fifo. This is because:
    // - We know that it is possible to deliver all fifo messages originated
    //   from partitioned component as safe in partitioned component
    // - Aru in this component is at least the max known fifo seq
    //   from partitioned component due to message recovery
    // - All FIFO messages originated from this component must be
    //   delivered to fulfill self delivery requirement and
    // - FIFO messages originated from this component qualify as AGREED
    //   in transitional configuration

    InputMap::iterator i, i_next;
    for (i = input_map_->begin(); i != input_map_->end(); i = i_next)
    {
        i_next = i;
        ++i_next;
        const InputMapMsg& msg(InputMapMsgIndex::value(i));
        bool deliver = false;
        switch (msg.msg().order())
        {
        case O_SAFE:
        case O_AGREED:
        case O_FIFO:
        case O_DROP:
            if (input_map_->is_fifo(i) == true)
            {
                deliver = true;
            }
            break;
        default:
            gu_throw_fatal;
        }

        if (deliver == true)
        {
            if (install_message_ != 0)
            {
                const MessageNode& mn(
                    MessageNodeList::value(
                        install_message_->node_list().find_checked(
                            msg.msg().source())));
                if (msg.msg().seq() <= mn.im_range().hs())
                {
                    deliver_finish(msg);
                }
                else
                {
                    gcomm_assert(mn.operational() == false);
                    log_info << "filtering out trans message higher than "
                             << "install message hs "
                             << mn.im_range().hs()
                             << ": " << msg.msg();
                }
            }
            else
            {
                deliver_finish(msg);
            }
            gu_trace(input_map_->erase(i));
        }
    }

    // Sanity check:
    // There must not be any messages left that
    // - Are originated from outside of trans conf and are FIFO
    // - Are originated from trans conf
    for (i = input_map_->begin(); i != input_map_->end(); i = i_next)
    {
        i_next = i;
        ++i_next;
        const InputMapMsg& msg(InputMapMsgIndex::value(i));
        NodeMap::iterator ii;
        gu_trace(ii = known_.find_checked(msg.msg().source()));

        if (NodeMap::value(ii).installed() == true)
        {
            gu_throw_fatal << "Protocol error in transitional delivery "
                           << "(self delivery constraint)";
        }
        else if (input_map_->is_fifo(i) == true)
        {
            gu_throw_fatal << "Protocol error in transitional delivery "
                           << "(fifo from partitioned component)";
        }
        gu_trace(input_map_->erase(i));
    }
    delivering_ = false;
}


/////////////////////////////////////////////////////////////////////////////
// Message handlers
/////////////////////////////////////////////////////////////////////////////



gcomm::evs::seqno_t gcomm::evs::Proto::update_im_safe_seq(const size_t uuid,
                                                          const seqno_t seq)
{
    const seqno_t im_safe_seq(input_map_->safe_seq(uuid));
    if (im_safe_seq  < seq)
    {
        input_map_->set_safe_seq(uuid, seq);
    }
    return im_safe_seq;
}


void gcomm::evs::Proto::handle_user(const UserMessage& msg,
                                    NodeMap::iterator ii,
                                    const Datagram& rb)

{
    assert(ii != known_.end());
    assert(state() != S_CLOSED && state() != S_JOINING);
    Node& inst(NodeMap::value(ii));

    evs_log_debug(D_USER_MSGS) << "received " << msg;

    if (msg.source_view_id() != current_view_.id())
    {
        if (state() == S_LEAVING)
        {
            // Silent drop
            return;
        }

        if (is_msg_from_previous_view(msg) == true)
        {
            evs_log_debug(D_FOREIGN_MSGS) << "user message "
                                          << msg
                                          << " from previous view";
            return;
        }

        if (inst.operational() == false)
        {
            evs_log_debug(D_STATE)
                << "dropping message from unoperational source "
                << msg.source();
            return;
        }
        else if (inst.installed() == false)
        {
            if (install_message_ != 0 &&
                msg.source_view_id() == install_message_->install_view_id())
            {
                assert(state() == S_GATHER || state() == S_INSTALL);
                evs_log_debug(D_STATE) << " recovery user message "
                                       << msg;

                // This is possible if install timer expires just before
                // new view is established on this node and retransmitted
                // install message is received just before user this message.
                if (state() == S_GATHER)
                {
                    // Sanity check
                    MessageNodeList::const_iterator self(
                        install_message_->node_list().find(uuid()));
                    gcomm_assert(self != install_message_->node_list().end()
                                 && MessageNodeList::value(self).operational() == true);
                    // Mark all operational nodes in install message as
                    // committed
                    for (MessageNodeList::const_iterator
                             mi = install_message_->node_list().begin();
                         mi != install_message_->node_list().end(); ++mi)
                    {
                        if (MessageNodeList::value(mi).operational() == true)
                        {
                            NodeMap::iterator jj;
                            gu_trace(jj = known_.find_checked(
                                         MessageNodeList::key(mi)));
                            NodeMap::value(jj).set_committed(true);
                        }
                    }
                    shift_to(S_INSTALL);
                }

                // Other instances installed view before this one, so it is
                // safe to shift to S_OPERATIONAL

                // Mark all operational nodes in install message as installed
                for (MessageNodeList::const_iterator
                         mi = install_message_->node_list().begin();
                     mi != install_message_->node_list().end(); ++mi)
                {
                    if (MessageNodeList::value(mi).operational() == true)
                    {
                        NodeMap::iterator jj;
                        gu_trace(jj = known_.find_checked(
                                     MessageNodeList::key(mi)));
                        NodeMap::value(jj).set_installed(true);
                    }
                }
                inst.set_tstamp(gu::datetime::Date::now());

                profile_enter(shift_to_prof_);
                gu_trace(shift_to(S_OPERATIONAL));
                profile_leave(shift_to_prof_);
                if (pending_leave_ == true)
                {
                    close();
                }
                // proceed to process actual user message
            }
            else
            {
                return;
            }
        }
        else
        {
            log_debug << self_string() << " unhandled user message " << msg;
            return;
        }
    }

    gcomm_assert(msg.source_view_id() == current_view_.id());


    // note: #gh40
    bool shift_to_gather = false;
    if (install_message_) {
        const MessageNode& mn(
            MessageNodeList::value(
                install_message_->node_list().find_checked(
                    msg.source())));
        if (!mn.operational())
            return ;
        if (mn.operational() &&
            msg.seq() > mn.im_range().hs()) {
            shift_to_gather = true;
        }
    }

    Range range;
    Range prev_range;
    seqno_t prev_aru;
    seqno_t prev_safe;

    profile_enter(input_map_prof_);

    prev_aru = input_map_->aru_seq();
    prev_range = input_map_->range(inst.index());

    // Insert only if msg seq is greater or equal than current lowest unseen
    if (msg.seq() >= prev_range.lu())
    {
        Datagram im_dgram(rb, rb.offset());
        im_dgram.normalize();
        gu_trace(range = input_map_->insert(inst.index(), msg, im_dgram));
        if (range.lu() > prev_range.lu())
        {
            inst.set_tstamp(gu::datetime::Date::now());
        }
    }
    else
    {
        range = prev_range;
    }

    // Update im safe seq for self
    update_im_safe_seq(NodeMap::value(self_i_).index(),
                       input_map_->aru_seq());

    // Update safe seq for message source
    prev_safe = update_im_safe_seq(inst.index(), msg.aru_seq());

    profile_leave(input_map_prof_);

    // Check for missing messages
    if (range.hs()                         >  range.lu() &&
        (msg.flags() & Message::F_RETRANS) == 0                 )
    {
        evs_log_debug(D_RETRANS) << " requesting retrans from "
                                 << msg.source() << " "
                                 << range
                                 << " due to input map gap, aru "
                                 << input_map_->aru_seq();
        profile_enter(send_gap_prof_);
        gu_trace(send_gap(EVS_CALLER, msg.source(), current_view_.id(), range));
        profile_leave(send_gap_prof_);
    }

    // Seqno range completion and acknowledgement
    const seqno_t max_hs(input_map_->max_hs());
    if (output_.empty()                          == true            &&
        (state() == S_OPERATIONAL || state() == S_GATHER)  &&
        (msg.flags() & Message::F_MSG_MORE) == 0               &&
        (last_sent_                              <  max_hs))
    {
        // Message not originated from this instance, output queue is empty
        // and last_sent seqno should be advanced
        gu_trace(complete_user(max_hs));
    }
    else if (output_.empty()           == true  &&
             input_map_->aru_seq() != prev_aru)
    {
        // Output queue empty and aru changed, send gap to inform others
        evs_log_debug(D_GAP_MSGS) << "sending empty gap";
        profile_enter(send_gap_prof_);
        gu_trace(send_gap(EVS_CALLER, UUID::nil(), current_view_.id(), Range()));
        profile_leave(send_gap_prof_);
    }

    // Send messages
    if (state() == S_OPERATIONAL)
    {
        size_t n_sent(0);
        profile_enter(send_user_prof_);
        while (output_.empty() == false)
        {
            int err;
            gu_trace(err = send_user(send_window_));
            if (err != 0)
            {
                if (err == EAGAIN && n_sent == 0)
                {
                    // If the send window was exhausted, send a gap
                    // message to advance aru_seq/safe_seq on peers.
                    gu_trace(send_gap(EVS_CALLER, UUID::nil(),
                                      current_view_.id(), Range()));
                }
                break;
            }
            else
            {
                ++n_sent;
            }
        }
        profile_leave(send_user_prof_);
    }

    // Deliver messages
    profile_enter(delivery_prof_);
    gu_trace(deliver());
    gu_trace(deliver_local());
    profile_leave(delivery_prof_);

    // If in recovery state, send join each time input map aru seq reaches
    // last sent and either input map aru or safe seq has changed.
    if (state()                  == S_GATHER &&
        consensus_.highest_reachable_safe_seq() == input_map_->aru_seq() &&
        (prev_aru                    != input_map_->aru_seq() ||
         prev_safe                   != input_map_->safe_seq()) &&
        (msg.flags() & Message::F_RETRANS) == 0)
    {
        gcomm_assert(output_.empty() == true);
        if (consensus_.is_consensus() == false)
        {
            profile_enter(send_join_prof_);
            gu_trace(send_join());
            profile_leave(send_join_prof_);
        }
    }
    if (shift_to_gather) {
        shift_to(S_GATHER, true);
    }
}


void gcomm::evs::Proto::handle_delegate(const DelegateMessage& msg,
                                        NodeMap::iterator ii,
                                        const Datagram& rb)
{
    gcomm_assert(ii != known_.end());
    evs_log_debug(D_DELEGATE_MSGS) << "delegate message " << msg;
    Message umsg;
    size_t offset;
    gu_trace(offset = unserialize_message(UUID::nil(), rb, &umsg));
    gu_trace(handle_msg(umsg, Datagram(rb, offset), false));
}


void gcomm::evs::Proto::handle_gap(const GapMessage& msg, NodeMap::iterator ii)
{
    assert(ii != known_.end());
    assert(state() != S_CLOSED && state() != S_JOINING);

    Node& inst(NodeMap::value(ii));
    evs_log_debug(D_GAP_MSGS) << "gap message " << msg;


    if ((msg.flags() & Message::F_COMMIT) != 0)
    {
        log_debug << self_string() << " commit gap from " << msg.source();
        if (state()                             == S_GATHER             &&
            install_message_                    != 0                    &&
            install_message_->install_view_id() == msg.source_view_id() &&
            install_message_->fifo_seq()        == msg.seq())
        {
            inst.set_committed(true);
            inst.set_tstamp(gu::datetime::Date::now());
            if (is_all_committed() == true)
            {
                shift_to(S_INSTALL);
                gu_trace(send_gap(EVS_CALLER, UUID::nil(),
                                  install_message_->install_view_id(),
                                  Range()));;
            }
        }
        else if (state()                             == S_GATHER             &&
                 install_message_                    != 0                    &&
                 install_message_->install_view_id() == msg.source_view_id() &&
                 install_message_->fifo_seq()        < msg.seq())
        {
            // new install message has been generated
            shift_to(S_GATHER, true);
        }
        else
        {
            evs_log_debug(D_GAP_MSGS) << " unhandled commit gap " << msg;
        }
        return;
    }
    else if (state()                           == S_INSTALL  &&
             install_message_                       != 0          &&
             install_message_->install_view_id() == msg.source_view_id())
    {
        evs_log_debug(D_STATE) << "install gap " << msg;
        inst.set_installed(true);
        inst.set_tstamp(gu::datetime::Date::now());
        if (is_all_installed() == true)
        {
            profile_enter(shift_to_prof_);
            gu_trace(shift_to(S_OPERATIONAL));
            profile_leave(shift_to_prof_);
            if (pending_leave_ == true)
            {
                close();
            }
        }
        return;
    }
    else if (msg.source_view_id() != current_view_.id())
    {
        if (state() == S_LEAVING)
        {
            // Silently drop
            return;
        }

        if (is_msg_from_previous_view(msg) == true)
        {
            evs_log_debug(D_FOREIGN_MSGS) << "gap message from previous view";
            return;
        }

        if (inst.operational() == false)
        {
            evs_log_debug(D_STATE)
                << "dropping message from unoperational source "
                << msg.source();
        }
        else if (inst.installed() == false)
        {
            evs_log_debug(D_STATE)
                << "dropping message from uninstalled source "
                << msg.source();
        }
        else
        {
            log_debug << "unhandled gap message " << msg;
        }
        return;
    }

    gcomm_assert(msg.source_view_id() == current_view_.id());

    //
    seqno_t prev_safe;

    profile_enter(input_map_prof_);
    prev_safe = update_im_safe_seq(inst.index(), msg.aru_seq());

    // Deliver messages and update tstamp only if safe_seq changed
    // for the source.
    if (prev_safe != input_map_->safe_seq(inst.index()))
    {
        inst.set_tstamp(gu::datetime::Date::now());
    }
    profile_leave(input_map_prof_);

    //
    if (msg.range_uuid() == uuid())
    {
        if (msg.range().hs() > last_sent_ &&
            (state() == S_OPERATIONAL || state() == S_GATHER))
        {
            // This could be leaving node requesting messages up to
            // its last sent.
            gu_trace(complete_user(msg.range().hs()));
        }
        const seqno_t upper_bound(
            std::min(msg.range().hs(), last_sent_));
        if (msg.range().lu() <= upper_bound)
        {
            gu_trace(resend(msg.source(),
                            Range(msg.range().lu(), upper_bound)));
        }
    }
    else if ((msg.flags() & Message::F_RETRANS) != 0 &&
             msg.source() != uuid())
    {
        gu_trace(recover(msg.source(), msg.range_uuid(), msg.range()));
    }

    //
    if (state() == S_OPERATIONAL)
    {
        if (output_.empty() == false)
        {
            profile_enter(send_user_prof_);
            while (output_.empty() == false)
            {
                int err;
                gu_trace(err = send_user(send_window_));
                if (err != 0)
                    break;
            }
            profile_leave(send_user_prof_);
        }
        else
        {
            const seqno_t max_hs(input_map_->max_hs());
            if (last_sent_ <  max_hs)
            {
                gu_trace(complete_user(max_hs));
            }
        }
    }

    profile_enter(delivery_prof_);
    gu_trace(deliver());
    gu_trace(deliver_local());
    profile_leave(delivery_prof_);

    //
    if (state()                            == S_GATHER                  &&
        consensus_.highest_reachable_safe_seq() == input_map_->aru_seq()  &&
        prev_safe                              != input_map_->safe_seq()   )
    {
        gcomm_assert(output_.empty() == true);
        if (consensus_.is_consensus() == false)
        {
            profile_enter(send_join_prof_);
            gu_trace(send_join());
            profile_leave(send_join_prof_);
        }
    }
}


bool gcomm::evs::Proto::update_im_safe_seqs(const MessageNodeList& node_list)
{
    bool updated = false;
    // Update input map state
    for (MessageNodeList::const_iterator i = node_list.begin();
         i != node_list.end(); ++i)
    {
        const UUID& node_uuid(MessageNodeList::key(i));
        const Node& local_node(NodeMap::value(known_.find_checked(node_uuid)));
        const MessageNode& node(MessageNodeList::value(i));
        gcomm_assert(node.view_id() == current_view_.id());
        const seqno_t safe_seq(node.safe_seq());
        seqno_t prev_safe_seq;
        gu_trace(prev_safe_seq = update_im_safe_seq(local_node.index(), safe_seq));
        if (prev_safe_seq                 != safe_seq &&
            input_map_->safe_seq(local_node.index()) == safe_seq)
        {
            updated = true;
        }
    }
    return updated;
}


void gcomm::evs::Proto::retrans_user(const UUID& nl_uuid,
                                     const MessageNodeList& node_list)
{
    for (MessageNodeList::const_iterator i = node_list.begin();
         i != node_list.end(); ++i)
    {
        const UUID& node_uuid(MessageNodeList::key(i));
        const MessageNode& mn(MessageNodeList::value(i));
        const Node& n(NodeMap::value(known_.find_checked(node_uuid)));
        const Range r(input_map_->range(n.index()));

        if (node_uuid == uuid() &&
            mn.im_range().lu() != r.lu())
        {
            // Source member is missing messages from us
            gcomm_assert(mn.im_range().hs() <= last_sent_);
            gu_trace(resend(nl_uuid,
                            Range(mn.im_range().lu(), last_sent_)));
        }
        else if ((mn.operational() == false ||
                  mn.leaving() == true) &&
                 node_uuid != uuid() &&
                 (mn.im_range().lu() < r.lu() ||
                  mn.im_range().hs() < r.hs()))
        {
            gu_trace(recover(nl_uuid, node_uuid,
                             Range(mn.im_range().lu(),
                                   r.hs())));
        }
    }
}

void gcomm::evs::Proto::retrans_leaves(const MessageNodeList& node_list)
{
    for (NodeMap::const_iterator li = known_.begin(); li != known_.end(); ++li)
    {
        const Node& local_node(NodeMap::value(li));
        if (local_node.leave_message() != 0 &&
            local_node.is_inactive()       == false)
        {
            MessageNodeList::const_iterator msg_li(
                node_list.find(NodeMap::key(li)));

            if (msg_li == node_list.end() ||
                MessageNodeList::value(msg_li).leaving() == false)
            {
                const LeaveMessage& lm(*NodeMap::value(li).leave_message());
                LeaveMessage send_lm(lm.version(),
                                     lm.source(),
                                     lm.source_view_id(),
                                     lm.seq(),
                                     lm.aru_seq(),
                                     lm.fifo_seq(),
                                     Message::F_RETRANS | Message::F_SOURCE);

                gu::Buffer buf;
                serialize(send_lm, buf);
                Datagram dg(buf);
                gu_trace(send_delegate(dg));
            }
        }
    }
}


class SelectSuspectsOp
{
public:
    SelectSuspectsOp(gcomm::evs::MessageNodeList& nl) : nl_(nl) { }

    void operator()(const gcomm::evs::MessageNodeList::value_type& vt) const
    {
        if (gcomm::evs::MessageNodeList::value(vt).suspected() == true)
        {
            nl_.insert_unique(vt);
        }
    }
private:
    gcomm::evs::MessageNodeList& nl_;
};

void gcomm::evs::Proto::check_suspects(const UUID& source,
                                       const MessageNodeList& nl)
{
    assert(source != uuid());
    MessageNodeList suspected;
    for_each(nl.begin(), nl.end(), SelectSuspectsOp(suspected));

    for (MessageNodeList::const_iterator i(suspected.begin());
         i != suspected.end(); ++i)
    {
        const UUID& node_uuid(MessageNodeList::key(i));
        const MessageNode& node(MessageNodeList::value(i));
        if (node.suspected() == true)
        {
            if (node_uuid != uuid())
            {
                size_t s_cnt(0);
                // Iterate over join messages to see if majority of current
                // view agrees with the suspicion
                for (NodeMap::const_iterator j(known_.begin());
                     j != known_.end(); ++j)
                {
                    const JoinMessage* jm(NodeMap::value(j).join_message());
                    if (jm != 0 && jm->source() != node_uuid &&
                        current_view_.is_member(jm->source()) == true)
                    {
                        MessageNodeList::const_iterator mni(jm->node_list().find(node_uuid));
                        if (mni != jm->node_list().end())
                        {
                            const MessageNode& mn(MessageNodeList::value(mni));
                            if (mn.suspected() == true)
                            {
                                ++s_cnt;
                            }
                        }
                    }
                }
                const Node& kn(NodeMap::value(known_.find_checked(node_uuid)));
                if (kn.operational() == true &&
                    s_cnt > current_view_.members().size()/2)
                {
                    evs_log_info(I_STATE)
                        << " declaring suspected "
                        << node_uuid << " as inactive";
                    set_inactive(node_uuid);
                }
            }
        }
    }
}


void gcomm::evs::Proto::cross_check_inactives(const UUID& source,
                                              const MessageNodeList& nl)
{
    assert(source != uuid());

    // Do elimination by suspect status
    NodeMap::const_iterator source_i(known_.find_checked(source));

    for (MessageNodeList::const_iterator i(nl.begin()); i != nl.end(); ++i)
    {
        const UUID& node_uuid(MessageNodeList::key(i));
        const MessageNode& node(MessageNodeList::value(i));
        if (node.operational() == false)
        {
            NodeMap::iterator local_i(known_.find(node_uuid));
            if (local_i != known_.end() && node_uuid != uuid())
            {
                const Node& local_node(NodeMap::value(local_i));
                if (local_node.suspected())
                {
                    // This node is suspecting and the source node has
                    // already set inactve, mark also locally inactive.
                    set_inactive(node_uuid);
                }
            }
        }
    }
}


// Asymmetry elimination:
// 1a) Find all joins that has this node marked as operational and which
//     this node considers operational
// 1b) Mark all operational nodes without join message unoperational
// 2) Iterate over join messages gathered in 1a, find all
//    unoperational entries and mark them unoperational too
void gcomm::evs::Proto::asymmetry_elimination()
{
    // Allow some time to pass from setting install timers to get
    // join messages accumulated.
    const gu::datetime::Date now(gu::datetime::Date::now());
    TimerList::const_iterator ti(
        find_if(timers_.begin(), timers_.end(), TimerSelectOp(T_INSTALL)));

    assert(ti != timers_.end());
    if (ti == timers_.end())
    {
        log_warn << "install timer not set in asymmetry_elimination()";
        return;
    }

    if (install_timeout_ - suspect_timeout_ < TimerList::key(ti) - now)
    {
        // No check yet
        return;
    }

    // Record initial operational state for logging
    std::vector<int> oparr_before(known_.size());
    size_t index(0);
    for (NodeMap::const_iterator i(known_.begin()); i != known_.end(); ++i)
    {
        oparr_before[index] = (NodeMap::value(i).operational() == true);
        index++;
    }
    std::list<const JoinMessage*> joins;

    // Compose list of join messages
    for (NodeMap::const_iterator i(known_.begin()); i != known_.end(); ++i)
    {
        const UUID& node_uuid(NodeMap::key(i));
        const Node& node(NodeMap::value(i));
        const JoinMessage* jm(node.join_message());
        if (jm != 0)
        {
            MessageNodeList::const_iterator self_ref(
                jm->node_list().find(uuid()));
            if (node.operational() == true                           &&
                self_ref           != jm->node_list().end()          &&
                MessageNodeList::value(self_ref).operational() == true)
            {
                joins.push_back(NodeMap::value(i).join_message());
            }
        }
        else if (node.operational() == true)
        {
            evs_log_info(I_STATE)
                << "marking operational node "
                << node_uuid << " without "
                << "join message inactive in asymmetry elimination";
            set_inactive(node_uuid);
        }
    }

    // Setting node inactive may remove join message and so invalidate
    // pointer in joins list, so collect set of UUIDs to set inactive
    // and do inactivation in separate loop.
    std::set<UUID> to_inactive;
    // Iterate over join messages and collect nodes to be set inactive
    for (std::list<const JoinMessage*>::const_iterator i(joins.begin());
         i != joins.end(); ++i)
    {
        for (MessageNodeList::const_iterator j((*i)->node_list().begin());
             j != (*i)->node_list().end(); ++j)
        {
            if (MessageNodeList::value(j).operational() == false)
            {
                to_inactive.insert(MessageNodeList::key(j));
            }
        }
    }
    joins.clear();
    for (std::set<UUID>::const_iterator i(to_inactive.begin());
         i != to_inactive.end(); ++i)
    {
        NodeMap::const_iterator ni(known_.find(*i));
        if (ni != known_.end())
        {
            if (NodeMap::value(ni).operational() == true)
            {
                evs_log_info(I_STATE) << "setting " << *i
                                      << " inactive in asymmetry elimination";
                set_inactive(*i);
            }
        }
        else
        {
            log_warn << "node " << *i << " not found from known list in ae";
        }
    }

    // Compute final state and log if it has changed
    std::vector<int> oparr_after(known_.size());
    index = 0;
    for (NodeMap::const_iterator i(known_.begin()); i != known_.end(); ++i)
    {
        oparr_after[index] = (NodeMap::value(i).operational() == true);
        index++;
    }

    if (oparr_before != oparr_after)
    {
        evs_log_info(I_STATE) << "before asym elimination";
        if (info_mask_ & I_STATE)
        {
            std::copy(oparr_before.begin(), oparr_before.end(),
                      std::ostream_iterator<int>(std::cerr, " "));
            std::cerr << "\n";
        }

        evs_log_info(I_STATE) << "after asym elimination";
        if (info_mask_ & I_STATE)
        {
            std::copy(oparr_after.begin(), oparr_after.end(),
                      std::ostream_iterator<int>(std::cerr, " "));
            std::cerr << "\n";
        }
    }
}

// For each node thas has no join message associated, iterate over other
// known nodes' join messages to find out if the node without join message
// should be declared inactive.
void gcomm::evs::Proto::check_unseen()
{
    for (NodeMap::iterator i(known_.begin()); i != known_.end(); ++i)
    {

        const UUID& node_uuid(NodeMap::key(i));
        Node& node(NodeMap::value(i));

        if (node_uuid                          != uuid() &&
            current_view_.is_member(node_uuid) == false  &&
            node.join_message()                == 0      &&
            node.operational()                 == true)
        {
            evs_log_debug(D_STATE) << "checking operational unseen "
                                   << node_uuid;
            size_t cnt(0), inact_cnt(0);
            for (NodeMap::iterator j(known_.begin()); j != known_.end(); ++j)
            {
                const JoinMessage* jm(NodeMap::value(j).join_message());
                if (jm == 0 || NodeMap::key(j) == uuid())
                {
                    continue;
                }
                MessageNodeList::const_iterator mn_i;
                for (mn_i = jm->node_list().begin();
                     mn_i != jm->node_list().end(); ++mn_i)
                {
                    NodeMap::const_iterator known_i(
                        known_.find(MessageNodeList::key(mn_i)));
                    if (known_i == known_.end() ||
                        (MessageNodeList::value(mn_i).operational() == true &&
                         NodeMap::value(known_i).join_message() == 0))
                    {
                        evs_log_debug(D_STATE)
                            << "all joins not locally present for "
                            << NodeMap::key(j)
                            << " join message node list";
                        return;
                    }
                }

                if ((mn_i = jm->node_list().find(node_uuid))
                    != jm->node_list().end())
                {
                    const MessageNode& mn(MessageNodeList::value(mn_i));
                    evs_log_debug(D_STATE)
                        << "found " << node_uuid << " from " <<  NodeMap::key(j)
                        << " join message: "
                        << mn.view_id() << " "
                        << mn.operational();
                    if (mn.view_id() != ViewId(V_REG))
                    {
                        ++cnt;
                        if (mn.operational() == false) ++inact_cnt;
                    }
                }
            }
            if (cnt > 0 && cnt == inact_cnt)
            {
                evs_log_info(I_STATE)
                    << "unseen node marked inactive by others (cnt="
                    << cnt
                    << ", inact_cnt="
                    << inact_cnt
                    << ")";
                set_inactive(node_uuid);
            }
        }
    }
}


// Iterate over all join messages. If some node has nil view id and suspected
// flag true in all present join messages, declare it inactive.
void gcomm::evs::Proto::check_nil_view_id()
{
    size_t join_counts(0);
    std::map<UUID, size_t > nil_counts;
    for (NodeMap::const_iterator i(known_.begin()); i != known_.end(); ++i)
    {
        const JoinMessage* jm(NodeMap::value(i).join_message());
        if (jm == 0)
        {
            continue;
        }
        ++join_counts;
        for (MessageNodeList::const_iterator j(jm->node_list().begin());
             j != jm->node_list().end(); ++j)
        {
            const MessageNode& mn(MessageNodeList::value(j));
            if (mn.view_id() == ViewId(V_REG))
            {
                // todo: investigate why removing mn.suspected() == true
                // condition causes some unit tests to fail
                if (mn.suspected() == true)
                {
                    const UUID& uuid(MessageNodeList::key(j));
                    ++nil_counts[uuid];
                }
            }
        }
    }
    for (std::map<UUID, size_t>::const_iterator
             i(nil_counts.begin()); i != nil_counts.end(); ++i)
    {
        if (i->second == join_counts && is_inactive(i->first) == false)
        {
            log_info << "node " << i->first
                     << " marked with nil view id and suspected in all present"
                     << " join messages, declaring inactive";
            set_inactive(i->first);
        }
    }
}


void gcomm::evs::Proto::handle_join(const JoinMessage& msg, NodeMap::iterator ii)
{
    assert(ii != known_.end());
    assert(state() != S_CLOSED);

    Node& inst(NodeMap::value(ii));

    evs_log_debug(D_JOIN_MSGS) << " " << msg;

    if (state() == S_LEAVING)
    {
        if (msg.source_view_id() == current_view_.id())
        {
            inst.set_tstamp(gu::datetime::Date::now());
            MessageNodeList same_view;
            for_each(msg.node_list().begin(), msg.node_list().end(),
                     SelectNodesOp(same_view, current_view_.id(),
                                   true, true));
            profile_enter(input_map_prof_);
            if (update_im_safe_seqs(same_view) == true)
            {
                profile_enter(send_leave_prof_);
                gu_trace(send_leave(false));
                profile_leave(send_leave_prof_);
            }
            for (NodeMap::const_iterator i = known_.begin(); i != known_.end();
                 ++i)
            {
                const UUID& uuid(NodeMap::key(i));
                const Node& node(NodeMap::value(i));
                if (current_view_.is_member(uuid) == true)
                {
                    const Range r(input_map_->range(node.index()));
                    if (r.lu() <= last_sent_)
                    {
                        send_gap(EVS_CALLER, uuid, current_view_.id(),
                                 Range(r.lu(), last_sent_));
                    }
                }
            }
            profile_leave(input_map_prof_);
            gu_trace(retrans_user(msg.source(), same_view));
        }
        return;
    }
    else if (is_msg_from_previous_view(msg) == true)
    {
        return;
    }
    else if (install_message_ != 0)
    {
        // Note: don't send join from this branch yet, join is
        // sent at the end of this method
        if (install_message_->source() == msg.source())
        {
            evs_log_info(I_STATE)
                << "shift to gather due to representative "
                << msg.source() << " join";
            if (msg.source_view_id() == install_message_->install_view_id())
            {
                // Representative reached operational state, we follow
                // Other instances installed view before this one, so it is
                // safe to shift to S_OPERATIONAL

                // Mark all operational nodes in install message as installed
                for (MessageNodeList::const_iterator
                         mi = install_message_->node_list().begin();
                     mi != install_message_->node_list().end(); ++mi)
                {
                    if (MessageNodeList::value(mi).operational() == true)
                    {
                        NodeMap::iterator jj;
                        gu_trace(jj = known_.find_checked(
                                     MessageNodeList::key(mi)));
                        NodeMap::value(jj).set_installed(true);
                    }
                }
                inst.set_tstamp(gu::datetime::Date::now());
                if (state() == S_INSTALL)
                {
                    profile_enter(shift_to_prof_);
                    gu_trace(shift_to(S_OPERATIONAL));
                    profile_leave(shift_to_prof_);
                    if (pending_leave_ == true)
                    {
                        close();
                        return;
                    }
                    // proceed to process actual join message
                }
                else
                {
                    log_warn << self_string()
                             << "received join message from new "
                             << "view while in GATHER, dropping";
                    return;
                }
            }
            gu_trace(shift_to(S_GATHER, false));
        }
        else if (consensus_.is_consistent(*install_message_) == true)
        {
            return;
            // Commented out: It seems to be better strategy to
            // just wait source of inconsistent join to time out
            // instead of shifting to gather. #443

            // if (consensus_.is_consistent(msg) == true)
            // {
            //   return;
            // }
            // else
            // {
            //   log_warn << "join message not consistent " << msg;
            //   log_info << "state (stderr): ";
            //   std::cerr << *this << std::endl;
            //
            // gu_trace(shift_to(S_GATHER, false));
            // }
        }
        else
        {
            evs_log_info(I_STATE)
                << "shift to GATHER, install message is "
                << "inconsistent when handling join from "
                << msg.source() << " " << msg.source_view_id();
            evs_log_info(I_STATE) << "state: " << *this;
            gu_trace(shift_to(S_GATHER, false));
        }
    }
    else if (state() != S_GATHER)
    {
        evs_log_info(I_STATE)
            << " shift to GATHER while handling join message from "
            << msg.source() << " " << msg.source_view_id();
        gu_trace(shift_to(S_GATHER, false));
    }

    gcomm_assert(output_.empty() == true);

    // If source node is member of current view but has already
    // formed new view, mark it unoperational
    if (current_view_.is_member(msg.source()) == true &&
        msg.source_view_id().seq() > current_view_.id().seq())
    {
        evs_log_info(I_STATE)
            << " join source has already formed new view, marking inactive";
        set_inactive(msg.source());
        return;
    }

    // Collect view ids to gather_views_ list.
    // Add unseen nodes to known list and evicted nodes to evicted list.
    // Evicted nodes must also be added to known list for GATHER time
    // bookkeeping.
    // No need to adjust node state here, it is done later on in
    // check_suspects()/cross_check_inactives().
    for (MessageNodeList::const_iterator i(msg.node_list().begin());
         i != msg.node_list().end(); ++i)
    {
        NodeMap::iterator ni(known_.find(MessageNodeList::key(i)));
        const UUID mn_uuid(MessageNodeList::key(i));
        const MessageNode& mn(MessageNodeList::value(i));
        gather_views_.insert(std::make_pair(mn.view_id(),
                                            gu::datetime::Date::now()));
        if (ni == known_.end())
        {
            known_.insert_unique(
                std::make_pair(mn_uuid, Node(*this)));
        }

        // Evict nodes according to join message
        if (mn_uuid != uuid() && mn.evicted() == true)
        {
            set_inactive(mn_uuid);
            if (is_evicted(mn_uuid) == false)
            {
                evict(mn_uuid);
            }
        }
    }

    // Timestamp source if it sees processing node as operational.
    // Adjust local entry operational status.
    MessageNodeList::const_iterator self(msg.node_list().find(uuid()));
    if (msg.node_list().end()                      != self)
    {
        if(MessageNodeList::value(self).operational() == true)
        {
            inst.set_tstamp(gu::datetime::Date::now());
        }
        else
        {
            evs_log_info(I_STATE)
                << " declaring source " << msg.source()
                << " as inactive (mutual exclusion)";
            set_inactive(msg.source());
        }
    }
    inst.set_join_message(&msg);

    // Select nodes that are coming from the same view as seen by
    // message source
    MessageNodeList same_view;
    for_each(msg.node_list().begin(), msg.node_list().end(),
             SelectNodesOp(same_view, current_view_.id(), true, true));
    // Find out self from node list
    MessageNodeList::const_iterator nlself_i(same_view.find(uuid()));

    // Other node coming from the same view
    if (msg.source()         != uuid() &&
        msg.source_view_id() == current_view_.id())
    {
        gcomm_assert(nlself_i != same_view.end());
        // Update input map state
        (void)update_im_safe_seqs(same_view);

        // Find out max hs and complete up to that if needed
        MessageNodeList::const_iterator max_hs_i(
            max_element(same_view.begin(), same_view.end(), RangeHsCmp()));
        const seqno_t max_hs(MessageNodeList::value(max_hs_i).im_range().hs());
        if (last_sent_ < max_hs)
        {
            gu_trace(complete_user(max_hs));
        }
    }

    //
    gu_trace(retrans_user(msg.source(), same_view));
    // Retrans leave messages that others are missing
    gu_trace(retrans_leaves(same_view));

    // Make cross check to resolve conflict if two nodes
    // declare each other inactive. There is no need to make
    // this for own messages.
    if (msg.source() != uuid())
    {
        gu_trace(check_suspects(msg.source(), same_view));
        gu_trace(cross_check_inactives(msg.source(), same_view));
        gu_trace(check_unseen());
        gu_trace(check_nil_view_id());
    }

    // Eliminate asymmetry according to operational status flags in
    // join messages
    gu_trace(asymmetry_elimination());

    // If current join message differs from current state, send new join
    const JoinMessage* curr_join(NodeMap::value(self_i_).join_message());
    MessageNodeList new_nl;
    populate_node_list(&new_nl);

    if (curr_join == 0 ||
        (curr_join->aru_seq()   != input_map_->aru_seq()  ||
         curr_join->seq()       != input_map_->safe_seq() ||
         curr_join->node_list() != new_nl))
    {
        gu_trace(create_join());
        if (consensus_.is_consensus() == false)
        {
            send_join(false);
        }
    }

    if (consensus_.is_consensus() == true)
    {
        if (is_representative(uuid()) == true)
        {
            gu_trace(send_install(EVS_CALLER));
        }
    }
}


void gcomm::evs::Proto::handle_leave(const LeaveMessage& msg,
                                     NodeMap::iterator ii)
{
    assert(ii != known_.end());
    assert(state() != S_CLOSED && state() != S_JOINING);

    Node& node(NodeMap::value(ii));
    evs_log_debug(D_LEAVE_MSGS) << "leave message " << msg;

    if (msg.source() != uuid() && node.is_inactive() == true)
    {
        evs_log_debug(D_LEAVE_MSGS) << "dropping leave from already inactive";
        return;
    }
    node.set_leave_message(&msg);
    if (msg.source() == uuid())
    {
        // The last one to live, instant close. Otherwise continue
        // serving until it becomes apparent that others have
        // leave message.
        if (current_view_.members().size() == 1)
        {
            profile_enter(shift_to_prof_);
            gu_trace(shift_to(S_CLOSED));
            profile_leave(shift_to_prof_);
        }
    }
    else
    {
        // Always set node nonoperational if leave message is seen
        node.set_operational(false);
        if (msg.source_view_id()       != current_view_.id() ||
            is_msg_from_previous_view(msg) == true)
        {
            // Silent drop
            return;
        }

        const seqno_t prev_safe_seq(update_im_safe_seq(node.index(), msg.aru_seq()));
        if (prev_safe_seq != input_map_->safe_seq(node.index()))
        {
            node.set_tstamp(gu::datetime::Date::now());
        }
        if (state() == S_OPERATIONAL)
        {
            profile_enter(shift_to_prof_);
            evs_log_info(I_STATE)
                << " shift to GATHER when handling leave from "
                << msg.source() << " " << msg.source_view_id();
            gu_trace(shift_to(S_GATHER, true));
            profile_leave(shift_to_prof_);
        }
        else if (state() == S_GATHER &&
                 prev_safe_seq != input_map_->safe_seq(node.index()))
        {
            profile_enter(send_join_prof_);
            gu_trace(send_join());
            profile_leave(send_join_prof_);
        }
    }
}


void gcomm::evs::Proto::handle_install(const InstallMessage& msg,
                                       NodeMap::iterator ii)
{

    assert(ii != known_.end());
    assert(state() != S_CLOSED && state() != S_JOINING);

    Node& inst(NodeMap::value(ii));

    evs_log_debug(D_INSTALL_MSGS) << "install msg " << msg;

    if (state() == S_LEAVING)
    {
        // Check if others have receievd leave message or declared
        // as unoperational before shifting to closed.
        MessageNodeList::const_iterator mn_i(msg.node_list().find(uuid()));
        if (mn_i != msg.node_list().end())
        {
            const MessageNode& mn(MessageNodeList::value(mn_i));
            if (mn.operational() == false || mn.leaving() == true)
            {
                profile_enter(shift_to_prof_);
                gu_trace(shift_to(S_CLOSED));
                profile_leave(shift_to_prof_);
            }
        }
        return;
    }
    else if (state() == S_OPERATIONAL)
    {
        // Drop install messages in operational state.
        evs_log_debug(D_INSTALL_MSGS)
            << "dropping install message in already installed view";
        return;
    }
    else if (inst.operational() == false)
    {
        // Message source is not seen as operational, must not accept
        // anything from it.
        evs_log_debug(D_INSTALL_MSGS)
            << "install message source " << msg.source()
            << " is not operational, discarding message";
        return;
    }
    else if (is_msg_from_previous_view(msg) == true)
    {
        // Delayed install message
        evs_log_debug(D_FOREIGN_MSGS)
            << " dropping install message from previous view";
        return;
    }
    else if (install_message_ != 0)
    {
        if (msg.source() == install_message_->source() &&
            msg.install_view_id().seq() > install_message_->install_view_id().seq())
        {
            // Representative regenerated install message
            evs_log_debug(D_INSTALL_MSGS)
                << "regenerated install message";
            setall_committed(false);
            setall_installed(false);
            delete install_message_;
            install_message_ = 0;
            // Fall through to process new install message
        }
        else if (msg.source() == install_message_->source())
        {
            // Duplicate or delayed install message
            evs_log_debug(D_INSTALL_MSGS)
                << "duplicate or delayed install message";
            return;
        }
        else
        {
            MessageNodeList::const_iterator self(msg.node_list().find(uuid()));
            if (msg.node_list().end()                      == self ||
                MessageNodeList::value(self).operational() == false)
            {
                evs_log_debug(D_INSTALL_MSGS)
                    << "dropping install message, processing node not in "
                    << "new view";
            }
            else
            {
                // Two nodes decided to generate install message simultaneously,
                // shift to gather to combine groups in install messages.
                log_warn << self_string()
                         << " shift to GATHER due to conflicting install "
                         << "messages";
                gu_trace(shift_to(S_GATHER));
            }
            return;
        }
    }
    else if (inst.installed() == true)
    {
        log_warn << self_string()
                 << " shift to GATHER due to inconsistent state";
        profile_enter(shift_to_prof_);
        gu_trace(shift_to(S_GATHER));
        profile_leave(shift_to_prof_);
        return;
    }

    // Construct join from install message so that the most recent
    // information from representative is updated to local state.
    if (msg.source() != uuid())
    {
        const MessageNode& mn(
            MessageNodeList::value(
                msg.node_list().find_checked(msg.source())));
        JoinMessage jm(msg.version(),
                       msg.source(),
                       mn.view_id(),
                       msg.seq(),
                       msg.aru_seq(),
                       msg.fifo_seq(),
                       msg.node_list());
        handle_join(jm, ii);
    }

    // Drop install message if processing node won't be part of the
    // view to be installed.
    // Don't set nodes that are forming another view inactive here,
    // they should enter new view shortly after install message
    // delivery and should be ready to restart GATHER round.
    MessageNodeList::const_iterator self(msg.node_list().find(uuid()));
    if (msg.node_list().end()                      == self ||
        MessageNodeList::value(self).operational() == false)
    {
        evs_log_debug(D_INSTALL_MSGS)
            << "dropping install message, processing node not in new view";
        return;
    }

    // Proceed to install phase
    assert(install_message_ == 0);

    // Run through known nodes and remove each entry that is
    // not member of current view or present in install message.
    // This is to prevent inconsistent view of group when first message(s)
    // from new node are received after install message on representative
    // and before install message on other nodes.
    bool changed(false);
    NodeMap::iterator i, i_next;
    for (NodeMap::iterator i(known_.begin()); i != known_.end(); i = i_next)
    {
        i_next = i, ++i_next;
        const UUID& uuid(NodeMap::key(i));
        if (msg.node_list().find(uuid)         == msg.node_list().end() &&
            current_view_.members().find(uuid) == current_view_.members().end())
        {
            log_info << self_string() << " temporarily discarding known "
                     << uuid << " due to received install message";
            known_.erase(i);
            changed = true;
        }
    }

    // Recreate join message to match current state, otherwise is_consistent()
    // below will fail.
    if (changed == true)
    {
        (void)create_join();
    }

    // See if install message is consistent with local state.
    // Is_consistent() checks only local state and local join
    // message in case other nodes have already been seen and reported
    // nodes that will not be in the next view.
    if (consensus_.is_consistent(msg) == true)
    {
        inst.set_tstamp(gu::datetime::Date::now());
        install_message_ = new InstallMessage(msg);
        assert(install_message_->source() != UUID::nil());
        assert(install_message_->flags() != 0);
        profile_enter(send_gap_prof_);
        // Send commit gap
        gu_trace(send_gap(EVS_CALLER, UUID::nil(), install_message_->install_view_id(),
                          Range(), true));
        profile_leave(send_gap_prof_);
    }
    else
    {
        evs_log_debug(D_INSTALL_MSGS)
            << "install message " << msg
            << " not consistent with state " << *this;
        profile_enter(shift_to_prof_);
        gu_trace(shift_to(S_GATHER, true));
        profile_leave(shift_to_prof_);
    }
}


void gcomm::evs::Proto::handle_delayed_list(const DelayedListMessage& msg,
                                          NodeMap::iterator ii)
{
    if (auto_evict_ == 0)
    {
        // Ignore evict list messages if auto_evict_ is disabled.
        return;
    }

    Node& node(NodeMap::value(ii));
    node.set_delayed_list_message(&msg);
    gu::datetime::Date now(gu::datetime::Date::now());

    // Construct a list of evict candidates that appear in evict list messages
    // with cnt greater than local auto_evict_. If evict candidate is reported
    // by majority of the current group, evict process is triggered.

    // UUID -> over auto_evict_, total count
    typedef std::map<UUID, std::pair<size_t, size_t> > Evicts;
    Evicts evicts;
    bool found(false);

    for (NodeMap::const_iterator i(known_.begin()); i != known_.end(); ++i)
    {
        const DelayedListMessage* const dlm(
            NodeMap::value(i).delayed_list_message());
        if (dlm == 0)
        {
            continue;
        }
        else if (dlm->delayed_list().find(uuid()) != dlm->delayed_list().end())
        {
            evs_log_debug(D_STATE)
                << "found self " << uuid() << " from evict list from "
                << msg.source() << " at " << get_address(msg.source());
            continue;
        }
        else if (dlm->tstamp() + delayed_keep_period_ < now)
        {
            evs_log_debug(D_STATE) << "ignoring expired evict message";
            continue;
        }

        for (DelayedListMessage::DelayedList::const_iterator
                 dlm_i(dlm->delayed_list().begin());
             dlm_i != dlm->delayed_list().end();
             ++dlm_i)
        {
            if (dlm_i->second <= 1)
            {
                // Don't consider entries with single delayed event as
                // evict candidates.
                continue;
            }

            std::pair<Evicts::iterator, bool> eir(
                evicts.insert(
                    std::make_pair(
                        dlm_i->first, std::make_pair(0, 0))));
            evs_log_debug(D_STATE) << "eir " << eir.first->first
                                   << " " << eir.first->second.first
                                   << " " << eir.first->second.second;
            ++eir.first->second.second; // total count
            if (dlm_i->second >= auto_evict_)
            {
                ++eir.first->second.first; // over threshold count
                found = true;
            }
        }
    }

    // Evict candidates that have reached threshold count
    for (Evicts::const_iterator i(evicts.begin());
         found == true && i != evicts.end(); ++i)
    {
        if (is_evicted(i->first) == true)
        {
            // Already evicted, avoid spamming
            continue;
        }
        evs_log_info(I_STATE) << "evict candidate "
                              << i->first << " " << i->second.first
                              << " " << i->second.second;
        // If the candidate is in the current view, require majority
        // of the view to agree. If the candidate is not in the current
        // view, require majority of known nodes to agree. Ability to
        // evict nodes outside of the group (even while in non-PC) is
        // needed to stabilize cluster also in the case that nodes
        // have already partitioned.

        // TODO: Record stable views from PC and use weights from there
        // accordingly (need to be added to view).
        if (i->second.first != 0 &&
            ((current_view_.is_member(i->first) &&
              i->second.second > current_view_.members().size()/2) ||
             i->second.second > known_.size()/2))
        {
            log_warn << "evicting member " << i->first
                     << " at " << get_address(i->first)
                     << " permanently from group";
            evict(i->first);
            if (state() == S_OPERATIONAL)
            {
                shift_to(S_GATHER, true);
            }
        }
    }
}
