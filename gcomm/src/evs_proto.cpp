/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
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

#include <stdexcept>
#include <algorithm>
#include <numeric>

using namespace std;
using namespace std::rel_ops;
using namespace gu;
using namespace gu::net;
using namespace gu::datetime;
using namespace gcomm;
using namespace gcomm::evs;

// Convenience macros for debug and info logging
#define evs_log_debug(__mask__)             \
    if ((debug_mask & (__mask__)) == 0) { } \
    else log_debug << self_string() << ": "

#define evs_log_info(__mask__)              \
    if ((info_mask & (__mask__)) == 0) { }  \
    else log_info << self_string() << ": "


// const int gcomm::evs::Proto::max_version_(GCOMM_EVS_MAX_VERSION);

gcomm::evs::Proto::Proto(gu::Config& conf,
                         const UUID& my_uuid_,
                         const gu::URI& uri,
                         const size_t mtu_)
    :
    Protolay(conf),
    timers(),
    version(check_range(Conf::EvsVersion,
                        param<int>(conf, uri, Conf::EvsVersion, "0"),
                        0, max_version_ + 1)),
    debug_mask(param<int>(conf, uri, Conf::EvsDebugLogMask, "0x1", std::hex)),
    info_mask(param<int>(conf, uri, Conf::EvsInfoLogMask, "0x0", std::hex)),
    last_stats_report(Date::now()),
    collect_stats(true),
    hs_agreed("0.0,0.0005,0.001,0.002,0.005,0.01,0.02,0.05,0.1,0.5,1.,5.,10.,30."),
    hs_safe("0.0,0.0005,0.001,0.002,0.005,0.01,0.02,0.05,0.1,0.5,1.,5.,10.,30."),
    hs_local_causal("0.0,0.0005,0.001,0.002,0.005,0.01,0.02,0.05,0.1,0.5,1.,5.,10.,30."),
    send_queue_s(0),
    n_send_queue_s(0),
    sent_msgs(7, 0),
    retrans_msgs(0),
    recovered_msgs(0),
    recvd_msgs(7, 0),
    delivered_msgs(O_LOCAL_CAUSAL + 1),
    send_user_prof    ("send_user"),
    send_gap_prof     ("send_gap"),
    send_join_prof    ("send_join"),
    send_install_prof ("send_install"),
    send_leave_prof   ("send_leave"),
    consistent_prof   ("consistent"),
    consensus_prof    ("consensus"),
    shift_to_prof     ("shift_to"),
    input_map_prof    ("input_map"),
    delivery_prof     ("delivery"),
    delivering(false),
    my_uuid(my_uuid_),
    known(),
    self_i(),
    view_forget_timeout(
        check_range(Conf::EvsViewForgetTimeout,
                    param<Period>(conf, uri, Conf::EvsViewForgetTimeout,
                                  Defaults::EvsViewForgetTimeout),
                    gu::from_string<Period>(Defaults::EvsViewForgetTimeoutMin),
                    Period::max())),
    inactive_timeout(
        check_range(Conf::EvsInactiveTimeout,
                    param<Period>(conf, uri, Conf::EvsInactiveTimeout,
                                  Defaults::EvsInactiveTimeout),
                    gu::from_string<Period>(Defaults::EvsInactiveTimeoutMin),
                    Period::max())),
    suspect_timeout(
        check_range(Conf::EvsSuspectTimeout,
                    param<Period>(conf, uri, Conf::EvsSuspectTimeout,
                                  Defaults::EvsSuspectTimeout),
                    gu::from_string<Period>(Defaults::EvsSuspectTimeoutMin),
                    Period::max())),
    inactive_check_period(
        check_range(Conf::EvsInactiveCheckPeriod,
                    param<Period>(conf, uri, Conf::EvsInactiveCheckPeriod,
                                  Defaults::EvsInactiveCheckPeriod),
                    Period::min(),
                    suspect_timeout/2 + 1)),
    retrans_period(
        check_range(Conf::EvsKeepalivePeriod,
                    param<Period>(conf, uri, Conf::EvsKeepalivePeriod,
                                  Defaults::EvsRetransPeriod),
                    gu::from_string<Period>(Defaults::EvsRetransPeriodMin),
                    suspect_timeout/3 + 1)),
    install_timeout(
        check_range(Conf::EvsInstallTimeout,
                    param<Period>(conf, uri, Conf::EvsInstallTimeout,
                                  gu::to_string(inactive_timeout)),
                    retrans_period*2, inactive_timeout + 1)),
    join_retrans_period(
        check_range(Conf::EvsJoinRetransPeriod,
                    param<Period>(conf, uri, Conf::EvsJoinRetransPeriod,
                                  Defaults::EvsJoinRetransPeriod),
                    gu::from_string<Period>(Defaults::EvsRetransPeriodMin),
                    Period::max())),
    stats_report_period(
        check_range(Conf::EvsStatsReportPeriod,
                    param<Period>(conf, uri, Conf::EvsStatsReportPeriod,
                                  Defaults::EvsStatsReportPeriod),
                    gu::from_string<Period>(Defaults::EvsStatsReportPeriodMin),
                    Period::max())),
    last_inactive_check   (gu::datetime::Date::now()),
    current_view(ViewId(V_TRANS, my_uuid, 0)),
    previous_view(),
    previous_views(),
    input_map(new InputMap()),
    causal_queue_(),
    consensus(my_uuid, known, *input_map, current_view),
    install_message(0),
    attempt_seq(1),
    max_install_timeouts(
        check_range(Conf::EvsMaxInstallTimeouts,
                    param<int>(conf, uri, Conf::EvsMaxInstallTimeouts,
                               Defaults::EvsMaxInstallTimeouts),
                    0, std::numeric_limits<int>::max())),
    install_timeout_count(0),
    fifo_seq(-1),
    last_sent(-1),
    send_window(
        check_range(Conf::EvsSendWindow,
                    param<seqno_t>(conf, uri, Conf::EvsSendWindow,
                                   Defaults::EvsSendWindow),
                    gu::from_string<seqno_t>(Defaults::EvsSendWindowMin),
                    std::numeric_limits<seqno_t>::max())),
    user_send_window(
        check_range(Conf::EvsUserSendWindow,
                    param<seqno_t>(conf, uri, Conf::EvsUserSendWindow,
                                   Defaults::EvsUserSendWindow),
                    gu::from_string<seqno_t>(Defaults::EvsUserSendWindowMin),
                    send_window + 1)),
    output(),
    send_buf_(),
    max_output_size(128),
    mtu(mtu_),
    use_aggregate(param<bool>(conf, uri, Conf::EvsUseAggregate, "true")),
    self_loopback(false),
    state(S_CLOSED),
    shift_to_rfcnt(0)
{
    log_info << "EVS version " << version;

    conf.set(Conf::EvsVersion, gu::to_string(version));
    conf.set(Conf::EvsViewForgetTimeout, gu::to_string(view_forget_timeout));
    conf.set(Conf::EvsSuspectTimeout, gu::to_string(suspect_timeout));
    conf.set(Conf::EvsInactiveTimeout, gu::to_string(inactive_timeout));
    conf.set(Conf::EvsKeepalivePeriod, gu::to_string(retrans_period));
    conf.set(Conf::EvsInactiveCheckPeriod,
             gu::to_string(inactive_check_period));
    conf.set(Conf::EvsJoinRetransPeriod, gu::to_string(join_retrans_period));
    conf.set(Conf::EvsInstallTimeout, gu::to_string(install_timeout));
    conf.set(Conf::EvsStatsReportPeriod, gu::to_string(stats_report_period));
    conf.set(Conf::EvsSendWindow, gu::to_string(send_window));
    conf.set(Conf::EvsUserSendWindow, gu::to_string(user_send_window));
    conf.set(Conf::EvsUseAggregate, gu::to_string(use_aggregate));
    conf.set(Conf::EvsDebugLogMask, gu::to_string(debug_mask, std::hex));
    conf.set(Conf::EvsInfoLogMask, gu::to_string(info_mask, std::hex));
    conf.set(Conf::EvsMaxInstallTimeouts, gu::to_string(max_install_timeouts));
    //

    known.insert_unique(make_pair(my_uuid, Node(inactive_timeout, suspect_timeout)));
    self_i = known.begin();
    assert(NodeMap::get_value(self_i).get_operational() == true);

    NodeMap::get_value(self_i).set_index(0);
    input_map->reset(1);
    current_view.add_member(my_uuid, "");
    if (mtu_ != numeric_limits<size_t>::max())
    {
        send_buf_.reserve(mtu_);
    }
}


gcomm::evs::Proto::~Proto()
{
    output.clear();
    delete install_message;
    delete input_map;
}


bool
gcomm::evs::Proto::set_param(const std::string& key, const std::string& val)
{
    if (key == gcomm::Conf::EvsSendWindow)
    {
        send_window = check_range(Conf::EvsSendWindow,
                                  gu::from_string<seqno_t>(val),
                                  user_send_window,
                                  std::numeric_limits<seqno_t>::max());
        conf_.set(Conf::EvsSendWindow, gu::to_string(send_window));
        return true;
    }
    else if (key == gcomm::Conf::EvsUserSendWindow)
    {
        user_send_window = check_range(
            Conf::EvsUserSendWindow,
            gu::from_string<seqno_t>(val),
            gu::from_string<seqno_t>(Defaults::EvsUserSendWindowMin),
            send_window + 1);
        conf_.set(Conf::EvsUserSendWindow, gu::to_string(user_send_window));
        return true;
    }
    else if (key == gcomm::Conf::EvsMaxInstallTimeouts)
    {
        max_install_timeouts = check_range(
            Conf::EvsMaxInstallTimeouts,
            gu::from_string<int>(val),
            0, std::numeric_limits<int>::max());
        conf_.set(Conf::EvsMaxInstallTimeouts, gu::to_string(max_install_timeouts));
        return true;
    }
    return false;
}


ostream& gcomm::evs::operator<<(ostream& os, const Proto& p)
{
    os << "evs::proto("
       << p.self_string() << ", "
       << p.to_string(p.get_state()) << ") {\n";
    os << "current_view=" << p.current_view << ",\n";
    os << "input_map=" << *p.input_map << ",\n";
    os << "fifo_seq=" << p.fifo_seq << ",\n";
    os << "last_sent=" << p.last_sent << ",\n";
    os << "known={\n" << p.known << " } \n";
    if (p.install_message != 0)
        os << "install msg=" << *p.install_message << "\n";
    os << " }";
    return os;
}

string gcomm::evs::Proto::get_stats() const
{
    ostringstream os;
    os << "\n\tnodes " << current_view.get_members().size();
    os << "\n\tagreed deliv hist {" << hs_agreed << "} ";
    os << "\n\tsafe deliv hist {" << hs_safe << "} ";
    os << "\n\tcaus deliv hist {" << hs_local_causal << "} ";
    os << "\n\toutq avg " << double(send_queue_s)/double(n_send_queue_s);
    os << "\n\tsent {";
    copy(sent_msgs.begin(), sent_msgs.end(),
         ostream_iterator<long long int>(os, ","));
    os << "}\n\tsent per sec {";
    const double norm(double(Date::now().get_utc()
                             - last_stats_report.get_utc())/gu::datetime::Sec);
    vector<double> result(7, norm);
    transform(sent_msgs.begin(), sent_msgs.end(),
              result.begin(), result.begin(), divides<double>());
    copy(result.begin(), result.end(), ostream_iterator<double>(os, ","));
    os << "}\n\trecvd { ";
    copy(recvd_msgs.begin(), recvd_msgs.end(), ostream_iterator<long long int>(os, ","));
    os << "}\n\trecvd per sec {";
    fill(result.begin(), result.end(), norm);
    transform(recvd_msgs.begin(), recvd_msgs.end(),
              result.begin(), result.begin(), divides<double>());
    copy(result.begin(), result.end(), ostream_iterator<double>(os, ","));
    os << "}\n\tretransmitted " << retrans_msgs << " ";
    os << "\n\trecovered " << recovered_msgs;
    os << "\n\tdelivered {";
    copy(delivered_msgs.begin(), delivered_msgs.end(),
         ostream_iterator<long long int>(os, ", "));
    os << "}\n\teff(delivered/sent) " <<
        double(accumulate(delivered_msgs.begin() + 1,
                          delivered_msgs.begin() + O_SAFE + 1, 0))
        /double(accumulate(sent_msgs.begin(), sent_msgs.end(), 0));
    return os.str();
}

void gcomm::evs::Proto::reset_stats()
{
    hs_agreed.clear();
    hs_safe.clear();
    hs_local_causal.clear();
    send_queue_s = 0;
    n_send_queue_s = 0;
    fill(sent_msgs.begin(), sent_msgs.end(), 0LL);
    fill(recvd_msgs.begin(), recvd_msgs.end(), 0LL);
    retrans_msgs = 0LL;
    recovered_msgs = 0LL;
    fill(delivered_msgs.begin(), delivered_msgs.end(), 0LL);
    last_stats_report = Date::now();
}


bool gcomm::evs::Proto::is_msg_from_previous_view(const Message& msg)
{
    for (list<pair<ViewId, Date> >::const_iterator i = previous_views.begin();
         i != previous_views.end(); ++i)
    {
        if (msg.get_source_view_id() == i->first)
        {
            evs_log_debug(D_FOREIGN_MSGS) << " message " << msg
                                          << " from previous view " << i->first;
            return true;
        }
    }

    // If node is in current view, check message source view seq, if it is
    // smaller than current view seq then the message is also from some
    // previous (but unknown to us) view
    NodeList::const_iterator ni(current_view.get_members().find(msg.get_source()));
    if (ni != current_view.get_members().end())
    {
        if (msg.get_source_view_id().get_seq() <
            current_view.get_id().get_seq())
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
}


void gcomm::evs::Proto::handle_retrans_timer()
{
    evs_log_debug(D_TIMERS) << "retrans timer";
    if (get_state() == S_GATHER)
    {
        gu_trace(send_join(true));
        if (install_message != 0)
        {
            gu_trace(send_gap(UUID::nil(),
                              install_message->get_install_view_id(),
                              Range(), true));
        }
    }
    else if (get_state() == S_INSTALL)
    {
        gcomm_assert(install_message != 0);
        gu_trace(send_gap(UUID::nil(),
                          install_message->get_install_view_id(),
                          Range(), true));
        gu_trace(send_gap(UUID::nil(),
                          install_message->get_install_view_id(),
                          Range()));
    }
    else if (get_state() == S_OPERATIONAL)
    {
        const seqno_t prev_last_sent(last_sent);
        evs_log_debug(D_TIMERS) << "send user timer, last_sent=" << last_sent;
        Datagram dg;
        gu_trace((void)send_user(dg, 0xff, O_DROP, -1, -1));
        if (prev_last_sent == last_sent)
        {
            log_warn << "could not send keepalive";
        }
    }
    else if (get_state() == S_LEAVING)
    {
        evs_log_debug(D_TIMERS) << "send leave timer";
        profile_enter(send_leave_prof);
        send_leave(false);
        profile_leave(send_leave_prof);
    }
}


void gcomm::evs::Proto::handle_install_timer()
{
    gcomm_assert(get_state() == S_GATHER || get_state() == S_INSTALL);
    log_warn << self_string() << " install timer expired";

    if (install_timeout_count == max_install_timeouts - 1)
    {
        // before reaching max_install_timeouts, declare only inconsistent
        // nodes as inactive
        for (NodeMap::iterator i = known.begin(); i != known.end(); ++i)
        {
            const Node& node(NodeMap::get_value(i));
            if (NodeMap::get_key(i) != get_uuid() &&
                (node.get_join_message() == 0 ||
                 consensus.is_consistent(*node.get_join_message()) == false))
            {
                evs_log_info(I_STATE)
                    << " setting source " << NodeMap::get_key(i)
                    << " as inactive due to expired consensus timer";
                set_inactive(NodeMap::get_key(i));
            }
        }
    }
    else if (install_timeout_count == max_install_timeouts)
    {
        // max install timeouts reached, declare all other nodes
        // as inactive
        for (NodeMap::iterator i = known.begin(); i != known.end(); ++i)
        {
            if (NodeMap::get_key(i) != get_uuid())
            {
                evs_log_info(I_STATE)
                    << " setting source " << NodeMap::get_key(i)
                    << " as inactive due to expired consensus timer";
                set_inactive(NodeMap::get_key(i));
            }
        }
    }
    else if (install_timeout_count > max_install_timeouts)
    {
        log_info << "going to give up, state dump for diagnosis:";
        std::cerr << *this << std::endl;
        gu_throw_fatal << self_string()
                       << " failed to form singleton view after exceeding "
                       << "max_install_timeouts " << max_install_timeouts
                       << ", giving up";
    }


    if (install_message != 0)
    {
        for (NodeMap::iterator i = known.begin(); i != known.end(); ++i)
        {
            if (NodeMap::get_value(i).get_committed() == false)
            {
                log_info << self_string() << " node " << NodeMap::get_key(i)
                         << " failed to commit for install message, "
                         << "declaring inactive";
                if (NodeMap::get_key(i) != get_uuid())
                {
                    set_inactive(NodeMap::get_key(i));
                }
            }
        }
    }
    else
    {
        log_info << "no install message received";
    }

    shift_to(S_GATHER, true);
    const bool is_cons(consensus.is_consensus());
    const bool is_repr(is_representative(get_uuid()));
    evs_log_info(I_STATE) << "consensus: " << is_cons;
    evs_log_info(I_STATE) << "repr     : " << is_repr;
    evs_log_info(I_STATE) << "state dump for diagnosis:";
    std::cerr << *this << std::endl;
    if (is_cons == true && is_repr == true)
    {
        send_install();
    }
    install_timeout_count++;
}

void gcomm::evs::Proto::handle_stats_timer()
{
    evs_log_info(I_STATISTICS) << get_stats();
    reset_stats();
#ifdef GCOMM_PROFILE
    evs_log_info(I_PROFILING) << "\nprofiles:\n";
    evs_log_info(I_PROFILING) << send_user_prof    << "\n";
    evs_log_info(I_PROFILING) << send_gap_prof     << "\n";
    evs_log_info(I_PROFILING) << send_join_prof    << "\n";
    evs_log_info(I_PROFILING) << send_install_prof << "\n";
    evs_log_info(I_PROFILING) << send_leave_prof   << "\n";
    evs_log_info(I_PROFILING) << consistent_prof   << "\n";
    evs_log_info(I_PROFILING) << consensus_prof    << "\n";
    evs_log_info(I_PROFILING) << shift_to_prof     << "\n";
    evs_log_info(I_PROFILING) << input_map_prof    << "\n";
    evs_log_info(I_PROFILING) << delivery_prof     << "\n";
#endif // GCOMM_PROFILE
}



class TimerSelectOp
{
public:
    TimerSelectOp(const Proto::Timer t_) : t(t_) { }
    bool operator()(const Proto::TimerList::value_type& vt) const
    {
        return (Proto::TimerList::get_value(vt) == t);
    }
private:
    Proto::Timer const t;
};


Date gcomm::evs::Proto::get_next_expiration(const Timer t) const
{
    gcomm_assert(get_state() != S_CLOSED);
    Date now(Date::now());
    switch (t)
    {
    case T_INACTIVITY:
        return (now + inactive_check_period);
    case T_RETRANS:
        switch (get_state())
        {
        case S_OPERATIONAL:
        case S_LEAVING:
            return (now + retrans_period);
        case S_GATHER:
        case S_INSTALL:
            return (now + join_retrans_period);
        default:
            gu_throw_fatal;
        }
    case T_INSTALL:
        switch (get_state())
        {
        case S_GATHER:
        case S_INSTALL:
            return (now + install_timeout);
        default:
            return Date::max();
        }
    case T_STATS:
        return (now + stats_report_period);
    }
    gu_throw_fatal;
    throw;
}


void gcomm::evs::Proto::reset_timers()
{
    timers.clear();
    gu_trace((void)timers.insert(
                 make_pair(get_next_expiration(T_INACTIVITY), T_INACTIVITY)));
    gu_trace((void)timers.insert(
                 make_pair(get_next_expiration(T_RETRANS), T_RETRANS)));
    gu_trace((void)timers.insert(
                 make_pair(get_next_expiration(T_INSTALL), T_INSTALL)));
    gu_trace((void)timers.insert(
                 make_pair(get_next_expiration(T_STATS), T_STATS)));
}


Date gcomm::evs::Proto::handle_timers()
{
    Date now(Date::now());

    while (timers.empty() == false &&
           TimerList::get_key(timers.begin()) <= now)
    {
        Timer t(TimerList::get_value(timers.begin()));
        timers.erase(timers.begin());
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
        if (get_state() == S_CLOSED)
        {
            return Date::max();
        }
        // Make sure that timer was not inserted twice
        TimerList::iterator ii(find_if(timers.begin(), timers.end(),
                                       TimerSelectOp(t)));
        if (ii != timers.end())
        {
            timers.erase(ii);
        }
        gu_trace((void)timers.insert(make_pair(get_next_expiration(t), t)));
    }

    if (timers.empty() == true)
    {
        evs_log_debug(D_TIMERS) << "no timers set";
        return Date::max();
    }
    return TimerList::get_key(timers.begin());
}



void gcomm::evs::Proto::check_inactive()
{
    const Date now(Date::now());
    if (last_inactive_check + inactive_check_period*3 < now)
    {
        log_warn << "last inactive check more than " << inactive_check_period*3
                 << " ago, skipping check";
        last_inactive_check = now;
        return;
    }

    NodeMap::get_value(self_i).set_tstamp(gu::datetime::Date::now());
    std::for_each(known.begin(), known.end(), InspectNode());

    bool has_inactive(false);
    size_t n_suspected(0);
    for (NodeMap::iterator i = known.begin(); i != known.end(); ++i)
    {
        const UUID& uuid(NodeMap::get_key(i));
        Node& node(NodeMap::get_value(i));
        if (uuid                   != get_uuid()    &&
            (node.is_inactive()     == true      ||
             node.is_suspected()    == true           ))
        {
            if (node.get_operational() == true && node.is_inactive() == true)
            {
                log_info << self_string() << " detected inactive node: " << uuid;
            }
            else if (node.is_suspected() == true && node.is_inactive() == false)
            {
                log_info << self_string() << " suspecting node: " << uuid;
            }
            if (node.is_inactive() == true)
            {
                set_inactive(uuid);
            }
            if (node.is_suspected() == true)
            {
                ++n_suspected;
            }
            has_inactive = true;
        }
    }

    // All other nodes are under suspicion, set all others as inactive.
    // This will speed up recovery when this node has been isolated from
    // other group. Note that this should be done only if known size is
    // greater than 2 in order to avoid immediate split brain.
    if (known.size() > 2 && n_suspected + 1 == known.size())
    {
        for (NodeMap::iterator i = known.begin(); i != known.end(); ++i)
        {
            if (NodeMap::get_key(i) != get_uuid())
            {
                evs_log_info(I_STATE)
                    << " setting source " << NodeMap::get_key(i)
                    << " inactive (other nodes under suspicion)";
                set_inactive(NodeMap::get_key(i));
            }
        }
    }

    if (has_inactive == true && get_state() == S_OPERATIONAL)
    {
        profile_enter(shift_to_prof);
        gu_trace(shift_to(S_GATHER, true));
        profile_leave(shift_to_prof);
    }
    else if (has_inactive    == true &&
             get_state()     == S_LEAVING &&
             n_operational() == 1)
    {
        profile_enter(shift_to_prof);
        gu_trace(shift_to(S_CLOSED));
        profile_leave(shift_to_prof);
    }

    last_inactive_check = now;
}


void gcomm::evs::Proto::set_inactive(const UUID& uuid)
{
    NodeMap::iterator i;
    gcomm_assert(uuid != get_uuid());
    gu_trace(i = known.find_checked(uuid));
    evs_log_debug(D_STATE) << "setting " << uuid << " inactive";
    Node& node(NodeMap::get_value(i));
    node.set_tstamp(Date::zero());
    node.set_join_message(0);
    // node.set_leave_message(0);
    node.set_operational(false);
}


void gcomm::evs::Proto::cleanup_unoperational()
{
    NodeMap::iterator i, i_next;
    for (i = known.begin(); i != known.end(); i = i_next)
    {
        i_next = i, ++i_next;
        if (NodeMap::get_value(i).get_installed() == false)
        {
            evs_log_debug(D_STATE) << "erasing " << NodeMap::get_key(i);
            known.erase(i);
        }
    }
}


void gcomm::evs::Proto::cleanup_views()
{
    Date now(Date::now());
    list<pair<ViewId, Date> >::iterator i = previous_views.begin();
    while (i != previous_views.end())
    {
        if (i->second + view_forget_timeout <= now)
        {
            evs_log_debug(D_STATE) << " erasing view: " << i->first;
            previous_views.erase(i);
        }
        else
        {
            break;
        }
        i = previous_views.begin();
    }
}

size_t gcomm::evs::Proto::n_operational() const
{
    NodeMap::const_iterator i;
    size_t ret = 0;
    for (i = known.begin(); i != known.end(); ++i) {
        if (i->second.get_operational() == true)
            ret++;
    }
    return ret;
}

void gcomm::evs::Proto::deliver_reg_view()
{
    if (install_message == 0)
    {
        gu_throw_fatal
            << "Protocol error: no install message in deliver reg view";
    }

    if (previous_views.size() == 0) gu_throw_fatal << "Zero-size view";

    const View& prev_view (previous_view);
    View view (install_message->get_install_view_id());

    for (NodeMap::iterator i = known.begin(); i != known.end(); ++i)
    {
        if (NodeMap::get_value(i).get_installed() == true)
        {
            view.add_member(NodeMap::get_key(i), "");
            if (prev_view.get_members().find(NodeMap::get_key(i)) ==
                prev_view.get_members().end())
            {
                view.add_joined(NodeMap::get_key(i), "");
            }
        }
        else if (NodeMap::get_value(i).get_installed() == false)
        {
            const MessageNodeList& instances = install_message->get_node_list();
            MessageNodeList::const_iterator inst_i;
            if ((inst_i = instances.find(NodeMap::get_key(i))) != instances.end())
            {
                if (MessageNodeList::get_value(inst_i).get_leaving() == true)
                {
                    view.add_left(NodeMap::get_key(i), "");
                }
                else
                {
                    view.add_partitioned(NodeMap::get_key(i), "");
                }
            }
            gcomm_assert(NodeMap::get_key(i) != get_uuid());
            gcomm_assert(NodeMap::get_value(i).get_operational() == false);
        }
    }

    evs_log_info(I_VIEWS) << "delivering view " << view;

    ProtoUpMeta up_meta(UUID::nil(), ViewId(), &view);
    send_up(Datagram(), up_meta);
}

void gcomm::evs::Proto::deliver_trans_view(bool local)
{
    if (local == false && install_message == 0)
    {
        gu_throw_fatal
            << "Protocol error: no install message in deliver trans view";
    }

    View view(ViewId(V_TRANS,
                     current_view.get_id().get_uuid(),
                     current_view.get_id().get_seq()));

    for (NodeMap::const_iterator i = known.begin(); i != known.end(); ++i)
    {
        const UUID& uuid(NodeMap::get_key(i));
        const Node& inst(NodeMap::get_value(i));

        if (inst.get_installed() == true &&
            current_view.get_members().find(uuid) !=
            current_view.get_members().end() &&
            (local == true ||
             MessageNodeList::get_value(install_message->get_node_list().find_checked(uuid)).get_view_id() == current_view.get_id()))
        {
            view.add_member(NodeMap::get_key(i), "");
        }
        else if (inst.get_installed() == false)
        {
            if (local == false)
            {
                const MessageNodeList& instances(install_message->get_node_list());
                MessageNodeList::const_iterator inst_i;
                if ((inst_i = instances.find(NodeMap::get_key(i))) != instances.end())
                {
                    if (MessageNodeList::get_value(inst_i).get_leaving())
                    {
                        view.add_left(NodeMap::get_key(i), "");
                    }
                    else
                    {
                        view.add_partitioned(NodeMap::get_key(i), "");
                    }
                }
                else if (current_view.is_member(NodeMap::get_key(i)) == true)
                {
                    view.add_partitioned(NodeMap::get_key(i), "");
                }
            }
            else
            {
                // Just assume others have partitioned, it does not matter
                // for leaving node anyway and it is not guaranteed if
                // the others get the leave message, so it is not safe
                // to assume then as left.
                log_info << self_string()
                         << " uuid " << NodeMap::get_key(i)
                         << " missing from install message, assuming partitioned";
                view.add_partitioned(NodeMap::get_key(i), "");
            }
        }
        else
        {
            // merging nodes, these won't be visible in trans view
        }
    }

    gcomm_assert(view.is_member(get_uuid()) == true);

    evs_log_info(I_VIEWS) << " delivering view " << view;

    ProtoUpMeta up_meta(UUID::nil(), ViewId(), &view);
    gu_trace(send_up(Datagram(), up_meta));
}


void gcomm::evs::Proto::deliver_empty_view()
{
    View view(V_REG);

    evs_log_info(I_VIEWS) << "delivering view " << view;

    ProtoUpMeta up_meta(UUID::nil(), ViewId(), &view);
    send_up(Datagram(), up_meta);
}


void gcomm::evs::Proto::setall_committed(bool val)
{
    for (NodeMap::iterator i = known.begin(); i != known.end(); ++i)
    {
        NodeMap::get_value(i).set_committed(val);
    }
}

bool gcomm::evs::Proto::is_all_committed() const
{
    for (NodeMap::const_iterator i = known.begin(); i != known.end(); ++i)
    {
        const Node& inst(NodeMap::get_value(i));
        if (inst.get_operational() == true && inst.get_committed() == false)
        {
            return false;
        }
    }
    return true;
}

void gcomm::evs::Proto::setall_installed(bool val)
{
    for (NodeMap::iterator i = known.begin(); i != known.end(); ++i)
    {
        NodeMap::get_value(i).set_installed(val);
    }
}


void gcomm::evs::Proto::cleanup_joins()
{
    for (NodeMap::iterator i = known.begin(); i != known.end(); ++i)
    {
        NodeMap::get_value(i).set_join_message(0);
    }
}


bool gcomm::evs::Proto::is_all_installed() const
{
    for (NodeMap::const_iterator i = known.begin(); i != known.end(); ++i)
    {
        const Node& inst(NodeMap::get_value(i));
        if (inst.get_operational() == true && inst.get_installed() == false)
        {
            return false;
        }
    }
    return true;
}




bool gcomm::evs::Proto::is_representative(const UUID& uuid) const
{
    for (NodeMap::const_iterator i = known.begin(); i != known.end(); ++i)
    {
        if (NodeMap::get_value(i).get_operational() == true &&
            NodeMap::get_value(i).is_inactive()     == false)
        {
            gcomm_assert(NodeMap::get_value(i).get_leave_message() == 0);
            return (uuid == NodeMap::get_key(i));
        }
    }

    return false;
}



/////////////////////////////////////////////////////////////////////////////
// Message sending
/////////////////////////////////////////////////////////////////////////////




bool gcomm::evs::Proto::is_flow_control(const seqno_t seq, const seqno_t win) const
{
    gcomm_assert(seq != -1 && win != -1);

    const seqno_t base(input_map->get_aru_seq());
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
    assert(get_state() == S_LEAVING ||
           get_state() == S_GATHER ||
           get_state() == S_OPERATIONAL);
    assert(dg.get_offset() == 0);
    assert(n_aggregated == 1 || output.size() >= n_aggregated);

    gcomm_assert(up_to_seqno == -1 || up_to_seqno >= last_sent);
    gcomm_assert(up_to_seqno == -1 || win == -1);

    int ret;
    const seqno_t seq(last_sent + 1);

    if (win                       != -1   &&
        is_flow_control(seq, win) == true)
    {
        return EAGAIN;
    }

    seqno_t seq_range(up_to_seqno == -1 ? 0 : up_to_seqno - seq);
    seqno_t last_msg_seq(seq + seq_range);
    uint8_t flags;

    if (output.size() <= n_aggregated ||
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

    if ((flags & Message::F_MSG_MORE) == 0 && up_to_seqno == -1)
    {
        seq_range = input_map->get_max_hs() - seq;
        seq_range = max(static_cast<seqno_t>(0), seq_range);
        seq_range = min(static_cast<seqno_t>(0xff), seq_range);
        if (seq_range != 0)
        {
            log_debug << "adjusted seq range to: " << seq_range;
            last_msg_seq = seq + seq_range;
        }
    }

    if (seq_range > 0xff)
    {
        log_warn << "readjusting seq range " << seq_range << " to 255";
        seq_range = 0xff;
    }
    gcomm_assert(seq_range >= 0 && seq_range <= 0xff);

    UserMessage msg(version,
                    get_uuid(),
                    current_view.get_id(),
                    seq,
                    input_map->get_aru_seq(),
                    seq_range,
                    order,
                    ++fifo_seq,
                    user_type,
                    flags);

    // Insert first to input map to determine correct aru seq
    Range range;
    gu_trace(range = input_map->insert(NodeMap::get_value(self_i).get_index(),
                                       msg, dg));

    gcomm_assert(range.get_hs() == last_msg_seq)
        << msg << " " << *input_map << " " << *this;

    last_sent = last_msg_seq;
    assert(range.get_hs() == last_sent);

    update_im_safe_seq(NodeMap::get_value(self_i).get_index(),
                       input_map->get_aru_seq());

    msg.set_aru_seq(input_map->get_aru_seq());
    evs_log_debug(D_USER_MSGS) << " sending " << msg;
    gu_trace(push_header(msg, dg));
    if ((ret = send_down(dg, ProtoDownMeta())) != 0)
    {
        log_debug << "send failed: "  << strerror(ret);
    }
    gu_trace(pop_header(msg, dg));
    sent_msgs[Message::T_USER]++;

    if (delivering == false && input_map->has_deliverables() == true)
    {
        gu_trace(deliver());
    }
    gu_trace(deliver_local());
    return 0;
}

size_t gcomm::evs::Proto::aggregate_len() const
{
    bool is_aggregate(false);
    size_t ret(0);
    AggregateMessage am;
    deque<pair<Datagram, ProtoDownMeta> >::const_iterator i(output.begin());
    const Order ord(i->second.get_order());
    ret += i->first.get_len() + am.serial_size();
    for (++i; i != output.end() && i->second.get_order() == ord; ++i)
    {
        if (ret + i->first.get_len() + am.serial_size() <= get_mtu())
        {
            ret += i->first.get_len() + am.serial_size();
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
    gcomm_assert(output.empty() == false);
    gcomm_assert(get_state() == S_OPERATIONAL);
    gcomm_assert(win <= send_window);
    int ret;
    size_t alen;
    if (use_aggregate == true && (alen = aggregate_len()) > 0)
    {
        // Messages can be aggregated into single message
        send_buf_.resize(alen);
        size_t offset(0);
        size_t n(0);

        deque<pair<Datagram, ProtoDownMeta> >::iterator i(output.begin());
        Order ord(i->second.get_order());
        while ((alen > 0 && i != output.end()))
        {
            const Datagram& dg(i->first);
            const ProtoDownMeta dm(i->second);
            AggregateMessage am(0, dg.get_len(), dm.get_user_type());
            gcomm_assert(alen >= dg.get_len() + am.serial_size());

            gu_trace(offset = am.serialize(&send_buf_[0],
                                           send_buf_.size(), offset));
            copy(dg.get_header() + dg.get_header_offset(),
                 dg.get_header() + dg.get_header_size(),
                 &send_buf_[0] + offset);
            offset += (dg.get_header_len());
            copy(dg.get_payload().begin(), dg.get_payload().end(),
                 &send_buf_[0] + offset);
            offset += dg.get_payload().size();
            alen -= dg.get_len() + am.serial_size();
            ++n;
            ++i;
        }
        Datagram dg(SharedBuffer(new Buffer(send_buf_.begin(),
                                            send_buf_.end())));
        if ((ret = send_user(dg, 0xff, ord, win, -1, n)) == 0)
        {
            while (n-- > 0)
            {
                output.pop_front();
            }
        }
    }
    else
    {
        pair<Datagram, ProtoDownMeta> wb(output.front());
        if ((ret = send_user(wb.first,
                             wb.second.get_user_type(),
                             wb.second.get_order(),
                             win,
                             -1)) == 0)
        {
            output.pop_front();
        }
    }
    return ret;
}


void gcomm::evs::Proto::complete_user(const seqno_t high_seq)
{
    gcomm_assert(get_state() == S_OPERATIONAL || get_state() == S_GATHER);

    evs_log_debug(D_USER_MSGS) << "completing seqno to " << high_seq;;

    Datagram wb;
    int err;
    profile_enter(send_user_prof);
    err = send_user(wb, 0xff, O_DROP, -1, high_seq);
    profile_leave(send_user_prof);
    if (err != 0)
    {
        log_debug << "failed to send completing msg " << strerror(err)
                  << " seq=" << high_seq << " send_window=" << send_window
                  << " last_sent=" << last_sent;
    }

}


int gcomm::evs::Proto::send_delegate(Datagram& wb)
{
    DelegateMessage dm(version, get_uuid(), current_view.get_id(), ++fifo_seq);
    push_header(dm, wb);
    int ret = send_down(wb, ProtoDownMeta());
    pop_header(dm, wb);
    sent_msgs[Message::T_DELEGATE]++;
    return ret;
}


void gcomm::evs::Proto::send_gap(const UUID&   range_uuid,
                                 const ViewId& source_view_id,
                                 const Range   range,
                                 const bool    commit)
{
    evs_log_debug(D_GAP_MSGS) << "sending gap  to "
                              << range_uuid
                              << " requesting range " << range;
    gcomm_assert((commit == false && source_view_id == current_view.get_id())
                 || install_message != 0);
    // TODO: Investigate if gap sending can be somehow limited,
    // message loss happen most probably during congestion and
    // flooding network with gap messages won't probably make
    // conditions better

    GapMessage gm(version,
                  get_uuid(),
                  source_view_id,
                  (source_view_id == current_view.get_id() ? last_sent :
                   (commit == true ? install_message->get_fifo_seq() : -1)),
                  (source_view_id == current_view.get_id() ?
                   input_map->get_aru_seq() : -1),
                  ++fifo_seq,
                  range_uuid,
                  range,
                  (commit == true ? Message::F_COMMIT : static_cast<uint8_t>(0)));

    Buffer buf;
    serialize(gm, buf);
    Datagram dg(buf);
    int err = send_down(dg, ProtoDownMeta());
    if (err != 0)
    {
        log_debug << "send failed: " << strerror(err);
    }
    sent_msgs[Message::T_GAP]++;
    gu_trace(handle_gap(gm, self_i));
}


void gcomm::evs::Proto::populate_node_list(MessageNodeList* node_list) const
{
    for (NodeMap::const_iterator i = known.begin(); i != known.end(); ++i)
    {
        const UUID& uuid(NodeMap::get_key(i));
        const Node& node(NodeMap::get_value(i));
        MessageNode mnode(node.get_operational());
        if (uuid != get_uuid())
        {
            const JoinMessage* jm(node.get_join_message());
            const LeaveMessage* lm(node.get_leave_message());

            //
            if (jm != 0)
            {
                const ViewId& nsv(jm->get_source_view_id());
                const MessageNode& mn(MessageNodeList::get_value(jm->get_node_list().find_checked(uuid)));
                mnode = MessageNode(node.get_operational(),
                                    node.is_suspected(),
                                    -1,
                                    jm->get_source_view_id(),
                                    (nsv == current_view.get_id() ?
                                     input_map->get_safe_seq(node.get_index()) :
                                     mn.get_safe_seq()),
                                    (nsv == current_view.get_id() ?
                                     input_map->get_range(node.get_index()) :
                                     mn.get_im_range()));
            }
            else if (lm != 0)
            {
                const ViewId& nsv(lm->get_source_view_id());
                mnode = MessageNode(node.get_operational(),
                                    node.is_suspected(),
                                    lm->get_seq(),
                                    nsv,
                                    (nsv == current_view.get_id() ?
                                     input_map->get_safe_seq(node.get_index()) :
                                     -1),
                                    (nsv == current_view.get_id() ?
                                     input_map->get_range(node.get_index()) :
                                     Range()));
            }
            else if (current_view.is_member(uuid) == true)
            {
                mnode = MessageNode(node.get_operational(),
                                    node.is_suspected(),
                                    -1,
                                    current_view.get_id(),
                                    input_map->get_safe_seq(node.get_index()),
                                    input_map->get_range(node.get_index()));
            }
        }
        else
        {
            mnode = MessageNode(true,
                                false,
                                -1,
                                current_view.get_id(),
                                input_map->get_safe_seq(node.get_index()),
                                input_map->get_range(node.get_index()));
        }
        gu_trace((void)node_list->insert_unique(make_pair(uuid, mnode)));
    }

    evs_log_debug(D_CONSENSUS) << "populate node list:\n" << *node_list;
}

const JoinMessage& gcomm::evs::Proto::create_join()
{

    MessageNodeList node_list;

    gu_trace(populate_node_list(&node_list));
    JoinMessage jm(version,
                   get_uuid(),
                   current_view.get_id(),
                   input_map->get_safe_seq(),
                   input_map->get_aru_seq(),
                   ++fifo_seq,
                   node_list);
    NodeMap::get_value(self_i).set_join_message(&jm);

    evs_log_debug(D_JOIN_MSGS) << " created join message " << jm;

    // Note: This assertion does not hold anymore, join message is
    //       not necessarily equal to local state.
    // gcomm_assert(consensus.is_consistent_same_view(jm) == true)
    //    << "created inconsistent JOIN message " << jm
    //    << " local state " << *this;

    return *NodeMap::get_value(self_i).get_join_message();
}


void gcomm::evs::Proto::set_join(const JoinMessage& jm, const UUID& source)
{
    NodeMap::iterator i;
    gu_trace(i = known.find_checked(source));
    NodeMap::get_value(i).set_join_message(&jm);;
}


void gcomm::evs::Proto::set_leave(const LeaveMessage& lm, const UUID& source)
{
    NodeMap::iterator i;
    gu_trace(i = known.find_checked(source));
    Node& inst(NodeMap::get_value(i));

    if (inst.get_leave_message())
    {
        evs_log_debug(D_LEAVE_MSGS) << "Duplicate leave:\told: "
                                    << *inst.get_leave_message()
                                    << "\tnew: " << lm;
    }
    else
    {
        inst.set_leave_message(&lm);
    }
}


void gcomm::evs::Proto::send_join(bool handle)
{
    assert(output.empty() == true);

    JoinMessage jm(create_join());

    Buffer buf;
    serialize(jm, buf);
    Datagram dg(buf);
    int err = send_down(dg, ProtoDownMeta());

    if (err != 0)
    {
        log_debug << "send failed: " << strerror(err);
    }
    sent_msgs[Message::T_JOIN]++;
    if (handle == true)
    {
        handle_join(jm, self_i);
    }
}


void gcomm::evs::Proto::send_leave(bool handle)
{
    gcomm_assert(get_state() == S_LEAVING);

    // If no messages have been sent, generate one dummy to
    // trigger message acknowledgement mechanism
    if (last_sent == -1 && output.empty() == true)
    {
        Datagram wb;
        gu_trace(send_user(wb, 0xff, O_DROP, -1, -1));
    }

    /* Move all pending messages from output to input map */
    while (output.empty() == false)
    {
        pair<Datagram, ProtoDownMeta> wb = output.front();
        if (send_user(wb.first,
                      wb.second.get_user_type(),
                      wb.second.get_order(),
                      -1, -1) != 0)
        {
            gu_throw_fatal << "send_user() failed";
        }
        output.pop_front();
    }


    LeaveMessage lm(version,
                    get_uuid(),
                    current_view.get_id(),
                    last_sent,
                    input_map->get_aru_seq(),
                    ++fifo_seq);

    evs_log_debug(D_LEAVE_MSGS) << "sending leave msg " << lm;

    Buffer buf;
    serialize(lm, buf);
    Datagram dg(buf);
    int err = send_down(dg, ProtoDownMeta());
    if (err != 0)
    {
        log_debug << "send failed " << strerror(err);
    }

    sent_msgs[Message::T_LEAVE]++;

    if (handle == true)
    {
        handle_leave(lm, self_i);
    }
}


struct ViewIdCmp
{
    bool operator()(const NodeMap::value_type& a,
                    const NodeMap::value_type& b) const
    {
        gcomm_assert(NodeMap::get_value(a).get_join_message() != 0 &&
                     NodeMap::get_value(b).get_join_message() != 0);
        return (NodeMap::get_value(a).get_join_message()->get_source_view_id().get_seq() <
                NodeMap::get_value(b).get_join_message()->get_source_view_id().get_seq());

    }
};


void gcomm::evs::Proto::send_install()
{
    gcomm_assert(consensus.is_consensus() == true &&
                 is_representative(get_uuid()) == true) << *this;

    NodeMap oper_list;
    for_each(known.begin(), known.end(), OperationalSelect(oper_list));
    NodeMap::const_iterator max_node =
        max_element(oper_list.begin(), oper_list.end(), ViewIdCmp());

    const uint32_t max_view_id_seq =
        NodeMap::get_value(max_node).get_join_message()->get_source_view_id().get_seq();

    MessageNodeList node_list;
    populate_node_list(&node_list);

    InstallMessage imsg(version,
                        get_uuid(),
                        current_view.get_id(),
                        ViewId(V_REG, get_uuid(), max_view_id_seq + attempt_seq),
                        input_map->get_safe_seq(),
                        input_map->get_aru_seq(),
                        ++fifo_seq,
                        node_list);
    ++attempt_seq;
    evs_log_debug(D_INSTALL_MSGS) << "sending install " << imsg;
    gcomm_assert(consensus.is_consistent(imsg));

    Buffer buf;
    serialize(imsg, buf);
    Datagram dg(buf);
    int err = send_down(dg, ProtoDownMeta());
    if (err != 0)
    {
        log_debug << "send failed: " << strerror(err);
    }

    sent_msgs[Message::T_INSTALL]++;
    handle_install(imsg, self_i);
}


void gcomm::evs::Proto::resend(const UUID& gap_source, const Range range)
{
    gcomm_assert(gap_source != get_uuid());
    gcomm_assert(range.get_lu() <= range.get_hs()) <<
        "lu (" << range.get_lu() << ") > hs(" << range.get_hs() << ")";

    if (range.get_lu() <= input_map->get_safe_seq())
    {
        log_warn << self_string() << "lu (" << range.get_lu() <<
            ") <= safe_seq(" << input_map->get_safe_seq()
                 << "), can't recover message";
        return;
    }

    evs_log_debug(D_RETRANS) << " retrans requested by "
                             << gap_source
                             << " "
                             << range.get_lu() << " -> "
                             << range.get_hs();

    seqno_t seq(range.get_lu());
    while (seq <= range.get_hs())
    {
        InputMap::iterator msg_i = input_map->find(NodeMap::get_value(self_i).get_index(), seq);
        if (msg_i == input_map->end())
        {
            gu_trace(msg_i = input_map->recover(NodeMap::get_value(self_i).get_index(), seq));
        }

        const UserMessage& msg(InputMapMsgIndex::get_value(msg_i).get_msg());
        gcomm_assert(msg.get_source() == get_uuid());
        Datagram rb(InputMapMsgIndex::get_value(msg_i).get_rb());
        assert(rb.get_offset() == 0);

        UserMessage um(msg.get_version(),
                       msg.get_source(),
                       msg.get_source_view_id(),
                       msg.get_seq(),
                       input_map->get_aru_seq(),
                       msg.get_seq_range(),
                       msg.get_order(),
                       msg.get_fifo_seq(),
                       msg.get_user_type(),
                       static_cast<uint8_t>(
                           Message::F_RETRANS |
                           (msg.get_flags() & Message::F_AGGREGATE)));

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
        seq = seq + msg.get_seq_range() + 1;
        retrans_msgs++;
    }
}


void gcomm::evs::Proto::recover(const UUID& gap_source,
                                const UUID& range_uuid,
                                const Range range)
{
    gcomm_assert(gap_source != get_uuid())
        << "gap_source (" << gap_source << ") == get_uuid() (" << get_uuid()
        << " state " << *this;
    gcomm_assert(range.get_lu() <= range.get_hs())
        << "lu (" << range.get_lu() << ") > hs (" << range.get_hs() << ")";

    if (range.get_lu() <= input_map->get_safe_seq())
    {
        log_warn << self_string() << "lu (" << range.get_lu() <<
            ") <= safe_seq(" << input_map->get_safe_seq()
                 << "), can't recover message";
        return;
    }

    const Node& range_node(NodeMap::get_value(known.find_checked(range_uuid)));
    const Range im_range(input_map->get_range(range_node.get_index()));

    evs_log_debug(D_RETRANS) << " recovering message from "
                             << range_uuid
                             << " requested by "
                             << gap_source
                             << " requested range " << range
                             << " available " << im_range;


    seqno_t seq(range.get_lu());
    while (seq <= range.get_hs() && seq <= im_range.get_hs())
    {
        InputMap::iterator msg_i = input_map->find(range_node.get_index(), seq);
        if (msg_i == input_map->end())
        {
            try
            {
                gu_trace(msg_i = input_map->recover(range_node.get_index(), seq));
            }
            catch (...)
            {
                seq = seq + 1;
                continue;
            }
        }

        const UserMessage& msg(InputMapMsgIndex::get_value(msg_i).get_msg());
        assert(msg.get_source() == range_uuid);

        Datagram rb(InputMapMsgIndex::get_value(msg_i).get_rb());
        assert(rb.get_offset() == 0);
        UserMessage um(msg.get_version(),
                       msg.get_source(),
                       msg.get_source_view_id(),
                       msg.get_seq(),
                       msg.get_aru_seq(),
                       msg.get_seq_range(),
                       msg.get_order(),
                       msg.get_fifo_seq(),
                       msg.get_user_type(),
                       static_cast<uint8_t>(
                           Message::F_SOURCE |
                           Message::F_RETRANS |
                           (msg.get_flags() & Message::F_AGGREGATE)));

        push_header(um, rb);

        int err = send_delegate(rb);
        if (err != 0)
        {
            log_debug << "send failed: " << strerror(err);
            break;
        }
        else
        {
            evs_log_info(D_RETRANS) << "recover " << um;
        }
        seq = seq + msg.get_seq_range() + 1;
        recovered_msgs++;
    }
}


void gcomm::evs::Proto::handle_foreign(const Message& msg)
{
    // no need to handle foreign LEAVE message
    if (msg.get_type() == Message::T_LEAVE)
    {
        return;
    }

    // don't handle foreing messages in install phase
    if (get_state()              == S_INSTALL)
    {
        //evs_log_debug(D_FOREIGN_MSGS)
        log_warn << self_string()
                 << " dropping foreign message from "
                 << msg.get_source() << " in install state";
        return;
    }

    if (is_msg_from_previous_view(msg) == true)
    {
        return;
    }

    const UUID& source = msg.get_source();

    evs_log_debug(D_FOREIGN_MSGS) << " detected new message source "
                                  << source;

    NodeMap::iterator i;
    gu_trace(i = known.insert_unique(make_pair(source, Node(inactive_timeout, suspect_timeout))));
    assert(NodeMap::get_value(i).get_operational() == true);

    if (get_state() == S_JOINING || get_state() == S_GATHER ||
        get_state() == S_OPERATIONAL)
    {
        evs_log_info(I_STATE)
            << " shift to GATHER due to foreign message from "
            << msg.get_source();
        gu_trace(shift_to(S_GATHER, false));
    }

    // Set join message after shift to recovery, shift may clean up
    // join messages
    if (msg.get_type() == Message::T_JOIN)
    {
        set_join(static_cast<const JoinMessage&>(msg), msg.get_source());
    }
    send_join(true);
}

void gcomm::evs::Proto::handle_msg(const Message& msg,
                                   const Datagram& rb)
{
    assert(msg.get_type() <= Message::T_LEAVE);
    if (get_state() == S_CLOSED)
    {
        return;
    }

    if (msg.get_source() == get_uuid())
    {
        return;
    }

    if (msg.get_version() != version)
    {
        log_info << "incompatible protocol version " << msg.get_version();
        return;
    }



    gcomm_assert(msg.get_source() != UUID::nil());


    // Figure out if the message is from known source
    NodeMap::iterator ii = known.find(msg.get_source());

    if (ii == known.end())
    {
        gu_trace(handle_foreign(msg));
        return;
    }

    Node& node(NodeMap::get_value(ii));

    if (node.get_operational()                 == false &&
        node.get_leave_message()               == 0     &&
        (msg.get_flags() & Message::F_RETRANS) == 0)
    {
        // We have set this node unoperational and there was
        // probably good reason to do so. Don't accept messages
        // from it before new view has been formed.
        // Exceptions:
        // - Node that is leaving
        // - Retransmitted messages
        return;
    }

    // Filter out non-fifo messages
    if (msg.get_fifo_seq() != -1 && (msg.get_flags() & Message::F_RETRANS) == 0)
    {

        if (node.get_fifo_seq() >= msg.get_fifo_seq())
        {
            evs_log_debug(D_FOREIGN_MSGS)
                << "droppoing non-fifo message " << msg
                << " fifo seq " << node.get_fifo_seq();
            return;
        }
        else
        {
            node.set_fifo_seq(msg.get_fifo_seq());
        }
    }

    // Accept non-membership messages only from current view
    // or from view to be installed
    if (msg.is_membership()                     == false                    &&
        msg.get_source_view_id()                != current_view.get_id()    &&
        (install_message                        == 0                     ||
         install_message->get_install_view_id() != msg.get_source_view_id()))
    {
        // If source node seems to be operational but it has proceeded
        // into new view, mark it as unoperational in order to create
        // intermediate views before re-merge.
        if (node.get_installed()           == true      &&
            node.get_operational()         == true      &&
            is_msg_from_previous_view(msg) == false     &&
            get_state()                    != S_LEAVING)
        {
            evs_log_info(I_STATE)
                << " detected new view from operational source "
                << msg.get_source() << ": "
                << msg.get_source_view_id();
            // Note: Commented out, this causes problems with
            // attempt_seq. Newly generated install message
            // followed by commit gap may cause undesired
            // node inactivation and shift to gather.
            //
            // set_inactive(msg.get_source());
            // gu_trace(shift_to(S_GATHER, true));
        }
        return;
    }

    recvd_msgs[msg.get_type()]++;

    switch (msg.get_type())
    {
    case Message::T_USER:
        gu_trace(handle_user(static_cast<const UserMessage&>(msg), ii, rb));
        break;
    case Message::T_DELEGATE:
        gu_trace(handle_delegate(static_cast<const DelegateMessage&>(msg), ii, rb));
        break;
    case Message::T_GAP:
        gu_trace(handle_gap(static_cast<const GapMessage&>(msg), ii));
        break;
    case Message::T_JOIN:
        gu_trace(handle_join(static_cast<const JoinMessage&>(msg), ii));
        break;
    case Message::T_LEAVE:
        gu_trace(handle_leave(static_cast<const LeaveMessage&>(msg), ii));
        break;
    case Message::T_INSTALL:
        gu_trace(handle_install(static_cast<const InstallMessage&>(msg), ii));
        break;
    default:
        log_warn << "invalid message type " << msg.get_type();
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
    const byte_t* begin(get_begin(rb));
    const size_t available(get_available(rb));
    gu_trace(offset = msg->unserialize(begin,
                                       available,
                                       0));
    if ((msg->get_flags() & Message::F_SOURCE) == 0)
    {
        gcomm_assert(source != UUID::nil());
        msg->set_source(source);
    }

    switch (msg->get_type())
    {
    case Message::T_NONE:
        gu_throw_fatal;
        break;
    case Message::T_USER:
        gu_trace(offset = static_cast<UserMessage&>(*msg).unserialize(
                     begin, available, offset, true));
        break;
    case Message::T_DELEGATE:
        gu_trace(offset = static_cast<DelegateMessage&>(*msg).unserialize(
                     begin, available, offset, true));
        break;
    case Message::T_GAP:
        gu_trace(offset = static_cast<GapMessage&>(*msg).unserialize(
                     begin, available, offset, true));
        break;
    case Message::T_JOIN:
        gu_trace(offset = static_cast<JoinMessage&>(*msg).unserialize(
                     begin, available, offset, true));
        break;
    case Message::T_INSTALL:
        gu_trace(offset = static_cast<InstallMessage&>(*msg).unserialize(
                     begin, available, offset, true));
        break;
    case Message::T_LEAVE:
        gu_trace(offset = static_cast<LeaveMessage&>(*msg).unserialize(
                     begin, available, offset, true));
        break;
    }
    return (offset + rb.get_offset());
}

void gcomm::evs::Proto::handle_up(const void* cid,
                                  const Datagram& rb,
                                  const ProtoUpMeta& um)
{

    Message msg;

    if (get_state() == S_CLOSED || um.get_source() == get_uuid())
    {
        // Silent drop
        return;
    }

    gcomm_assert(um.get_source() != UUID::nil());

    try
    {
        size_t offset;
        gu_trace(offset = unserialize_message(um.get_source(), rb, &msg));
        handle_msg(msg, Datagram(rb, offset));
    }
    catch (Exception& e)
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
            log_fatal << " state after handling message: " << *this;
            throw;
        }
    }
}


int gcomm::evs::Proto::handle_down(Datagram& wb, const ProtoDownMeta& dm)
{
    if (get_state() == S_GATHER || get_state() == S_INSTALL)
    {
        return EAGAIN;
    }

    else if (get_state() != S_OPERATIONAL)
    {
        log_warn << "user message in state " << to_string(get_state());
        return ENOTCONN;
    }

    if (dm.get_order() == O_LOCAL_CAUSAL)
    {
        if (causal_queue_.empty() == true &&
            last_sent == input_map->get_safe_seq())
        {
            hs_local_causal.insert(0.0);
            deliver_causal(dm.get_user_type(), last_sent, wb);
        }
        else
        {
            causal_queue_.push_back(CausalMessage(dm.get_user_type(),
                                                  input_map->get_aru_seq(), wb));
        }
        return 0;
    }


    send_queue_s += output.size();
    ++n_send_queue_s;

    int ret = 0;

    if (output.empty() == true)
    {
        int err;
        err = send_user(wb,
                        dm.get_user_type(),
                        dm.get_order(),
                        user_send_window,
                        -1);

        switch (err)
        {
        case EAGAIN:
        {
            output.push_back(make_pair(wb, dm));
            // Fall through
        }
        case 0:
            ret = 0;
            break;
        default:
            log_error << "send error: " << err;
            ret = err;
        }
    }
    else if (output.size() < max_output_size)
    {
        output.push_back(make_pair(wb, dm));
    }
    else
    {
        ret = EAGAIN;
    }
    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// State handler
/////////////////////////////////////////////////////////////////////////////

void gcomm::evs::Proto::shift_to(const State s, const bool send_j)
{
    if (shift_to_rfcnt > 0) gu_throw_fatal << *this;

    shift_to_rfcnt++;

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

    if (allowed[state][s] == false) {
        gu_throw_fatal << "Forbidden state transition: "
                          << to_string(state) << " -> " << to_string(s);
    }

    if (get_state() != s)
    {
        evs_log_info(I_STATE) << " state change: "
                              << to_string(state) << " -> " << to_string(s);
    }
    switch (s) {
    case S_CLOSED:
        gcomm_assert(get_state() == S_LEAVING);
        gu_trace(deliver());
        gu_trace(deliver_local());
        setall_installed(false);
        NodeMap::get_value(self_i).set_installed(true);
        gu_trace(deliver_trans_view(true));
        gu_trace(deliver_trans());
        gu_trace(deliver_local(true));
        gcomm_assert(causal_queue_.empty() == true);
        if (collect_stats == true)
        {
            handle_stats_timer();
        }
        gu_trace(deliver_empty_view());
        cleanup_unoperational();
        cleanup_views();
        timers.clear();
        state = S_CLOSED;
        break;
    case S_JOINING:
        state = S_JOINING;
        break;
    case S_LEAVING:
        state = S_LEAVING;
        reset_timers();
        break;
    case S_GATHER:
    {
        setall_committed(false);
        setall_installed(false);
        delete install_message;
        install_message = 0;

        if (get_state() == S_OPERATIONAL)
        {
            profile_enter(send_user_prof);
            while (output.empty() == false)
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
            profile_leave(send_user_prof);
        }
        else
        {
            gcomm_assert(output.empty() == true);
        }

        state = S_GATHER;
        if (send_j == true)
        {
            profile_enter(send_join_prof);
            gu_trace(send_join(false));
            profile_leave(send_join_prof);
        }
        gcomm_assert(get_state() == S_GATHER);
        reset_timers();
        break;
    }
    case S_INSTALL:
    {
        gcomm_assert(install_message != 0);
        gcomm_assert(is_all_committed() == true);
        state = S_INSTALL;
        reset_timers();
        break;
    }
    case S_OPERATIONAL:
    {
        gcomm_assert(output.empty() == true);
        gcomm_assert(install_message != 0);
        gcomm_assert(NodeMap::get_value(self_i).get_join_message() != 0 &&
                     consensus.equal(
                         *NodeMap::get_value(self_i).get_join_message(),
                         *install_message))
            << "install message not consistent with own join, state: " << *this;
        gcomm_assert(is_all_installed() == true);
        gu_trace(deliver());
        gu_trace(deliver_local());
        gu_trace(deliver_trans_view(false));
        gu_trace(deliver_trans());
        gu_trace(deliver_local(true));
        gcomm_assert(causal_queue_.empty() == true);
        input_map->clear();
        if (collect_stats == true)
        {
            handle_stats_timer();
        }

        previous_view = current_view;
        previous_views.push_back(make_pair(current_view.get_id(), Date::now()));

        const MessageNodeList& imap(install_message->get_node_list());

        for (MessageNodeList::const_iterator i = imap.begin();
             i != imap.end(); ++i)
        {
            previous_views.push_back(make_pair(MessageNodeList::get_value(i).get_view_id(),
                                               Date::now()));
        }
        current_view = View(install_message->get_install_view_id());
        size_t idx = 0;
        for (NodeMap::iterator i = known.begin(); i != known.end(); ++i)
        {
            if (NodeMap::get_value(i).get_installed() == true)
            {
                gu_trace(current_view.add_member(NodeMap::get_key(i), ""));
                NodeMap::get_value(i).set_index(idx++);
            }
            else
            {
                NodeMap::get_value(i).set_index(numeric_limits<size_t>::max());
            }
        }

        if (previous_view.get_id().get_type() == V_REG &&
            previous_view.get_members() == current_view.get_members())
        {
            log_warn << "subsequent views have same members, prev view "
                     << previous_view << " current view " << current_view;
        }

        input_map->reset(current_view.get_members().size());
        last_sent = -1;
        state = S_OPERATIONAL;
        deliver_reg_view();

        cleanup_unoperational();
        cleanup_views();
        cleanup_joins();

        delete install_message;
        install_message = 0;
        attempt_seq = 1;
        install_timeout_count = 0;
        profile_enter(send_gap_prof);
        gu_trace(send_gap(UUID::nil(), current_view.get_id(), Range()));;
        profile_leave(send_gap_prof);
        gcomm_assert(get_state() == S_OPERATIONAL);
        reset_timers();
        break;
    }
    default:
        gu_throw_fatal << "invalid state";
    }
    shift_to_rfcnt--;
}

////////////////////////////////////////////////////////////////////////////
// Message delivery
////////////////////////////////////////////////////////////////////////////

void gcomm::evs::Proto::deliver_causal(uint8_t user_type,
                                       seqno_t seqno,
                                       const gu::Datagram& datagram)
{
    send_up(datagram, ProtoUpMeta(get_uuid(),
                                  current_view.get_id(),
                                  0,
                                  user_type,
                                  O_LOCAL_CAUSAL,
                                  seqno));
    ++delivered_msgs[O_LOCAL_CAUSAL];
}


void gcomm::evs::Proto::deliver_local(bool trans)
{
    // local causal
    const seqno_t causal_seq(trans == false ? input_map->get_safe_seq() : last_sent);
    gu::datetime::Date now(gu::datetime::Date::now());
    while (causal_queue_.empty() == false &&
           causal_queue_.front().seqno() <= causal_seq)
    {
        const CausalMessage& cm(causal_queue_.front());
        hs_local_causal.insert(double(now.get_utc() - cm.tstamp().get_utc())/gu::datetime::Sec);
        deliver_causal(cm.user_type(), cm.seqno(), cm.datagram());
        causal_queue_.pop_front();
    }
}

void gcomm::evs::Proto::validate_reg_msg(const UserMessage& msg)
{
    if (msg.get_source_view_id() != current_view.get_id())
    {
        // Note: This implementation should guarantee same view delivery,
        // this is sanity check for that.
        gu_throw_fatal << "reg validate: not current view";
    }

    if (collect_stats == true)
    {
        if (msg.get_order() == O_SAFE)
        {
            Date now(Date::now());
            hs_safe.insert(double(now.get_utc() - msg.get_tstamp().get_utc())/gu::datetime::Sec);
        }
        else if (msg.get_order() == O_AGREED)
        {
            Date now(Date::now());
            hs_agreed.insert(double(now.get_utc() - msg.get_tstamp().get_utc())/gu::datetime::Sec);
        }
    }
}


void gcomm::evs::Proto::deliver_finish(const InputMapMsg& msg)
{
    if ((msg.get_msg().get_flags() & Message::F_AGGREGATE) == 0)
    {
        ++delivered_msgs[msg.get_msg().get_order()];
        if (msg.get_msg().get_order() != O_DROP)
        {
            gu_trace(validate_reg_msg(msg.get_msg()));
            profile_enter(delivery_prof);
            ProtoUpMeta um(msg.get_msg().get_source(),
                           msg.get_msg().get_source_view_id(),
                           0,
                           msg.get_msg().get_user_type(),
                           msg.get_msg().get_order(),
                           msg.get_msg().get_seq());
            try
            {
                send_up(msg.get_rb(), um);
            }
            catch (...)
            {
                log_info << msg.get_msg() << " " << msg.get_rb().get_len();
                throw;
            }
            profile_leave(delivery_prof);
        }
    }
    else
    {
        size_t offset(0);
        while (offset < msg.get_rb().get_len())
        {
            ++delivered_msgs[msg.get_msg().get_order()];
            AggregateMessage am;
            gu_trace(am.unserialize(&msg.get_rb().get_payload()[0],
                                    msg.get_rb().get_payload().size(),
                                    offset));
            Datagram dg(
                SharedBuffer(
                    new Buffer(
                        &msg.get_rb().get_payload()[0]
                        + offset
                        + am.serial_size(),
                        &msg.get_rb().get_payload()[0]
                        + offset
                        + am.serial_size()
                        + am.get_len())));
            ProtoUpMeta um(msg.get_msg().get_source(),
                           msg.get_msg().get_source_view_id(),
                           0,
                           am.get_user_type(),
                           msg.get_msg().get_order(),
                           msg.get_msg().get_seq());
            gu_trace(send_up(dg, um));
            offset += am.serial_size() + am.get_len();
        }
        gcomm_assert(offset == msg.get_rb().get_len());
    }
}

void gcomm::evs::Proto::deliver()
{
    if (delivering == true)
    {
        gu_throw_fatal << "Recursive enter to delivery";
    }

    delivering = true;

    if (get_state() != S_OPERATIONAL &&
        get_state() != S_GATHER      &&
        get_state() != S_INSTALL     &&
        get_state() != S_LEAVING)
    {
        gu_throw_fatal << "invalid state: " << to_string(get_state());
    }

    evs_log_debug(D_DELIVERY)
        << " aru_seq="   << input_map->get_aru_seq()
        << " safe_seq=" << input_map->get_safe_seq();

    InputMapMsgIndex::iterator i, i_next;
    for (i = input_map->begin(); i != input_map->end(); i = i_next)
    {
        i_next = i;
        ++i_next;
        const InputMapMsg& msg(InputMapMsgIndex::get_value(i));
        bool deliver = false;
        switch (msg.get_msg().get_order())
        {
        case O_SAFE:
            if (input_map->is_safe(i) == true)
            {
                deliver = true;
            }
            break;
        case O_AGREED:
            if (input_map->is_agreed(i) == true)
            {
                deliver = true;
            }
            break;
        case O_FIFO:
        case O_DROP:
            if (input_map->is_fifo(i) == true)
            {
                deliver = true;
            }
            break;
        default:
            gu_throw_fatal << "invalid safety prefix "
                              << msg.get_msg().get_order();
        }

        if (deliver == true)
        {
            deliver_finish(msg);
            gu_trace(input_map->erase(i));
        }
        else if (input_map->has_deliverables() == false)
        {
            break;
        }
    }
    delivering = false;

}


void gcomm::evs::Proto::deliver_trans()
{
    if (delivering == true)
    {
        gu_throw_fatal << "Recursive enter to delivery";
    }

    delivering = true;

    if (get_state() != S_INSTALL &&
        get_state() != S_LEAVING)
        gu_throw_fatal << "invalid state";

    evs_log_debug(D_DELIVERY)
        << " aru_seq="  << input_map->get_aru_seq()
        << " safe_seq=" << input_map->get_safe_seq();

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
    for (i = input_map->begin(); i != input_map->end(); i = i_next)
    {
        i_next = i;
        ++i_next;
        const InputMapMsg& msg(InputMapMsgIndex::get_value(i));
        bool deliver = false;
        switch (msg.get_msg().get_order())
        {
        case O_SAFE:
        case O_AGREED:
        case O_FIFO:
        case O_DROP:
            if (input_map->is_fifo(i) == true)
            {
                deliver = true;
            }
            break;
        default:
            gu_throw_fatal;
        }

        if (deliver == true)
        {
            if (install_message != 0)
            {
                const MessageNode& mn(
                    MessageNodeList::get_value(
                        install_message->get_node_list().find_checked(
                            msg.get_msg().get_source())));
                if (msg.get_msg().get_seq() <= mn.get_im_range().get_hs())
                {
                    deliver_finish(msg);
                }
                else
                {
                    gcomm_assert(mn.get_operational() == false);
                    log_info << "filtering out trans message higher than "
                             << "install message hs "
                             << mn.get_im_range().get_hs()
                             << ": " << msg.get_msg();
                }
            }
            else
            {
                deliver_finish(msg);
            }
            gu_trace(input_map->erase(i));
        }
    }

    // Sanity check:
    // There must not be any messages left that
    // - Are originated from outside of trans conf and are FIFO
    // - Are originated from trans conf
    for (i = input_map->begin(); i != input_map->end(); i = i_next)
    {
        i_next = i;
        ++i_next;
        const InputMapMsg& msg(InputMapMsgIndex::get_value(i));
        NodeMap::iterator ii;
        gu_trace(ii = known.find_checked(msg.get_msg().get_source()));

        if (NodeMap::get_value(ii).get_installed() == true)
        {
            gu_throw_fatal << "Protocol error in transitional delivery "
                           << "(self delivery constraint)";
        }
        else if (input_map->is_fifo(i) == true)
        {
            gu_throw_fatal << "Protocol error in transitional delivery "
                           << "(fifo from partitioned component)";
        }
        gu_trace(input_map->erase(i));
    }
    delivering = false;
}


/////////////////////////////////////////////////////////////////////////////
// Message handlers
/////////////////////////////////////////////////////////////////////////////



gcomm::evs::seqno_t gcomm::evs::Proto::update_im_safe_seq(const size_t uuid,
                                                          const seqno_t seq)
{
    const seqno_t im_safe_seq(input_map->get_safe_seq(uuid));
    if (im_safe_seq  < seq)
    {
        input_map->set_safe_seq(uuid, seq);
    }
    return im_safe_seq;
}


void gcomm::evs::Proto::handle_user(const UserMessage& msg,
                                    NodeMap::iterator ii,
                                    const Datagram& rb)

{
    assert(ii != known.end());
    assert(get_state() != S_CLOSED && get_state() != S_JOINING);
    Node& inst(NodeMap::get_value(ii));

    evs_log_debug(D_USER_MSGS) << "received " << msg;

    if (msg.get_source_view_id() != current_view.get_id())
    {
        if (get_state() == S_LEAVING)
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

        if (inst.get_operational() == false)
        {
            evs_log_debug(D_STATE)
                << "dropping message from unoperational source "
                << msg.get_source();
            return;
        }
        else if (inst.get_installed() == false)
        {
            if (install_message != 0 &&
                msg.get_source_view_id() == install_message->get_install_view_id())
            {
                assert(state == S_INSTALL);
                evs_log_debug(D_STATE) << " recovery user message "
                                       << msg;

                // Other instances installed view before this one, so it is
                // safe to shift to S_OPERATIONAL if consensus has been reached
                for (MessageNodeList::const_iterator
                         mi = install_message->get_node_list().begin();
                     mi != install_message->get_node_list().end(); ++mi)
                {
                    if (MessageNodeList::get_value(mi).get_operational() == true)
                    {
                        NodeMap::iterator jj;
                        gu_trace(jj = known.find_checked(
                                     MessageNodeList::get_key(mi)));
                        NodeMap::get_value(jj).set_installed(true);
                    }
                }
                inst.set_tstamp(Date::now());
                if (consensus.is_consensus() == true)
                {
                    if (get_state() == S_INSTALL)
                    {
                        profile_enter(shift_to_prof);
                        gu_trace(shift_to(S_OPERATIONAL));
                        profile_leave(shift_to_prof);
                    }
                    else
                    {
                        log_warn << self_string()
                                 << "received user message from new "
                                 << "view while in GATHER, dropping";
                        return;
                    }
                }
                else
                {
                    profile_enter(shift_to_prof);
                    evs_log_info(I_STATE)
                        << " no consensus after "
                        << "handling user message from new view";
                    // Don't change state here but continue waiting for
                    // install gaps. Other nodes should time out eventually
                    // if consensus can't be reached.
                    // gu_trace(shift_to(S_GATHER));
                    profile_leave(shift_to_prof);
                    return;
                }
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

    gcomm_assert(msg.get_source_view_id() == current_view.get_id());

    Range range;
    Range prev_range;
    seqno_t prev_aru;
    seqno_t prev_safe;

    profile_enter(input_map_prof);

    prev_aru = input_map->get_aru_seq();
    prev_range = input_map->get_range(inst.get_index());

    // Insert only if msg seq is greater or equal than current lowest unseen
    if (msg.get_seq() >= prev_range.get_lu())
    {
        Datagram im_dgram(rb, rb.get_offset());
        im_dgram.normalize();
        gu_trace(range = input_map->insert(inst.get_index(), msg, im_dgram));
        if (range.get_lu() > prev_range.get_lu())
        {
            inst.set_tstamp(Date::now());
        }
    }
    else
    {
        range = prev_range;
    }

    // Update im safe seq for self
    update_im_safe_seq(NodeMap::get_value(self_i).get_index(),
                       input_map->get_aru_seq());

    // Update safe seq for message source
    prev_safe = update_im_safe_seq(inst.get_index(), msg.get_aru_seq());

    profile_leave(input_map_prof);

    // Check for missing messages
    if (range.get_hs()                         >  range.get_lu() &&
        (msg.get_flags() & Message::F_RETRANS) == 0                 )
    {
        evs_log_debug(D_RETRANS) << " requesting retrans from "
                                 << msg.get_source() << " "
                                 << range
                                 << " due to input map gap, aru "
                                 << input_map->get_aru_seq();
        profile_enter(send_gap_prof);
        gu_trace(send_gap(msg.get_source(), current_view.get_id(), range));
        profile_leave(send_gap_prof);
    }

    // Seqno range completion and acknowledgement
    const seqno_t max_hs(input_map->get_max_hs());
    if (output.empty()                          == true            &&
        (get_state() == S_OPERATIONAL || get_state() == S_GATHER)  &&
        (msg.get_flags() & Message::F_MSG_MORE) == 0               &&
        (last_sent                              <  max_hs))
    {
        // Message not originated from this instance, output queue is empty
        // and last_sent seqno should be advanced
        gu_trace(complete_user(max_hs));
    }
    else if (output.empty()           == true  &&
             input_map->get_aru_seq() != prev_aru)
    {
        // Output queue empty and aru changed, send gap to inform others
        evs_log_debug(D_GAP_MSGS) << "sending empty gap";
        profile_enter(send_gap_prof);
        gu_trace(send_gap(UUID::nil(), current_view.get_id(), Range()));
        profile_leave(send_gap_prof);
    }

    // Send messages
    if (get_state() == S_OPERATIONAL)
    {
        profile_enter(send_user_prof);
        while (output.empty() == false)
        {
            int err;
            gu_trace(err = send_user(send_window));
            if (err != 0)
            {
                break;
            }
        }
        profile_leave(send_user_prof);
    }

    // Deliver messages
    profile_enter(delivery_prof);
    if (input_map->has_deliverables() == true)
    {
        gu_trace(deliver());
    }
    gu_trace(deliver_local());
    profile_leave(delivery_prof);

    // If in recovery state, send join each time input map aru seq reaches
    // last sent and either input map aru or safe seq has changed.
    if (get_state()                  == S_GATHER &&
        consensus.highest_reachable_safe_seq() == input_map->get_aru_seq() &&
        (prev_aru                    != input_map->get_aru_seq() ||
         prev_safe                   != input_map->get_safe_seq()) &&
        (msg.get_flags() & Message::F_RETRANS) == 0)
    {
        gcomm_assert(output.empty() == true);
        if (consensus.is_consensus() == false)
        {
            profile_enter(send_join_prof);
            gu_trace(send_join());
            profile_leave(send_join_prof);
        }
    }
}


void gcomm::evs::Proto::handle_delegate(const DelegateMessage& msg,
                                        NodeMap::iterator ii,
                                        const Datagram& rb)
{
    gcomm_assert(ii != known.end());
    // evs_log_debug(D_DELEGATE_MSGS) << "delegate message " << msg;
    log_debug << "delegate";
    Message umsg;
    size_t offset;
    gu_trace(offset = unserialize_message(UUID::nil(), rb, &umsg));
    gu_trace(handle_msg(umsg, Datagram(rb, offset)));
}


void gcomm::evs::Proto::handle_gap(const GapMessage& msg, NodeMap::iterator ii)
{
    assert(ii != known.end());
    assert(get_state() != S_CLOSED && get_state() != S_JOINING);

    Node& inst(NodeMap::get_value(ii));
    evs_log_debug(D_GAP_MSGS) << "gap message " << msg;


    if ((msg.get_flags() & Message::F_COMMIT) != 0)
    {
        log_debug << self_string() << " commit gap from " << msg.get_source();
        if (get_state()                           == S_GATHER &&
            install_message                       != 0 &&
            install_message->get_fifo_seq()       == msg.get_seq())
        {
            inst.set_committed(true);
            inst.set_tstamp(Date::now());
            if (is_all_committed() == true)
            {
                shift_to(S_INSTALL);
                gu_trace(send_gap(UUID::nil(),
                                  install_message->get_install_view_id(),
                                  Range()));;
            }
        }
        else if (get_state() == S_GATHER &&
                 install_message != 0 &&
                 install_message->get_fifo_seq() < msg.get_seq())
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
    else if (get_state()                           == S_INSTALL  &&
             install_message                       != 0          &&
             install_message->get_install_view_id() == msg.get_source_view_id())
    {
        evs_log_debug(D_STATE) << "install gap " << msg;
        inst.set_installed(true);
        inst.set_tstamp(Date::now());
        if (is_all_installed() == true)
        {
            profile_enter(shift_to_prof);
            gu_trace(shift_to(S_OPERATIONAL));
            profile_leave(shift_to_prof);
        }
        return;
    }
    else if (msg.get_source_view_id() != current_view.get_id())
    {
        if (get_state() == S_LEAVING)
        {
            // Silently drop
            return;
        }

        if (is_msg_from_previous_view(msg) == true)
        {
            evs_log_debug(D_FOREIGN_MSGS) << "gap message from previous view";
            return;
        }

        if (inst.get_operational() == false)
        {
            evs_log_debug(D_STATE)
                << "dropping message from unoperational source "
                << msg.get_source();
        }
        else if (inst.get_installed() == false)
        {
            evs_log_debug(D_STATE)
                << "dropping message from uninstalled source "
                << msg.get_source();
        }
        else
        {
            log_debug << "unhandled gap message " << msg;
        }
        return;
    }

    gcomm_assert(msg.get_source_view_id() == current_view.get_id());

    //
    seqno_t prev_safe;

    profile_enter(input_map_prof);
    prev_safe = update_im_safe_seq(inst.get_index(), msg.get_aru_seq());

    // Deliver messages and update tstamp only if safe_seq changed
    // for the source.
    if (prev_safe != input_map->get_safe_seq(inst.get_index()))
    {
        inst.set_tstamp(Date::now());
    }
    profile_leave(input_map_prof);

    //
    if (msg.get_range_uuid() == get_uuid())
    {
        if (msg.get_range().get_hs() > last_sent &&
            (get_state() == S_OPERATIONAL || get_state() == S_GATHER))
        {
            // This could be leaving node requesting messages up to
            // its last sent.
            gu_trace(complete_user(msg.get_range().get_hs()));
        }
        gu_trace(resend(msg.get_source(), msg.get_range()));
    }

    //
    if (get_state() == S_OPERATIONAL)
    {
        if (output.empty() == false)
        {
            profile_enter(send_user_prof);
            while (output.empty() == false)
            {
                int err;
                gu_trace(err = send_user(send_window));
                if (err != 0)
                    break;
            }
            profile_leave(send_user_prof);
        }
        else
        {
            const seqno_t max_hs(input_map->get_max_hs());
            if (last_sent <  max_hs)
            {
                gu_trace(complete_user(max_hs));
            }
        }
    }

    profile_enter(delivery_prof);
    if (input_map->has_deliverables() == true)
    {
        gu_trace(deliver());
    }
    gu_trace(deliver_local());
    profile_leave(delivery_prof);

    //
    if (get_state()                            == S_GATHER                  &&
        consensus.highest_reachable_safe_seq() == input_map->get_aru_seq()  &&
        prev_safe                              != input_map->get_safe_seq()   )
    {
        gcomm_assert(output.empty() == true);
        if (consensus.is_consensus() == false)
        {
            profile_enter(send_join_prof);
            gu_trace(send_join());
            profile_leave(send_join_prof);
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
        const UUID& uuid(MessageNodeList::get_key(i));
        const Node& local_node(NodeMap::get_value(known.find_checked(uuid)));
        const MessageNode& node(MessageNodeList::get_value(i));
        gcomm_assert(node.get_view_id() == current_view.get_id());
        const seqno_t safe_seq(node.get_safe_seq());
        seqno_t prev_safe_seq;
        gu_trace(prev_safe_seq = update_im_safe_seq(local_node.get_index(), safe_seq));
        if (prev_safe_seq                 != safe_seq &&
            input_map->get_safe_seq(local_node.get_index()) == safe_seq)
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
        const UUID& uuid(MessageNodeList::get_key(i));
        const MessageNode& mn(MessageNodeList::get_value(i));
        const Node& n(NodeMap::get_value(known.find_checked(uuid)));
        const Range r(input_map->get_range(n.get_index()));

        if (uuid == get_uuid() &&
            mn.get_im_range().get_lu() != r.get_lu())
        {
            // Source member is missing messages from us
            gcomm_assert(mn.get_im_range().get_hs() <= last_sent);
            gu_trace(resend(nl_uuid,
                            Range(mn.get_im_range().get_lu(), last_sent)));
        }
        else if ((mn.get_operational() == false ||
                  mn.get_leaving() == true) &&
                 uuid != get_uuid() &&
                 (mn.get_im_range().get_lu() < r.get_lu() ||
                  mn.get_im_range().get_hs() < r.get_hs()))
        {
            gu_trace(recover(nl_uuid, uuid,
                             Range(mn.get_im_range().get_lu(),
                                   r.get_hs())));
        }
    }
}

void gcomm::evs::Proto::retrans_leaves(const MessageNodeList& node_list)
{
    for (NodeMap::const_iterator li = known.begin(); li != known.end(); ++li)
    {
        const Node& local_node(NodeMap::get_value(li));
        if (local_node.get_leave_message() != 0 &&
            local_node.is_inactive()       == false)
        {
            MessageNodeList::const_iterator msg_li(
                node_list.find(NodeMap::get_key(li)));

            if (msg_li == node_list.end() ||
                MessageNodeList::get_value(msg_li).get_leaving() == false)
            {
                const LeaveMessage& lm(*NodeMap::get_value(li).get_leave_message());
                LeaveMessage send_lm(lm.get_version(),
                                     lm.get_source(),
                                     lm.get_source_view_id(),
                                     lm.get_seq(),
                                     lm.get_aru_seq(),
                                     lm.get_fifo_seq(),
                                     Message::F_RETRANS | Message::F_SOURCE);

                Buffer buf;
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
    SelectSuspectsOp(MessageNodeList& nl) : nl_(nl) { }

    void operator()(const MessageNodeList::value_type& vt) const
    {
        if (MessageNodeList::get_value(vt).get_suspected() == true)
        {
            nl_.insert_unique(vt);
        }
    }
private:
    MessageNodeList& nl_;
};

void gcomm::evs::Proto::check_suspects(const UUID& source,
                                       const MessageNodeList& nl)
{
    assert(source != get_uuid());
    MessageNodeList suspected;
    for_each(nl.begin(), nl.end(), SelectSuspectsOp(suspected));

    for (MessageNodeList::const_iterator i(suspected.begin());
         i != suspected.end(); ++i)
    {
        const UUID& uuid(MessageNodeList::get_key(i));
        const MessageNode& node(MessageNodeList::get_value(i));
        if (node.get_suspected() == true)
        {
            if (uuid != get_uuid())
            {
                size_t s_cnt(0);
                // Iterate over join messages to see if majority agrees
                // with the suspicion
                for (NodeMap::const_iterator j(known.begin());
                     j != known.end(); ++j)
                {
                    const JoinMessage* jm(NodeMap::get_value(j).get_join_message());
                    if (jm != 0 && jm->get_source() != uuid)
                    {
                        MessageNodeList::const_iterator mni(jm->get_node_list().find(uuid));
                        if (mni != jm->get_node_list().end())
                        {
                            const MessageNode& mn(MessageNodeList::get_value(mni));
                            if (mn.get_suspected() == true)
                            {
                                ++s_cnt;
                            }
                        }
                    }
                }
                const Node& kn(NodeMap::get_value(known.find_checked(uuid)));
                if (kn.get_operational() == true &&
                    s_cnt > current_view.get_members().size()/2)
                {
                    evs_log_info(I_STATE)
                        << " declaring suspected "
                        << uuid << " as inactive";
                    set_inactive(uuid);
                }
            }
        }
    }
}


// If one node has set some other as unoperational but we see it still
// operational, do some arbitration. We wait until 2/3 of inactive timeout
// from setting of consensus timer has passed. If both nodes express liveness,
// select the one with greater uuid as a victim of exclusion.
void gcomm::evs::Proto::cross_check_inactives(const UUID& source,
                                              const MessageNodeList& nl)
{
    assert(source != get_uuid());

    const Date now(Date::now());
    const Period wait_c_to((install_timeout*2)/3); // Wait for consensus

    TimerList::const_iterator ti(
        find_if(timers.begin(), timers.end(), TimerSelectOp(T_INSTALL)));
    assert(ti != timers.end());
    if (now + wait_c_to < TimerList::get_key(ti))
    {
        // No check yet
        return;
    }

    NodeMap::const_iterator source_i(known.find_checked(source));
    const Node& local_source_node(NodeMap::get_value(source_i));
    const Period ito((install_timeout*1)/3);       // Tighter inactive timeout

    MessageNodeList inactive;
    for_each(nl.begin(), nl.end(), SelectNodesOp(inactive, ViewId(), false, false));
    for (MessageNodeList::const_iterator i = inactive.begin();
         i != inactive.end(); ++i)
    {
        const UUID& uuid(MessageNodeList::get_key(i));
        const MessageNode& node(MessageNodeList::get_value(i));
        gcomm_assert(node.get_operational() == false);
        NodeMap::iterator local_i(known.find(uuid));

        if (local_i != known.end() && uuid != get_uuid())
        {
            Node& local_node(NodeMap::get_value(local_i));
            if (local_node.get_operational()         == true &&
                local_source_node.get_tstamp() + ito >= now  &&
                local_node.get_tstamp() + ito        >= now  &&
                uuid > source)
            {
                evs_log_info(I_STATE) << " arbitrating, select " << uuid;
                set_inactive(uuid);
            }
        }
    }
}


void gcomm::evs::Proto::handle_join(const JoinMessage& msg, NodeMap::iterator ii)
{
    assert(ii != known.end());
    assert(get_state() != S_CLOSED);

    Node& inst(NodeMap::get_value(ii));

    evs_log_debug(D_JOIN_MSGS) << " " << msg;

    if (get_state() == S_LEAVING)
    {
        if (msg.get_source_view_id() == current_view.get_id())
        {
            inst.set_tstamp(Date::now());
            MessageNodeList same_view;
            for_each(msg.get_node_list().begin(), msg.get_node_list().end(),
                     SelectNodesOp(same_view, current_view.get_id(),
                                   true, true));
            profile_enter(input_map_prof);
            if (update_im_safe_seqs(same_view) == true)
            {
                profile_enter(send_leave_prof);
                gu_trace(send_leave(false));
                profile_leave(send_leave_prof);
            }
            for (NodeMap::const_iterator i = known.begin(); i != known.end();
                 ++i)
            {
                const UUID& uuid(NodeMap::get_key(i));
                const Node& node(NodeMap::get_value(i));
                if (current_view.is_member(uuid) == true)
                {
                    const Range r(input_map->get_range(node.get_index()));
                    if (r.get_lu() <= last_sent)
                    {
                        send_gap(uuid, current_view.get_id(),
                                 Range(r.get_lu(), last_sent));
                    }
                }
            }
            profile_leave(input_map_prof);
            gu_trace(retrans_user(msg.get_source(), same_view));
        }
        return;
    }
    else if (is_msg_from_previous_view(msg) == true)
    {
        return;
    }
    else if (install_message != 0)
    {
        // Note: don't send join from this branch yet, join is
        // sent at the end of this method
        if (install_message->get_source() == msg.get_source())
        {
            evs_log_info(I_STATE)
                << "shift to gather due to representative "
                << msg.get_source() << " join";
            gu_trace(shift_to(S_GATHER, false));
        }
        else if (consensus.is_consistent(*install_message) == true)
        {
            return;
            // Commented out: It seems to be better strategy to
            // just wait source of inconsistent join to time out
            // instead of shifting to gather. #443

            // if (consensus.is_consistent(msg) == true)
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
                << msg.get_source() << " " << msg.get_source_view_id();
            evs_log_info(I_STATE) << "state: " << *this;
            gu_trace(shift_to(S_GATHER, false));
        }
    }
    else if (get_state() != S_GATHER)
    {
        evs_log_info(I_STATE)
            << " shift to GATHER while handling join message from "
            << msg.get_source() << " " << msg.get_source_view_id();
        gu_trace(shift_to(S_GATHER, false));
    }

    gcomm_assert(output.empty() == true);

    // @todo Figure out how to avoid setting tstamp from any join message
    if (inst.get_join_message() != 0)
        inst.set_tstamp(Date::now());
    inst.set_join_message(&msg);

    // Add unseen nodes to known list. No need to adjust node state here,
    // it is done later on in check_suspects()/cross_check_inactives().
    for (MessageNodeList::const_iterator i(msg.get_node_list().begin());
         i != msg.get_node_list().end(); ++i)
    {
        NodeMap::iterator ni(known.find(MessageNodeList::get_key(i)));
        if (ni == known.end())
        {
            known.insert_unique(
                make_pair(MessageNodeList::get_key(i),
                          Node(inactive_timeout, suspect_timeout)));
        }
    }

    // Select nodes that are coming from the same view as seen by
    // message source
    MessageNodeList same_view;
    for_each(msg.get_node_list().begin(), msg.get_node_list().end(),
             SelectNodesOp(same_view, current_view.get_id(), true, true));
    // Find out self from node list
    MessageNodeList::const_iterator nlself_i(same_view.find(get_uuid()));

    // Other node coming from the same view
    if (msg.get_source()         != get_uuid() &&
        msg.get_source_view_id() == current_view.get_id())
    {
        gcomm_assert(nlself_i != same_view.end());
        // Update input map state
        (void)update_im_safe_seqs(same_view);

        // Find out max hs and complete up to that if needed
        MessageNodeList::const_iterator max_hs_i(
            max_element(same_view.begin(), same_view.end(), RangeHsCmp()));
        const seqno_t max_hs(MessageNodeList::get_value(max_hs_i).get_im_range().get_hs());
        if (last_sent < max_hs)
        {
            gu_trace(complete_user(max_hs));
        }
    }

    //
    gu_trace(retrans_user(msg.get_source(), same_view));
    // Retrans leave messages that others are missing
    gu_trace(retrans_leaves(same_view));

    // Check if this node is set inactive by the other,
    // if yes, the other must be marked as inactive too
    if (msg.get_source() != get_uuid() && nlself_i != same_view.end())
    {
        if (MessageNodeList::get_value(nlself_i).get_operational() == false)
        {

            evs_log_info(I_STATE)
                << " declaring source " << msg.get_source()
                << " as inactive (mutual exclusion)";
            set_inactive(msg.get_source());
        }
    }

    // Make cross check to resolve conflict if two nodes
    // declare each other inactive. There is no need to make
    // this for own messages.
    if (msg.get_source() != get_uuid())
    {
        gu_trace(check_suspects(msg.get_source(), same_view));
        gu_trace(cross_check_inactives(msg.get_source(), same_view));
    }

    // If current join message differs from current state, send new join
    const JoinMessage* curr_join(NodeMap::get_value(self_i).get_join_message());
    MessageNodeList new_nl;
    populate_node_list(&new_nl);

    if (curr_join == 0 ||
        (curr_join->get_aru_seq()   != input_map->get_aru_seq()  ||
         curr_join->get_seq()       != input_map->get_safe_seq() ||
         curr_join->get_node_list() != new_nl))
    {
        gu_trace(create_join());
        if (consensus.is_consensus() == false)
        {
            send_join(false);
        }
    }

    if (consensus.is_consensus() == true)
    {
        if (is_representative(get_uuid()) == true)
        {
            gu_trace(send_install());
        }
    }
}


void gcomm::evs::Proto::handle_leave(const LeaveMessage& msg,
                                     NodeMap::iterator ii)
{
    assert(ii != known.end());
    assert(get_state() != S_CLOSED && get_state() != S_JOINING);

    Node& node(NodeMap::get_value(ii));
    evs_log_debug(D_LEAVE_MSGS) << "leave message " << msg;

    if (msg.get_source() != get_uuid() && node.is_inactive() == true)
    {
        evs_log_debug(D_LEAVE_MSGS) << "dropping leave from already inactive";
        return;
    }
    node.set_leave_message(&msg);
    if (msg.get_source() == get_uuid())
    {
        // The last one to live, instant close. Otherwise continue
        // serving until it becomes apparent that others have
        // leave message.
        if (current_view.get_members().size() == 1)
        {
            profile_enter(shift_to_prof);
            gu_trace(shift_to(S_CLOSED));
            profile_leave(shift_to_prof);
        }
    }
    else
    {
        if (msg.get_source_view_id()       != current_view.get_id() ||
            is_msg_from_previous_view(msg) == true)
        {
            // Silent drop
            return;
        }
        node.set_operational(false);
        const seqno_t prev_safe_seq(update_im_safe_seq(node.get_index(), msg.get_aru_seq()));
        if (prev_safe_seq != input_map->get_safe_seq(node.get_index()))
        {
            node.set_tstamp(Date::now());
        }
        if (get_state() == S_OPERATIONAL)
        {
            profile_enter(shift_to_prof);
            evs_log_info(I_STATE)
                << " shift to GATHER when handling leave from "
                << msg.get_source() << " " << msg.get_source_view_id();
            gu_trace(shift_to(S_GATHER, true));
            profile_leave(shift_to_prof);
        }
        else if (get_state() == S_GATHER &&
                 prev_safe_seq != input_map->get_safe_seq(node.get_index()))
        {
            profile_enter(send_join_prof);
            gu_trace(send_join());
            profile_leave(send_join_prof);
        }
    }
}


void gcomm::evs::Proto::handle_install(const InstallMessage& msg,
                                       NodeMap::iterator ii)
{

    assert(ii != known.end());
    assert(get_state() != S_CLOSED && get_state() != S_JOINING);

    Node& inst(NodeMap::get_value(ii));

    evs_log_debug(D_INSTALL_MSGS) << "install msg " << msg;

    if (get_state() == S_LEAVING)
    {
        MessageNodeList::const_iterator mn_i = msg.get_node_list().find(get_uuid());
        if (mn_i != msg.get_node_list().end())
        {
            const MessageNode& mn(MessageNodeList::get_value(mn_i));
            if (mn.get_operational() == false || mn.get_leaving() == true)
            {
                profile_enter(shift_to_prof);
                gu_trace(shift_to(S_CLOSED));
                profile_leave(shift_to_prof);
            }
        }
        return;
    }
    else if (get_state()           == S_OPERATIONAL &&
             current_view.get_id() == msg.get_source_view_id())
    {
        evs_log_debug(D_INSTALL_MSGS)
            << "dropping install message in already installed view";
        return;
    }
    else if (inst.get_operational() == false)
    {
        evs_log_debug(D_INSTALL_MSGS)
            << "previously unoperatioal message source " << msg.get_source()
            << " discarding message";
        return;
    }
    else if (is_msg_from_previous_view(msg) == true)
    {
        evs_log_debug(D_FOREIGN_MSGS)
            << " dropping install message from previous view";
        return;
    }
    else if (install_message != 0)
    {
        log_warn << self_string()
                 << " shift to GATHER due to duplicate install";
        gu_trace(shift_to(S_GATHER));
        return;
    }
    else if (inst.get_installed() == true)
    {
        log_warn << self_string()
                 << " shift to GATHER due to inconsistent state";
        profile_enter(shift_to_prof);
        gu_trace(shift_to(S_GATHER));
        profile_leave(shift_to_prof);
        return;
    }
    else if (is_representative(msg.get_source()) == false)
    {
        log_warn << self_string()
                 << " source " << msg.get_source()
                 << " is not supposed to be representative";
        // Isolate node from my group
        // set_inactive(msg.get_source());
        profile_enter(shift_to_prof);
        gu_trace(shift_to(S_GATHER));
        profile_leave(shift_to_prof);
        return;
    }

    assert(install_message == 0);

    bool ic;
    if ((ic = consensus.is_consistent(msg)) == false)
    {
        gcomm_assert(msg.get_source() != get_uuid());
        // Construct join from install message so that the most recent
        // information from representative is updated to local state.
        const MessageNode& mn(
            MessageNodeList::get_value(
                msg.get_node_list().find_checked(msg.get_source())));
        JoinMessage jm(msg.get_version(),
                       msg.get_source(),
                       mn.get_view_id(),
                       msg.get_seq(),
                       msg.get_aru_seq(),
                       msg.get_fifo_seq(),
                       msg.get_node_list());
        handle_join(jm, ii);
        ic = consensus.is_consistent(msg);
    }

    if (ic == true)
    {
        inst.set_tstamp(Date::now());
        install_message = new InstallMessage(msg);
        profile_enter(send_gap_prof);
        gu_trace(send_gap(UUID::nil(), install_message->get_install_view_id(),
                          Range(), true));
        profile_leave(send_gap_prof);
    }
    else
    {
        evs_log_debug(D_INSTALL_MSGS)
            << "install message " << msg
            << " not consistent with state " << *this;
        profile_enter(shift_to_prof);
        gu_trace(shift_to(S_GATHER, true));
        profile_leave(shift_to_prof);
    }
}
