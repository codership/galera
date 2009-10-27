
#include "evs_proto.hpp"
#include "evs_message2.hpp"
#include "evs_input_map2.hpp"

#include "gcomm/transport.hpp"
#include "gcomm/conf.hpp"

#include <stdexcept>
#include <algorithm>

using namespace std;
using namespace std::rel_ops;
using namespace gu;
using namespace gcomm;
using namespace gcomm::evs;

// Convenience macros for debug and info logging
#define evs_log_debug(__mask__)   if ((debug_mask & (__mask__)) == 0) { } log_debug << self_string() << ": "
#define evs_log_info(__mask__)    if ((info_mask & (__mask__)) == 0) { } else log_info << self_string() << ": "



class SelectNodesOp
{
public:
    SelectNodesOp(MessageNodeList& nl_, 
                  const ViewId& view_id_, 
                  const bool operational_,
                  const bool leaving_ ) : 
        nl          (nl_), 
        view_id     (view_id_),
        operational (operational_),
        leaving     (leaving_)
    { }
    
    void operator()(const MessageNodeList::value_type& vt) const
    {
        const MessageNode& node(MessageNodeList::get_value(vt));
        if (node.get_view_id()      == view_id           &&
            ((operational           == true          && 
              leaving               == true            ) ||
             (node.get_operational()  == operational &&
              node.get_leaving()      == leaving       ) ) )
            
        {
            nl.insert_checked(vt);
        }
    }
private:
    MessageNodeList&       nl;
    ViewId           const view_id;
    bool             const operational;
    bool             const leaving;
};



class OperationalSelect
{
public:
    OperationalSelect(NodeMap& nm_) : nm(nm_) { }
    
    void operator()(const NodeMap::value_type& vt) const
    {
        if (NodeMap::get_value(vt).get_operational() == true)
        {
            nm.insert_checked(vt);
        }
    }
private:
    NodeMap& nm;
};



class LeaveSeqCmpOp
{
public:
    bool operator()(const MessageNodeList::value_type& a,
                    const MessageNodeList::value_type& b) const
    {
        const MessageNode& aval(MessageNodeList::get_value(a));
        const MessageNode& bval(MessageNodeList::get_value(b));
        gcomm_assert(aval.get_leaving() != false &&
                     bval.get_leaving() != false);
        const Seqno asec(aval.get_leave_seq());
        const Seqno bsec(bval.get_leave_seq());
        gcomm_assert(asec != Seqno::max() && bsec != Seqno::max());
        return (asec < bsec);
    }
};

class RangeHsCmp
{
public:
    bool operator()(const MessageNodeList::value_type& a,
                    const MessageNodeList::value_type& b) const
    {
        if (MessageNodeList::get_value(a).get_im_range().get_hs() == Seqno::max())
        {
            return true;
        }
        else if (MessageNodeList::get_value(b).get_im_range().get_hs() == Seqno::max())
        {
            return false;
        }
        else
        {
            return MessageNodeList::get_value(a).get_im_range().get_hs() < 
                MessageNodeList::get_value(b).get_im_range().get_hs();
        }
    }
};

class RangeLuCmp
{
public:
    bool operator()(const MessageNodeList::value_type& a,
                    const MessageNodeList::value_type& b) const
    {
        if (MessageNodeList::get_value(a).get_im_range().get_lu() == Seqno::max())
        {
            return true;
        }
        else if (MessageNodeList::get_value(b).get_im_range().get_lu() == Seqno::max())
        {
            return false;
        }
        else
        {
            return MessageNodeList::get_value(a).get_im_range().get_lu() < 
                MessageNodeList::get_value(b).get_im_range().get_lu();
        }
    }
};

class SafeSeqCmp
{
public:
    bool operator()(const MessageNodeList::value_type& a,
                    const MessageNodeList::value_type& b) const
    {
        if (MessageNodeList::get_value(a).get_safe_seq() == Seqno::max())
        {
            return true;
        }
        else if (MessageNodeList::get_value(b).get_safe_seq() == Seqno::max())
        {
            return false;
        }
        else
        {
            return MessageNodeList::get_value(a).get_safe_seq() < 
                MessageNodeList::get_value(b).get_safe_seq();
        }
    }
};




gcomm::evs::Node::Node(const Node& n) :
    operational(n.operational),
    installed(n.installed),
    join_message(n.join_message != 0 ? new JoinMessage(*n.join_message) : 0),
    leave_message(n.leave_message != 0 ? new LeaveMessage(*n.leave_message) : 0),
    tstamp(n.tstamp),
    fifo_seq(n.fifo_seq)
{
}

gcomm::evs::Node::~Node()
{
    delete join_message;
    delete leave_message;
}

void gcomm::evs::Node::set_join_message(const JoinMessage* jm)
{
    if (join_message != 0)
    {
        delete join_message;
    }
    if (jm != 0)
    {
        join_message = new JoinMessage(*jm);
    }
    else
    {
        join_message = 0;
    }
}

void gcomm::evs::Node::set_leave_message(const LeaveMessage* lm)
{
    if (leave_message != 0)
    {
        delete leave_message;
    }
    if (lm != 0)
    {
        leave_message = new LeaveMessage(*lm);
    }
    else
    {
        leave_message = 0;
    }
}



gcomm::evs::Proto::Proto(const UUID& my_uuid_, const string& conf) :
    timers(),
    debug_mask(D_STATE),
    info_mask(I_VIEWS | I_STATE | I_STATISTICS),
    collect_stats(true),
    hs_safe("0.0,0.0005,0.001,0.002,0.005,0.01,0.02,0.05,0.1,0.5,1.,5.,10.,30."),
    delivering(false),
    my_uuid(my_uuid_), 
    known(),
    self_i(),
    view_forget_timeout("PT5M"),
    inactive_timeout("PT3S"),
    inactive_check_period("PT1S"),
    consensus_timeout("PT5S"),
    retrans_period("PT1S"),
    join_retrans_period("PT0.3S"),
    current_view(ViewId(V_TRANS, my_uuid, 0)),
    previous_view(),
    previous_views(),
    input_map(new InputMap()),
    install_message(0),
    fifo_seq(-1),
    last_sent(Seqno::max()),
    send_window(8), 
    output(),
    max_output_size(16),
    self_loopback(false),
    state(S_CLOSED),
    shift_to_rfcnt(0)
{
    known.insert_checked(make_pair(my_uuid, Node()));
    self_i = known.begin();
    assert(NodeMap::get_value(self_i).get_operational() == true);
    
    input_map->insert_uuid(my_uuid);
    current_view.add_member(my_uuid, "");

    URI uri(conf);
    try
    {
        view_forget_timeout = 
            Period(uri.get_option(Conf::EvsParamViewForgetTimeout));
    } catch (NotFound&) { }
    try
    {
        inactive_timeout = 
            Period(uri.get_option(Conf::EvsParamInactiveTimeout));
    } catch (NotFound&) { }
    try
    {
        inactive_check_period = 
            Period(uri.get_option(Conf::EvsParamInactiveCheckPeriod));
    } catch (NotFound&) { }
    try
    {
        consensus_timeout = 
            Period(uri.get_option(Conf::EvsParamConsensusTimeout));
    } catch (NotFound&) { }
    try
    {
        retrans_period = Period(uri.get_option(Conf::EvsParamRetransPeriod));
    } catch (NotFound&) { }
    try
    {
        join_retrans_period = 
            Period(uri.get_option(Conf::EvsParamJoinRetransPeriod));
    } catch (NotFound&) { }

}


gcomm::evs::Proto::~Proto() 
{
    for (deque<pair<WriteBuf*, ProtoDownMeta> >::iterator i = output.begin(); i != output.end();
         ++i)
    {
        delete i->first;
    }
    output.clear();
    delete install_message;
    delete input_map;
}

ostream& gcomm::evs::operator<<(ostream& os, const Node& n)
{
    os << "evs::node{";
    os << "operational=" << n.get_operational() << ",";
    os << "installed=" << n.get_installed() << ",";
    os << "fifo_seq=" << n.get_fifo_seq() << ",";
    if (n.get_join_message() != 0)
    {
        os << "join_message=" << *n.get_join_message() << ",";
    }
    if (n.get_leave_message() != 0)
    {
        os << "leave_message=" << *n.get_leave_message() << ",";
    }
    os << "}";
    return os;
}



ostream& gcomm::evs::operator<<(ostream& os, const Proto& p)
{
    os << "evs::proto("
       << p.self_string() << ", " 
       << p.to_string(p.get_state()) << ") {";
    os << "current_view=" << p.current_view << ",";
    os << "input_map=" << *p.input_map << ",";
    os << "fifo_seq=" << p.fifo_seq << ",";
    os << "last_sent=" << p.last_sent << ",";
    os << "known={ " << p.known << " } ";
    os << " }";
    return os;
}



bool gcomm::evs::Proto::is_msg_from_previous_view(const Message& msg)
{
    for (list<pair<ViewId, Time> >::const_iterator i = previous_views.begin();
         i != previous_views.end(); ++i)
    {
        if (msg.get_source_view_id() == i->first)
        {
            evs_log_debug(D_FOREIGN_MSGS) << " message " << msg 
                                          << " from previous view " << i->first;
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
    if (get_state() == S_RECOVERY)
    {
        evs_log_debug(D_TIMERS) << "send join/install/gap timer";
        
        send_join(true);
        if (install_message != 0 && is_consistent(*install_message) == true)
        {
            if (is_representative(get_uuid()) == true)
            {
                gu_trace(send_install());
            }
            else
            {
                gu_trace(send_gap(UUID::nil(), 
                                  install_message->get_source_view_id(), 
                                  Range()));
            }
        }
    }
    else if (get_state() == S_OPERATIONAL)
    {
        evs_log_debug(D_TIMERS) << "send user timer";
        if (output.empty() == true)
        {
            WriteBuf wb(0, 0);
            gu_trace((void)send_user(&wb, 0xff, SP_DROP, send_window, 
                                     Seqno::max()));
        }
        else
        {
            gu_trace(send_user(send_window));
        }
    }
    else if (get_state() == S_LEAVING)
    {
        evs_log_debug(D_TIMERS) << "send leave timer";

        send_leave(false);
    }
}


void gcomm::evs::Proto::handle_consensus_timer()
{
    if (get_state() != S_OPERATIONAL)
    {
        log_warn << self_string() << " consensus timer expired";
        shift_to(S_RECOVERY, true);
    }
    if (get_state() != S_LEAVING)
    {
        for (NodeMap::iterator i = known.begin(); i != known.end(); ++i)
        {
            Node& node(NodeMap::get_value(i));
            if (node.get_leave_message() != 0 && is_inactive(node) == true)
            {
                log_warn << self_string() 
                         << " removing leave message of previously leaving node "
                         << NodeMap::get_key(i);
                node.set_leave_message(0);
            }
        }
    }
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

Time gcomm::evs::Proto::get_next_expiration(const Timer t) const
{
    gcomm_assert(get_state() != S_CLOSED);
    Time now(Time::now());
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
        case S_RECOVERY:
            return (now + join_retrans_period);
        default:
            gcomm_throw_fatal;
        }
    case T_CONSENSUS:
        switch (get_state()) 
        {
        case S_RECOVERY:
            return (now + consensus_timeout);
        default:
            return Time::max();
        }
    }
    gcomm_throw_fatal;
    return Time::max();
}


void gcomm::evs::Proto::reset_timers()
{
    timers.clear();
    gu_trace((void)timers.insert(
                 make_pair(get_next_expiration(T_INACTIVITY), T_INACTIVITY)));
    gu_trace((void)timers.insert(
                 make_pair(get_next_expiration(T_RETRANS), T_RETRANS)));
    gu_trace((void)timers.insert(
                 make_pair(get_next_expiration(T_CONSENSUS), T_CONSENSUS)));
}


Time gcomm::evs::Proto::handle_timers()
{
    Time now(Time::now());
    
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
        case T_CONSENSUS:
            handle_consensus_timer();
            break;
        }
        // Make sure that timer was not inserted twice
        TimerList::iterator ii = find_if(timers.begin(), timers.end(), 
                                         TimerSelectOp(t));
        if (ii != timers.end())
        {
            timers.erase(ii);
        }
        gu_trace((void)timers.insert(make_pair(get_next_expiration(t), t)));
    }
    
    if (timers.empty() == true)
    {
        evs_log_debug(D_TIMERS) << "no timers set";
        return Time::max();
    }
    return TimerList::get_key(timers.begin());
}


bool gcomm::evs::Proto::is_inactive(const Node& node) const
{
    return (node.get_tstamp() + inactive_timeout < Time::now());
}

void gcomm::evs::Proto::check_inactive()
{
    bool has_inactive = false;
    for (NodeMap::iterator i = known.begin(); i != known.end(); ++i)
    {
        const UUID& uuid(NodeMap::get_key(i));
        Node& node(NodeMap::get_value(i));
        if (uuid                   != get_uuid() &&
            node.get_operational() == true       &&
            is_inactive(node)      == true         )
        {
            log_warn << self_string() << " detected inactive node: " 
                     << uuid;
            node.set_operational(false);
            has_inactive = true;
        }
    }

    if (has_inactive == true && get_state() == S_OPERATIONAL)
    {
        shift_to(S_RECOVERY, true);
    }
}


void gcomm::evs::Proto::set_inactive(const UUID& uuid)
{
    NodeMap::iterator i;
    gu_trace(i = known.find_checked(uuid));
    evs_log_debug(D_STATE) << "setting " << uuid << " inactive";
    NodeMap::get_value(i).set_tstamp(Time::zero());
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
    Time now(Time::now());
    list<pair<ViewId, Time> >::iterator i = previous_views.begin();
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
        if (i->second.get_operational())
            ret++;
    }
    return ret;
}

void gcomm::evs::Proto::deliver_reg_view()
{
    if (install_message == 0)
    {
        gcomm_throw_fatal
            << "Protocol error: no install message in deliver reg view";
    }
    
    if (previous_views.size() == 0) gcomm_throw_fatal << "Zero-size view";
    
    const View& prev_view (previous_view);
    View view (install_message->get_source_view_id());
    
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
    pass_up(0, 0, up_meta);
}

void gcomm::evs::Proto::deliver_trans_view(bool local) 
{
    if (local == false && install_message == 0)
    {
        gcomm_throw_fatal
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
             inst.get_join_message()->get_source_view_id() == current_view.get_id()))
        {
            view.add_member(NodeMap::get_key(i), "");
        }
        else if (inst.get_installed() == false)
        {
            if (local == false)
            {
                const MessageNodeList& instances = install_message->get_node_list();
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
            }
            else
            {
                // Just assume others have partitioned, it does not matter
                // for leaving node anyway and it is not guaranteed if
                // the others get the leave message, so it is not safe
                // to assume then as left.
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
    gu_trace(pass_up(0, 0, up_meta));
}


void gcomm::evs::Proto::deliver_empty_view()
{
    View view(V_REG);

    evs_log_info(I_VIEWS) << "delivering view " << view;

    ProtoUpMeta up_meta(UUID::nil(), ViewId(), &view);
    pass_up(0, 0, up_meta);
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


bool gcomm::evs::Proto::is_consensus() const
{
    const JoinMessage* my_jm = 
        NodeMap::get_value(self_i).get_join_message();
    
    if (my_jm == 0) 
    {
        evs_log_debug(D_CONSENSUS) << "no own join message";
        return false;
    }
    
    if (is_consistent_same_view(*my_jm) == false) 
    {
        evs_log_debug(D_CONSENSUS) << "own join message not consistent";
        return false;
    }
    
    for (NodeMap::const_iterator i = known.begin(); i != known.end(); ++i)
    {
        const Node& inst(NodeMap::get_value(i));
        if (inst.get_operational() == false)
        {
            continue;
        }
        
        const JoinMessage* jm = inst.get_join_message();
        if (jm == 0)
        {
            evs_log_debug(D_CONSENSUS) 
                << "no join message for " << NodeMap::get_key(i);
            return false;
        }
        
        if (is_consistent(*jm) == false)
        {
            return false;
        }
    }
    if (install_message == 0)
    {
        evs_log_debug(D_CONSENSUS) << "consensus reached";
    }
    if (is_representative(get_uuid()) == true && install_message == 0)
    {
        evs_log_debug(D_CONSENSUS) << "consensus state " << *this;
    }
    return true;
}


bool gcomm::evs::Proto::is_representative(const UUID& uuid) const
{
    for (NodeMap::const_iterator i = known.begin(); i != known.end(); ++i) 
    {
        if (NodeMap::get_value(i).get_operational() == true && 
            is_inactive(NodeMap::get_value(i))      == false) 
        {
            gcomm_assert(NodeMap::get_value(i).get_leave_message() == 0);
            return (uuid == NodeMap::get_key(i));
        }
    }
    
    return false;
}


bool gcomm::evs::Proto::is_consistent_highest_reachable_safe_seq(
    const Message& msg) const
{
    gcomm_assert(msg.get_type() == Message::T_JOIN || 
                 msg.get_type() == Message::T_INSTALL);
    gcomm_assert(msg.get_source_view_id() == current_view.get_id());
    
    const MessageNodeList& node_list(msg.get_node_list());
    
    // Same view
    MessageNodeList same_view;
    for_each(node_list.begin(), node_list.end(),
             SelectNodesOp(same_view, current_view.get_id(), true, true));
    MessageNodeList::const_iterator max_hs_i(max_element(same_view.begin(), 
                                                         same_view.end(), 
                                                         RangeHsCmp()));
    // Max highest seen
    const Seqno max_hs(
        max_hs_i == same_view.end() ? 
        Seqno::max() : 
        MessageNodeList::get_value(max_hs_i).get_im_range().get_hs());
    
    Seqno max_reachable_safe_seq(max_hs);
    
    MessageNodeList leaving;
    for_each(node_list.begin(), node_list.end(), 
             SelectNodesOp(leaving, current_view.get_id(), false, true));
    
    if (leaving.empty() == false)
    {
        const MessageNodeList::const_iterator min_leave_seq_i(
            min_element(leaving.begin(), leaving.end(),
                        LeaveSeqCmpOp()));
        gcomm_assert(min_leave_seq_i != leaving.end());
        const Seqno min_leave_seq(
            MessageNodeList::get_value(min_leave_seq_i).get_leave_seq());
        gcomm_assert(min_leave_seq != Seqno::max());
        if (max_reachable_safe_seq == Seqno::max())
        {
            // We will always get at least this far
            max_reachable_safe_seq = min_leave_seq;
        }
        else
        {
            max_reachable_safe_seq = min(max_reachable_safe_seq, 
                                         min_leave_seq);
        }
    }
    
    MessageNodeList partitioning;
    for_each(node_list.begin(), node_list.end(), 
             SelectNodesOp(partitioning, current_view.get_id(), false, false));
    
    if (partitioning.empty() == false)
    {
        MessageNodeList::const_iterator min_part_safe_seq_i(
            min_element(partitioning.begin(), partitioning.end(),
                        SafeSeqCmp()));
        const Seqno min_part_safe_seq(
            min_part_safe_seq_i == partitioning.end() ?
            Seqno::max() : 
            MessageNodeList::get_value(min_part_safe_seq_i).get_safe_seq());
        
        if (min_part_safe_seq      != Seqno::max() &&
            max_reachable_safe_seq != Seqno::max())
        {
            max_reachable_safe_seq = min(max_reachable_safe_seq, 
                                         min_part_safe_seq);
        }
        else
        {
            max_reachable_safe_seq = Seqno::max();
        }
    }
    
    evs_log_debug(D_CONSENSUS)
        << " max reachable safe seq " << max_reachable_safe_seq
        << " highest reachable safe seq " << highest_reachable_safe_seq()
        << " max_hs " << max_hs 
        << " input map max hs " << input_map->get_max_hs()
        << " input map safe_seq " << input_map->get_safe_seq();
    
    return (input_map->get_max_hs()      == max_hs                 &&
            highest_reachable_safe_seq() == max_reachable_safe_seq &&
            input_map->get_safe_seq()    == max_reachable_safe_seq);
}

bool gcomm::evs::Proto::is_consistent_input_map(const Message& msg) const
{
    gcomm_assert(msg.get_type() == Message::T_JOIN || 
                 msg.get_type() == Message::T_INSTALL);
    gcomm_assert(msg.get_source_view_id() == current_view.get_id());
    
    
    if (msg.get_aru_seq() != input_map->get_aru_seq())
    {
        evs_log_debug(D_CONSENSUS) << "message aru seq "
                                   << msg.get_aru_seq()
                                   << " not consistent with input map aru seq "
                                   << input_map->get_aru_seq();
        return false;
    }
    
    if (msg.get_seq() != input_map->get_safe_seq())
    {
        evs_log_debug(D_CONSENSUS) << "message safe seq "
                                   << msg.get_seq()
                                   << " not consistent with input map safe seq "
                                   << input_map->get_safe_seq();
        return false;
    }
    
    Map<const UUID, Range> local_insts, msg_insts;
    
    for (NodeMap::const_iterator i = known.begin(); i != known.end(); ++i)
    {
        const UUID& uuid(NodeMap::get_key(i));
        if (current_view.get_members().find(uuid) != current_view.get_members().end())
        {
            gu_trace((void)local_insts.insert_checked(
                         make_pair(uuid, input_map->get_range(uuid))));
        }
    }
    
    const MessageNodeList& m_insts(msg.get_node_list());
    
    for (MessageNodeList::const_iterator i = m_insts.begin();
         i != m_insts.end(); ++i)
    {
        const UUID& msg_uuid(MessageNodeList::get_key(i));
        const MessageNode& msg_inst(MessageNodeList::get_value(i));
        if (msg_inst.get_view_id() == current_view.get_id())
        {
            gu_trace((void)msg_insts.insert_checked(
                         make_pair(msg_uuid, msg_inst.get_im_range())));
        }
    }
    
    evs_log_debug(D_CONSENSUS) << " msg_insts " << msg_insts
                               << " local_insts " << local_insts;
    
    return (msg_insts == local_insts);
}

bool gcomm::evs::Proto::is_consistent_partitioning(const Message& msg) const
{
    gcomm_assert(msg.get_type() == Message::T_JOIN ||
                 msg.get_type() == Message::T_INSTALL);
    gcomm_assert(msg.get_source_view_id() == current_view.get_id());
    
    // Compare instances that were present in the current view but are 
    // not proceeding in the next view.
    
    Map<const UUID, Range> local_insts, msg_insts;
    
    for (NodeMap::const_iterator i = known.begin(); i != known.end(); ++i)
    {
        const UUID& uuid(NodeMap::get_key(i));
        const Node& inst(NodeMap::get_value(i));
        if (inst.get_operational()                == false &&
            inst.get_leave_message()              == 0     &&
            current_view.get_members().find(uuid) != current_view.get_members().end())
        {
            gu_trace((void)local_insts.insert_checked(
                         make_pair(uuid, 
                                   input_map->get_range(uuid))));
        }
    }
    
    gcomm_assert(msg.has_node_list() == true);
    const MessageNodeList& m_insts = msg.get_node_list();
    
    for (MessageNodeList::const_iterator i = m_insts.begin(); 
         i != m_insts.end(); ++i)
    {
        const UUID& m_uuid(MessageNodeList::get_key(i));
        const MessageNode& m_inst(MessageNodeList::get_value(i));
        if (m_inst.get_operational() == false &&
            m_inst.get_leaving()     == false &&
            m_inst.get_view_id()     == current_view.get_id())
        {
            gu_trace((void)msg_insts.insert_checked(
                         make_pair(m_uuid, m_inst.get_im_range())));
        }
    }

    
    evs_log_debug(D_CONSENSUS) << " msg insts " << msg_insts
                               << " local insts " << local_insts;
    return (msg_insts == local_insts);
}

bool gcomm::evs::Proto::is_consistent_leaving(const Message& msg) const
{
    gcomm_assert(msg.get_type() == Message::T_JOIN ||
                 msg.get_type() == Message::T_INSTALL);
    gcomm_assert(msg.get_source_view_id() == current_view.get_id());
    
    // Compare instances that were present in the current view but are 
    // not proceeding in the next view.
    
    Map<const UUID, Range> local_insts, msg_insts;
    
    for (NodeMap::const_iterator i = known.begin(); i != known.end(); ++i)
    {
        const UUID& uuid(NodeMap::get_key(i));
        const Node& inst(NodeMap::get_value(i));
        const LeaveMessage* lm(inst.get_leave_message());
        
        if (inst.get_operational()   == false &&
            lm                       != 0  &&
            lm->get_source_view_id() == current_view.get_id())
        {
            gu_trace((void)local_insts.insert_checked(
                         make_pair(uuid, input_map->get_range(uuid))));
        }
    }
    
    const MessageNodeList& m_insts = msg.get_node_list();
    
    for (MessageNodeList::const_iterator i = m_insts.begin(); 
         i != m_insts.end(); ++i)
    {
        const UUID& m_uuid(MessageNodeList::get_key(i));
        const MessageNode& m_inst(MessageNodeList::get_value(i));
        if (m_inst.get_operational() == false &&
            m_inst.get_leaving()     == true &&
            m_inst.get_view_id()     == current_view.get_id())
        {
            gu_trace((void)msg_insts.insert_checked(
                         make_pair(m_uuid, m_inst.get_im_range())));
        }
    }

    evs_log_debug(D_CONSENSUS) << " msg insts " << msg_insts
                               << " local insts " << local_insts;    
    return (local_insts == msg_insts);
}

bool gcomm::evs::Proto::is_consistent_same_view(const Message& msg) const
{
    gcomm_assert(msg.get_type() == Message::T_JOIN ||
                 msg.get_type() == Message::T_INSTALL);
    gcomm_assert(msg.get_source_view_id() == current_view.get_id());
    
    if (is_consistent_highest_reachable_safe_seq(msg) == false)
    {
        evs_log_debug(D_CONSENSUS) 
            << "highest reachable safe seq not consistent";
        return false;
    }
    
    if (is_consistent_input_map(msg) == false)
    {
        evs_log_debug(D_CONSENSUS) << "input map not consistent ";
        return false;
    }
    
    if (is_consistent_partitioning(msg) == false)
    {
        evs_log_debug(D_CONSENSUS) << "partitioning not consistent";
        return false;
    }
    
    if (is_consistent_leaving(msg) == false)
    {
        evs_log_debug(D_CONSENSUS) << "leaving not consistent";
        return false;
    }
    
    return true;
}

bool gcomm::evs::Proto::is_consistent_joining(const Message& msg) const
{
    gcomm_assert(msg.get_type() == Message::T_JOIN ||
                 msg.get_type() == Message::T_INSTALL);
    gcomm_assert(current_view.get_id() != msg.get_source_view_id());
    
    Map<const UUID, Range> local_insts, msg_insts;
    for (NodeMap::const_iterator i = known.begin(); i != known.end(); ++i)
    {
        const UUID& uuid(NodeMap::get_key(i));
        const Node& inst(NodeMap::get_value(i));
        if (inst.get_operational() == false)
        {
            continue;
        }
        
        const JoinMessage* jm = inst.get_join_message();        
        if (jm == 0)
        {
            if (msg.get_source() == get_uuid())
            {
                evs_log_debug(D_CONSENSUS) 
                    << "own join not consistent with joining";
            }
            return false;
        }
        
        if (msg.get_source_view_id() == jm->get_source_view_id())
        { 
            if (msg.get_aru_seq() != jm->get_aru_seq())
            {
                if (msg.get_source() == get_uuid())
                {
                    evs_log_debug(D_CONSENSUS) << "own join not consistent with joining jm aru seq";
                }
                return false;
            }
            if (msg.get_seq() != jm->get_seq())
            {
                if (msg.get_source() == get_uuid())
                {
                    evs_log_debug(D_CONSENSUS) << "own join not consistent with joining jm seq";
                }
                return false;
            }
        }
        gu_trace((void)local_insts.insert_checked(make_pair(uuid, Range())));
    }
    
    assert(msg.has_node_list() == true);
    const MessageNodeList m_insts = msg.get_node_list();
    
    for (MessageNodeList::const_iterator mi = m_insts.begin();
         mi != m_insts.end(); ++mi)
    {
        const UUID& m_uuid(MessageNodeList::get_key(mi));
        const MessageNode& m_inst(MessageNodeList::get_value(mi));
        
        if (m_inst.get_operational() == true)
        {
            gu_trace((void)msg_insts.insert_checked(make_pair(m_uuid, Range())));
        }
    }
    
    evs_log_debug(D_CONSENSUS) << " msg insts " << msg_insts
                               << " local insts " << local_insts;    
    
    return (local_insts == msg_insts);
}


bool gcomm::evs::Proto::is_consistent(const Message& msg) const
{
    if (msg.get_source_view_id() == current_view.get_id())
    {
        return is_consistent_same_view(msg);
    }
    else
    {
        return is_consistent_joining(msg);
    }
}


/////////////////////////////////////////////////////////////////////////////
// Message sending
/////////////////////////////////////////////////////////////////////////////

template <class M>
void push_header(const M& msg, WriteBuf* wb)
{
    vector<byte_t> buf(msg.serial_size());
    msg.serialize(&buf[0], buf.size(), 0);
    wb->prepend_hdr(&buf[0], buf.size());
}

template <class M>
void pop_header(const M& msg, WriteBuf* wb)
{
    wb->rollback_hdr(msg.serial_size());
}


bool gcomm::evs::Proto::is_flow_control(const Seqno seq, const Seqno win) const
{
    gcomm_assert(seq != Seqno::max() && win != Seqno::max());
    
    const Seqno base(input_map->get_aru_seq() == Seqno::max() ? 0 : 
                     input_map->get_aru_seq());
    if (seq > base + win)
    {
        return true;
    }
    return false;
}

int gcomm::evs::Proto::send_user(WriteBuf* wb, 
                                 const uint8_t user_type,
                                 const SafetyPrefix sp, 
                                 const Seqno win,
                                 const Seqno up_to_seqno,
                                 bool local)
{
    assert(get_state() == S_LEAVING || 
           get_state() == S_RECOVERY || 
           get_state() == S_OPERATIONAL);
    gcomm_assert(up_to_seqno == Seqno::max() || 
                 last_sent   == Seqno::max() ||
                 up_to_seqno >= last_sent);
    gcomm_assert(up_to_seqno == Seqno::max() || win == Seqno::max());
    int ret;
    const Seqno seq(last_sent == Seqno::max() ? 0 : last_sent + 1);
    
    if (local                     == false         && 
        win                       != Seqno::max()  &&
        is_flow_control(seq, win) == true)
    {
        return EAGAIN;
    }
    
    const Seqno seq_range(up_to_seqno == Seqno::max() ? 0 : up_to_seqno - seq);
    gcomm_assert(seq_range <= Seqno(0xff));
    const Seqno last_msg_seq(seq + seq_range);
    uint8_t flags;
    
    if (output.size() < 2 || 
        up_to_seqno != Seqno::max() ||
        (win != Seqno::max() &&
         is_flow_control(last_msg_seq + 1, win) == true))
    {
        flags = 0;
    }
    else 
    {
        flags = Message::F_MSG_MORE;
    }
    
    UserMessage msg(get_uuid(),
                    current_view.get_id(), 
                    seq,
                    input_map->get_aru_seq(),
                    seq_range,
                    sp, 
                    ++fifo_seq,
                    user_type,
                    flags);
    
    // Insert first to input map to determine correct aru seq
    ReadBuf* rb = wb->to_readbuf();
    Range range;
    gu_trace(range = input_map->insert(get_uuid(), msg, rb, 0));
    rb->release();
    
    gcomm_assert(range.get_hs() == last_msg_seq) 
        << msg << " " << *input_map << " " << *this;
    
    last_sent = last_msg_seq;
    assert(range.get_hs() == last_sent);
    
    update_im_safe_seq(get_uuid(), input_map->get_aru_seq());
    
    if (local == false)
    {
        // Rewrite message hdr to include correct aru
        msg.set_aru_seq(input_map->get_aru_seq());
        push_header(msg, wb);
        if ((ret = pass_down(wb, 0)) != 0)
        {
            log_warn << "send failed: "  << strerror(ret);
        }
        pop_header(msg, wb);
    }
    
    if (delivering == false)
    {
        gu_trace(deliver());
    }
    return 0;
}

int gcomm::evs::Proto::send_user(const Seqno win)
{
    gcomm_assert(output.empty() == false);
    gcomm_assert(get_state() == S_OPERATIONAL);
    pair<WriteBuf*, ProtoDownMeta> wb = output.front();
    int ret;
    if ((ret = send_user(wb.first, 
                         wb.second.get_user_type(), 
                         wb.second.get_safety_prefix(), 
                         win, 
                         Seqno::max())) == 0) 
    {
        output.pop_front();
        delete wb.first;
    }
    return ret;
}


void gcomm::evs::Proto::complete_user(const Seqno high_seq)
{
    gcomm_assert(get_state() == S_OPERATIONAL || get_state() == S_RECOVERY);
    
    evs_log_debug(D_USER_MSGS) << "completing seqno to " << high_seq;;

    WriteBuf wb(0, 0);
    int err = send_user(&wb, 0xff, SP_DROP, Seqno::max(), high_seq);
    if (err != 0)
    {
        log_warn << "failed to send completing msg " << strerror(err) 
                 << " seq=" << high_seq << " send_window=" << send_window
                 << " last_sent=" << last_sent;
    }
}


int gcomm::evs::Proto::send_delegate(WriteBuf* wb)
{
    DelegateMessage dm(get_uuid(), current_view.get_id(), ++fifo_seq);
    push_header(dm, wb);
    int ret = pass_down(wb, 0);
    pop_header(dm, wb);
    return ret;
}


void gcomm::evs::Proto::send_gap(const UUID&   range_uuid, 
                                 const ViewId& source_view_id, 
                                 const Range   range)
{
    evs_log_debug(D_GAP_MSGS) << "sending gap  to "  
                              << range_uuid 
                              << " requesting range " << range;
    // TODO: Investigate if gap sending can be somehow limited, 
    // message loss happen most probably during congestion and 
    // flooding network with gap messages won't probably make 
    // conditions better
    
    GapMessage gm(get_uuid(),
                  source_view_id,
                  (source_view_id == current_view.get_id() ? last_sent :
                   Seqno::max()), 
                  (source_view_id == current_view.get_id() ? 
                   input_map->get_aru_seq() : Seqno::max()), 
                  ++fifo_seq,
                  range_uuid, 
                  range);
    
    WriteBuf wb(0, 0);
    push_header(gm, &wb);
    int err = pass_down(&wb, 0);
    if (err != 0)
    {
        log_warn << "send failed: " << strerror(err);
    }
    gu_trace(handle_gap(gm, self_i));
}

void gcomm::evs::Proto::populate_node_list(MessageNodeList* node_list) const
{
    for (NodeMap::const_iterator i = known.begin(); i != known.end(); ++i)
    {
        const UUID& uuid(NodeMap::get_key(i));
        const Node& node(NodeMap::get_value(i));
        const bool in_current(current_view.get_members().find(uuid) != 
                              current_view.get_members().end());
        const ViewId vid(node.get_join_message() != 0 ?
                         node.get_join_message()->get_source_view_id() :
                         (in_current == true ? 
                          current_view.get_id() : 
                          ViewId(V_REG)));
        const Seqno safe_seq(in_current == true ? input_map->get_safe_seq(uuid) : Seqno::max());
        const Range range(in_current == true         ? 
                          input_map->get_range(uuid) : 
                          Range());
        const MessageNode mnode(node.get_operational(),
                                (node.get_leave_message() != 0 ? 
                                 node.get_leave_message()->get_seq() :
                                 Seqno::max()),
                                vid, 
                                safe_seq, 
                                range);
        gu_trace((void)node_list->insert_checked(make_pair(uuid, mnode)));
    }
}

const JoinMessage& gcomm::evs::Proto::create_join()
{
    
    MessageNodeList node_list;
    
    gu_trace(populate_node_list(&node_list));
    JoinMessage jm(get_uuid(),
                   current_view.get_id(),
                   input_map->get_safe_seq(),
                   input_map->get_aru_seq(),
                   ++fifo_seq,
                   &node_list);
    NodeMap::get_value(self_i).set_join_message(&jm);
    
    evs_log_debug(D_JOIN_MSGS) << " created join message " << jm;
    gcomm_assert(is_consistent_input_map    (jm) == true &&
                 is_consistent_partitioning (jm) == true &&
                 is_consistent_leaving      (jm) == true)
        << "inconsistent JOIN message " << jm << " local state " << *this;
    
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
    vector<byte_t> buf(jm.serial_size());
    gu_trace((void)jm.serialize(&buf[0], buf.size(), 0));
    WriteBuf wb(&buf[0], buf.size());
    
    int err = pass_down(&wb, 0);
    if (err != 0) 
    {
        log_warn << "send failed: " << strerror(err);
    }
    
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
    if (last_sent == Seqno::max() && output.empty() == true)
    {
        WriteBuf wb(0, 0);
        gu_trace(send_user(&wb, 0xff, SP_DROP, Seqno::max(), Seqno::max()));
    }
    
    /* Move all pending messages from output to input map */
    while (output.empty() == false)
    {
        pair<WriteBuf*, ProtoDownMeta> wb = output.front();
        if (send_user(wb.first, 
                      wb.second.get_user_type(), 
                      wb.second.get_safety_prefix(), 
                      Seqno::max(), Seqno::max()) != 0)
        {
            gcomm_throw_fatal << "send_user() failed";
        }
        
        output.pop_front();
        delete wb.first;
    }
    

    
    LeaveMessage lm(get_uuid(),
                    current_view.get_id(),
                    last_sent,
                    input_map->get_aru_seq(), 
                    ++fifo_seq);

    evs_log_debug(D_LEAVE_MSGS) << "sending leave msg " << lm;
    
    WriteBuf wb(0, 0);
    push_header(lm, &wb);
    
    int err = pass_down(&wb, 0);
    if (err != 0)
    {
        log_warn << "send failed " << strerror(err);
    }
    
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
    gcomm_assert(is_consensus() == true && 
                 is_representative(get_uuid()) == true);
    
    NodeMap oper_list;
    for_each(known.begin(), known.end(), OperationalSelect(oper_list));
    NodeMap::const_iterator max_node = 
        max_element(oper_list.begin(), oper_list.end(), ViewIdCmp());
    
    const uint32_t max_view_id_seq = 
        NodeMap::get_value(max_node).get_join_message()->get_source_view_id().get_seq();
    
    MessageNodeList node_list;
    populate_node_list(&node_list);
    
    InstallMessage imsg(get_uuid(),
                        ViewId(V_REG, get_uuid(), max_view_id_seq + 1),
                        input_map->get_safe_seq(),
                        input_map->get_aru_seq(),
                        ++fifo_seq,
                        &node_list);
    
    evs_log_debug(D_INSTALL_MSGS) << "sending install " << imsg;
    
    vector<byte_t> buf(imsg.serial_size());
    gu_trace((void)imsg.serialize(&buf[0], buf.size(), 0));
    WriteBuf wb (&buf[0], buf.size());
    
    int err = pass_down(&wb, 0);;
    if (err != 0) 
    {
        log_warn << "send failed: " << strerror(err);
    }
    
    handle_install(imsg, self_i);
}


void gcomm::evs::Proto::resend(const UUID& gap_source, const Range range)
{
    gcomm_assert(gap_source != get_uuid());
    gcomm_assert(range.get_lu() != Seqno::max() && 
                 range.get_hs() != Seqno::max());
    gcomm_assert(range.get_lu() <= range.get_hs()) << 
        "lu (" << range.get_lu() << ") > hs(" << range.get_hs() << ")"; 
    
    if (input_map->get_safe_seq() != Seqno::max() &&
        range.get_lu() <= input_map->get_safe_seq())
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
    
    Seqno seq(range.get_lu()); 
    while (seq <= range.get_hs())
    {
        InputMap::iterator msg_i = input_map->find(get_uuid(), seq);
        if (msg_i == input_map->end())
        {
            gu_trace(msg_i = input_map->recover(get_uuid(), seq));
        }
        
        const UserMessage& msg(InputMapMsgIndex::get_value(msg_i).get_msg());
        gcomm_assert(msg.get_source() == get_uuid());
        const ReadBuf* rb(InputMapMsgIndex::get_value(msg_i).get_rb());
        UserMessage um(msg.get_source(),
                       msg.get_source_view_id(),
                       msg.get_seq(),
                       input_map->get_aru_seq(),
                       msg.get_seq_range(),
                       msg.get_safety_prefix(),
                       msg.get_fifo_seq(),
                       msg.get_user_type(),
                       Message::F_RETRANS);
        
        WriteBuf wb(rb != 0 ? rb->get_buf() : 0, rb != 0 ? rb->get_len() : 0);
        push_header(um, &wb);
        
        int err = pass_down(&wb, 0);
        if (err != 0)
        {
            log_warn << "send failed: " << strerror(err);
            break;
        }
        else
        {
            evs_log_debug(D_RETRANS) << "retransmitted " << um;
        }
        seq = seq + msg.get_seq_range() + 1;
    }
}


void gcomm::evs::Proto::recover(const UUID& gap_source, 
                                const UUID& range_uuid,
                                const Range range)
{
    gcomm_assert(gap_source != get_uuid())
        << "gap_source (" << gap_source << ") == get_uuid() (" << get_uuid()
        << " state " << *this;
    gcomm_assert(range.get_lu() != Seqno::max() && range.get_hs() != Seqno::max())
        << "lu (" << range.get_lu() << ") hs (" << range.get_hs() << ")"; 
    gcomm_assert(range.get_lu() <= range.get_hs()) 
        << "lu (" << range.get_lu() << ") > hs (" << range.get_hs() << ")"; 
    
    if (input_map->get_safe_seq() != Seqno::max() &&
        range.get_lu() <= input_map->get_safe_seq())
    {
        log_warn << self_string() << "lu (" << range.get_lu() <<
            ") <= safe_seq(" << input_map->get_safe_seq() 
                 << "), can't recover message";
        return;
    }
    
    const Range im_range(input_map->get_range(range_uuid));
    
    evs_log_debug(D_RETRANS) << " recovering message from "
                             << range_uuid
                             << " requested by " 
                             << gap_source 
                             << " requested range " << range
                             << " available " << im_range;
    
    
    Seqno seq(range.get_lu()); 
    while (seq <= range.get_hs() && seq <= im_range.get_hs())
    {
        InputMap::iterator msg_i = input_map->find(range_uuid, seq);
        if (msg_i == input_map->end())
        {
            try
            {
                gu_trace(msg_i = input_map->recover(range_uuid, seq));
            }
            catch (...)
            {
                seq = seq + 1;
                continue;
            }
        }
        
        const UserMessage& msg(InputMapMsgIndex::get_value(msg_i).get_msg());
        assert(msg.get_source() == range_uuid);
        const ReadBuf* rb(InputMapMsgIndex::get_value(msg_i).get_rb());
        UserMessage um(msg.get_source(),
                       msg.get_source_view_id(),
                       msg.get_seq(),
                       msg.get_aru_seq(),
                       msg.get_seq_range(),
                       msg.get_safety_prefix(),
                       msg.get_fifo_seq(),
                       msg.get_user_type(),
                       Message::F_SOURCE | Message::F_RETRANS);
        
        WriteBuf wb(rb != 0 ? rb->get_buf() : 0, rb != 0 ? rb->get_len() : 0);
        push_header(um, &wb);
        
        int err = send_delegate(&wb);
        if (err != 0)
        {
            log_warn << "send failed: " << strerror(err);
            break;
        }
        seq = seq + msg.get_seq_range() + 1;
    }
}


void gcomm::evs::Proto::handle_foreign(const Message& msg)
{
    if (msg.get_type() == Message::T_LEAVE)
    {
        // No need to handle foreign LEAVE message
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
    gu_trace(i = known.insert_checked(make_pair(source, Node())));
    assert(NodeMap::get_value(i).get_operational() == true);
    
    if (get_state() == S_JOINING || get_state() == S_RECOVERY || 
        get_state() == S_OPERATIONAL)
    {
        evs_log_debug(D_STATE) << " shift to RECOVERY due to foreign message";
        gu_trace(shift_to(S_RECOVERY, true));
    }
    
    // Set join message after shift to recovery, shift may clean up
    // join messages
    if (msg.get_type() == Message::T_JOIN)
    {
        set_join(static_cast<const JoinMessage&>(msg), msg.get_source());
    }
}

void gcomm::evs::Proto::handle_msg(const Message& msg, 
                                   const ReadBuf* rb,
                                   const size_t roff)
{
    if (get_state() == S_CLOSED)
    {
        return;
    }

    if (msg.get_source() == get_uuid())
    {
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
    
    // Filter out non-fifo messages
    if (msg.get_fifo_seq() != -1 && (msg.get_flags() & Message::F_RETRANS) == 0)
    {
        Node& node(NodeMap::get_value(ii));
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
    if (msg.is_membership() == false)
    {
        if (msg.get_source_view_id() != current_view.get_id())
        {
            if (install_message == 0 ||
                install_message->get_source_view_id() != msg.get_source_view_id())
            {
                return;
            }
        }
    }
    
    switch (msg.get_type()) {
    case Message::T_USER:
        gu_trace(handle_user(static_cast<const UserMessage&>(msg), ii, rb, roff));
        break;
    case Message::T_DELEGATE:
        gu_trace(handle_delegate(static_cast<const DelegateMessage&>(msg), ii, rb, roff));
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
                                              const ReadBuf* const rb, 
                                              size_t offset,
                                              Message* msg)
{


    gu_trace(offset = msg->unserialize(rb->get_buf(), rb->get_len(), offset));
    if ((msg->get_flags() & Message::F_SOURCE) == false)
    {
        gcomm_assert(source != UUID::nil());
        msg->set_source(source);
    }
    
    switch (msg->get_type())
    {
    case Message::T_NONE:
        gcomm_throw_fatal;
        break;
    case Message::T_USER:
        gu_trace(offset = static_cast<UserMessage&>(*msg).unserialize(
                     rb->get_buf(), rb->get_len(), offset, true));
        break;
    case Message::T_DELEGATE:
        gu_trace(offset = static_cast<DelegateMessage&>(*msg).unserialize(
                     rb->get_buf(), rb->get_len(), offset, true));
        break;
    case Message::T_GAP:
        gu_trace(offset = static_cast<GapMessage&>(*msg).unserialize(
                     rb->get_buf(), rb->get_len(), offset, true));
        break;
    case Message::T_JOIN:
        gu_trace(offset = static_cast<JoinMessage&>(*msg).unserialize(
                     rb->get_buf(), rb->get_len(), offset, true));
        break;
    case Message::T_INSTALL:
        gu_trace(offset = static_cast<InstallMessage&>(*msg).unserialize(
                     rb->get_buf(), rb->get_len(), offset, true));
        break;
    case Message::T_LEAVE:
        gu_trace(offset = static_cast<LeaveMessage&>(*msg).unserialize(
                     rb->get_buf(), rb->get_len(), offset, true));
        break;
    }
    return offset;
}

void gcomm::evs::Proto::handle_up(int cid, 
                                  const ReadBuf* rb, 
                                  size_t offset,
                                  const ProtoUpMeta& um)
{
    
    Message msg;
    
    if (rb == 0)
    {
        gcomm_throw_fatal << "Invalid input: rb == 0";
    }
    
    if (get_state() == S_CLOSED || um.get_source() == get_uuid())
    {
        // Silent drop
        return;
    }
    
    gcomm_assert(um.get_source() != UUID::nil());    
    
    try
    {
        gu_trace(offset = unserialize_message(um.get_source(), rb, offset, &msg));
        handle_msg(msg, rb, offset);
    }
    catch (...)
    {
        log_fatal << "exception caused by message: " << msg;
        log_fatal << " state after handling message: " << *this;
        throw;
    }
}

int gcomm::evs::Proto::handle_down(WriteBuf* wb, const ProtoDownMeta& dm)
{
    
    if (get_state() == S_RECOVERY)
    {
        return EAGAIN;
    }
    
    else if (get_state() != S_OPERATIONAL)
    {
        log_warn << "user message in state " << to_string(get_state());
        return ENOTCONN;
    }
    
    if (dm.get_user_type() == 0xff)
    {
        return EINVAL;
    }
    
    int ret = 0;
    
    if (output.empty() == true) 
    {
        int err = send_user(wb, 
                            dm.get_user_type(),
                            dm.get_safety_prefix(), send_window.get()/2, 
                            Seqno::max());
        switch (err) 
        {
        case EAGAIN:
        {
            WriteBuf* priv_wb = wb->copy();
            output.push_back(make_pair(priv_wb, dm));
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
        WriteBuf* priv_wb = wb->copy();
        output.push_back(make_pair(priv_wb, dm));
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
    if (shift_to_rfcnt > 0) gcomm_throw_fatal;

    shift_to_rfcnt++;

    static const bool allowed[S_MAX][S_MAX] = {
        // CLOSED JOINING LEAVING RECOV  OPERAT
        {  false,  true,   false, false, false }, // CLOSED
        
        {  false,  false,  true,  true,  false }, // JOINING

        {  true,   false,  false, false, false }, // LEAVING

        {  false,  false,  true,  true,  true  }, // RECOVERY

        {  false,  false,  true,  true,  false }  // OPERATIONAL
    };
    
    assert(s < S_MAX);
    
    if (allowed[state][s] == false) {
        gcomm_throw_fatal << "Forbidden state transition: " 
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
        setall_installed(false);
        NodeMap::get_value(self_i).set_installed(true);
        gu_trace(deliver_trans_view(true));
        gu_trace(deliver_trans());
        gu_trace(deliver_empty_view());
        if (collect_stats == true)
        {
            evs_log_info(I_STATISTICS) << "delivery stats (safe): " << hs_safe;
        }
        hs_safe.clear();
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
    case S_RECOVERY:
    {
        if (get_state() != S_RECOVERY)
        {
            cleanup_joins();
        }
        setall_installed(false);
        delete install_message;
        install_message = 0;
        
        if (get_state() == S_OPERATIONAL)
        {
            while (output.empty() == false)
            {
                int err;
                gu_trace(err = send_user(Seqno::max()));
                if (err != 0)
                {
                    gcomm_throw_fatal << self_string() 
                                      << "send_user() failed in shifto "
                                      << "to S_RECOVERY: "
                                      << strerror(err);
                }
            }
        }
        else
        {
            gcomm_assert(output.empty() == true);
        }
        
        state = S_RECOVERY;
        if (send_j == true)
        {
            gu_trace(send_join(false));
        }
        gcomm_assert(get_state() == S_RECOVERY);
        reset_timers();        
        break;
    }
    case S_OPERATIONAL:
    {
        gcomm_assert(output.empty() == true);
        gcomm_assert(install_message != 0 && 
                     is_consistent(*install_message) == true);
        gcomm_assert(is_all_installed() == true);
        gu_trace(deliver());
        gu_trace(deliver_trans_view(false));
        gu_trace(deliver_trans());
        input_map->clear();
        
        previous_view = current_view;
        previous_views.push_back(make_pair(current_view.get_id(), Time::now()));
        
        gcomm_assert(install_message->has_node_list() == true);
        const MessageNodeList& imap = install_message->get_node_list();
        
        for (MessageNodeList::const_iterator i = imap.begin();
             i != imap.end(); ++i)
        {
            previous_views.push_back(make_pair(MessageNodeList::get_value(i).get_view_id(), 
                                               Time::now()));
        }
        current_view = View(install_message->get_source_view_id());
        for (NodeMap::const_iterator i = known.begin(); i != known.end(); ++i)
        {
            if (NodeMap::get_value(i).get_installed() == true)
            {
                gu_trace(current_view.add_member(NodeMap::get_key(i), ""));
                gu_trace(input_map->insert_uuid(NodeMap::get_key(i)));
            }
        }
        
        last_sent = Seqno::max();
        state = S_OPERATIONAL;
        deliver_reg_view();
        if (collect_stats == true)
        {
            evs_log_info(I_STATISTICS) << "delivery stats (safe): " << hs_safe;
        }
        hs_safe.clear();
        cleanup_unoperational();
        cleanup_views();
        for (NodeMap::iterator i = known.begin(); i != known.end(); ++i)
        {
            NodeMap::get_value(i).set_join_message(0);
        }
        delete install_message;
        install_message = 0;
        gu_trace(send_gap(UUID::nil(), current_view.get_id(), Range()));;
        gcomm_assert(get_state() == S_OPERATIONAL);
        reset_timers();
        break;
    }
    default:
        gcomm_throw_fatal << "invalid state";
    }
    shift_to_rfcnt--;
}

////////////////////////////////////////////////////////////////////////////
// Message delivery
////////////////////////////////////////////////////////////////////////////

void gcomm::evs::Proto::validate_reg_msg(const UserMessage& msg)
{
    if (msg.get_source_view_id() != current_view.get_id())
    {
        gcomm_throw_fatal << "reg validate: not current view";
    }

    if (collect_stats == true && msg.get_safety_prefix() == SP_SAFE)
    {
        Time now(Time::now());
        hs_safe.insert(double(now.get_utc() - msg.get_tstamp().get_utc())/gu::datetime::Sec);
    }
}

void gcomm::evs::Proto::deliver()
{
    if (delivering == true)
    {
        gcomm_throw_fatal << "Recursive enter to delivery";
    }
    
    delivering = true;
    
    if (get_state() != S_OPERATIONAL && get_state() != S_RECOVERY && 
        get_state() != S_LEAVING)
    {
        gcomm_throw_fatal << "invalid state: " << to_string(get_state());
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
        gu_trace(validate_reg_msg(msg.get_msg()));
        bool deliver = false;
        switch (msg.get_msg().get_safety_prefix())
        {
        case SP_DROP:
            deliver = true;
            break;
            
        case SP_SAFE:
            if (input_map->is_safe(i) == true)
            {
                deliver = true;
            }
            break;
        case SP_AGREED:
            if (input_map->is_agreed(i) == true)
            {
                deliver = true;
            }
            break;
        case SP_FIFO:
            if (input_map->is_fifo(i) == true)
            {
                deliver = true;
            }
            break;
        default:
            gcomm_throw_fatal << "invalid safety prefix " 
                              << msg.get_msg().get_safety_prefix();
        }
        
        if (deliver == true)
        {
            if (msg.get_msg().get_safety_prefix() != SP_DROP)
            {
                ProtoUpMeta um(msg.get_uuid(), 
                               msg.get_msg().get_source_view_id(),
                               0,
                               msg.get_msg().get_user_type(),
                               msg.get_msg().get_seq().get());
                gu_trace(pass_up(msg.get_rb(), 0, um));
            }
            gu_trace(input_map->erase(i));
        }
    }
    delivering = false;

}

void gcomm::evs::Proto::validate_trans_msg(const UserMessage& msg)
{
    if (msg.get_source_view_id() != current_view.get_id())
    {
        // @todo: do we have to freak out here?
        // @note: yes, this evs implementation should guarantee 
        //        same view delivery
        gcomm_throw_fatal << "reg validate: not current view";
    }
    
    if (collect_stats && msg.get_safety_prefix() == SP_SAFE)
    {
        Time now(Time::now());
        hs_safe.insert(double(now.get_utc() - msg.get_tstamp().get_utc())/gu::datetime::Sec);
    }
}


void gcomm::evs::Proto::deliver_trans()
{
    if (delivering == true)
    {
        gcomm_throw_fatal << "Recursive enter to delivery";
    }
    
    delivering = true;
    
    if (get_state() != S_RECOVERY && get_state() != S_LEAVING)
        gcomm_throw_fatal << "invalid state";
    
    evs_log_debug(D_DELIVERY)
        << " aru_seq="   << input_map->get_aru_seq() 
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
        gu_trace(validate_reg_msg(msg.get_msg()));
        bool deliver = false;
        switch (msg.get_msg().get_safety_prefix())
        {
        case SP_DROP:
            deliver = true;
            break;
        case SP_SAFE:
        case SP_AGREED:
        case SP_FIFO:
            if (input_map->is_fifo(i) == true)
            {
                deliver = true;
            }
            break;
        default:
            gcomm_throw_fatal;
        }
        
        if (deliver == true)
        {
            if (msg.get_msg().get_safety_prefix() != SP_DROP)
            {
                ProtoUpMeta um(msg.get_uuid(), 
                               msg.get_msg().get_source_view_id(),
                               0,
                               msg.get_msg().get_user_type(),
                               msg.get_msg().get_seq().get());
                gu_trace(pass_up(msg.get_rb(), 0, um));
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
        gu_trace(ii = known.find_checked(msg.get_uuid()));
        
        if (NodeMap::get_value(ii).get_installed() == true)
        {
            gcomm_throw_fatal << "Protocol error in transitional delivery "
                              << "(self delivery constraint)";
        }
        else if (input_map->is_fifo(i) == true)
        {
            gcomm_throw_fatal << "Protocol error in transitional delivery "
                              << "(fifo from partitioned component)";
        }
        gu_trace(input_map->erase(i));
    }
    delivering = false;
}


/////////////////////////////////////////////////////////////////////////////
// Message handlers
/////////////////////////////////////////////////////////////////////////////

gcomm::evs::Seqno gcomm::evs::Proto::highest_reachable_safe_seq() const
{
    list<Seqno> seq_list;
    for (NodeMap::const_iterator i = known.begin(); i != known.end();
         ++i)
    {
        if (current_view.get_members().find(NodeMap::get_key(i)) != 
            current_view.get_members().end())
        {
            const UUID& uuid(NodeMap::get_key(i));
            const Node& node(NodeMap::get_value(i));
            if (is_inactive(node) == true)
            {
                const Seqno max_reachable_safe_seq(
                    input_map->get_safe_seq(uuid));
                if (max_reachable_safe_seq != Seqno::max())
                {
                    seq_list.push_back(max_reachable_safe_seq);
                }
                else
                {
                    return Seqno::max();
                }
            }
            else
            {
                const Seqno im_hs(input_map->get_range(uuid).get_hs());
                if (im_hs != Seqno::max())
                {
                    seq_list.push_back(im_hs);
                }
                else
                {
                    return Seqno::max();
                }
            }
        }
    }
    gcomm_assert(seq_list.empty() == false);
    return *min_element(seq_list.begin(), seq_list.end());
}


gcomm::evs::Seqno gcomm::evs::Proto::update_im_safe_seq(const UUID& uuid, 
                                                        const Seqno seq)
{
    const Seqno im_safe_seq(input_map->get_safe_seq(uuid));
    if (seq          != Seqno::max()    &&
        (im_safe_seq == Seqno::max() ||
         im_safe_seq  < seq            )  )
    {
        input_map->set_safe_seq(uuid, seq);
    }
    return im_safe_seq;
}


void gcomm::evs::Proto::handle_user(const UserMessage& msg, 
                                    NodeMap::iterator ii, 
                                    const ReadBuf* rb, 
                                    const size_t roff)
{
    assert(ii != known.end());
    assert(get_state() != S_CLOSED && get_state() != S_JOINING);
    Node& inst(NodeMap::get_value(ii));
    
    
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
            // This is probably partition merge, see if it works out
            evs_log_debug(D_STATE) << " unoperational message source " 
                                   << msg.get_source()
                                   << " got operational";
            inst.set_operational(true);
            shift_to(S_RECOVERY);
            return;
        } 
        else if (inst.get_installed() == false) 
        {
            if (install_message != 0 && 
                msg.get_source_view_id() == install_message->get_source_view_id()) 
            {
                assert(state == S_RECOVERY);
                gcomm_assert(install_message->has_node_list() == true);
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
                inst.set_tstamp(Time::now());                
                if (is_consensus() == true) 
                {
                    shift_to(S_OPERATIONAL);
                } 
                else 
                {
                    shift_to(S_RECOVERY);
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
            log_warn << self_string() << " unhandled user message " << msg;
            return;
        }
    }
    
    gcomm_assert(msg.get_source_view_id() == current_view.get_id());
    
    const Seqno prev_aru(input_map->get_aru_seq());
    const Range prev_range(input_map->get_range(msg.get_source()));
    Range range;
    
    // Insert only if msg seq is greater or equal than current lowest unseen
    if (msg.get_seq() >= prev_range.get_lu())
    {
        gu_trace(range = input_map->insert(msg.get_source(), msg, rb, roff));
        if (range.get_lu() > prev_range.get_lu())
        {
            inst.set_tstamp(Time::now());
        }
    }
    else
    {
        range = prev_range;
    }
    
    update_im_safe_seq(get_uuid(), input_map->get_aru_seq());
    
    // Update safe seq for message source
    const Seqno prev_safe(update_im_safe_seq(msg.get_source(), 
                                             msg.get_aru_seq()));
    
    // Check for missing messages
    if (range.get_hs()                         >  range.get_lu() && 
        (msg.get_flags() & Message::F_RETRANS) == 0                 )
    {
        evs_log_debug(D_RETRANS) << " requesting retrans from " 
                                 << msg.get_source() << " "
                                 << range 
                                 << " due to input map gap, aru " 
                                 << input_map->get_aru_seq();
        gu_trace(send_gap(msg.get_source(), current_view.get_id(), range));
    }
    
    // Seqno range completion and acknowledgement
    if (output.empty()                          == true            && 
        get_state()                             != S_LEAVING       && 
        (msg.get_flags() & Message::F_MSG_MORE) == 0               &&
        (last_sent                              == Seqno::max() || 
         last_sent                              <  range.get_hs()))
    {
        // Message not originated from this instance, output queue is empty
        // and last_sent seqno should be advanced
        gu_trace(complete_user(range.get_hs()));
    }
    else if (output.empty()           == true  && 
             input_map->get_aru_seq() != prev_aru)
    {
        // Output queue empty and aru changed, send gap to inform others
        evs_log_debug(D_GAP_MSGS) << "sending empty gap";
        gu_trace(send_gap(UUID::nil(), current_view.get_id(), Range()));
    }
    
    // Deliver messages
    gu_trace(deliver());
    
    // Send messages
    if (get_state() == S_OPERATIONAL)
    {
        while (output.empty() == false)
        {
            int err;
            gu_trace(err = send_user(send_window));
            if (err != 0)
            {
                break;
            }
        }
    }
    
    // If in recovery state, send join each time input map aru seq reaches
    // last sent and either input map aru or safe seq has changed.
    if (get_state()                  == S_RECOVERY && 
        highest_reachable_safe_seq() == input_map->get_aru_seq() && 
        (prev_aru                    != input_map->get_aru_seq() ||
         prev_safe                   != input_map->get_safe_seq()))
    {
        gcomm_assert(output.empty() == true);
        const JoinMessage* jm = NodeMap::get_value(self_i).get_join_message();
        if (jm == 0 || is_consistent(*jm) == false)
        {
            gu_trace(send_join());
        }
    }
}


void gcomm::evs::Proto::handle_delegate(const DelegateMessage& msg, 
                                        NodeMap::iterator ii,
                                        const ReadBuf* rb, 
                                        size_t offset)
{
    gcomm_assert(ii != known.end());
    evs_log_debug(D_DELEGATE_MSGS) << "delegate message " << msg;
    Message umsg;
    gu_trace(offset = unserialize_message(UUID::nil(), rb, offset, &umsg));
    gu_trace(handle_msg(umsg, rb, offset));
}


void gcomm::evs::Proto::handle_gap(const GapMessage& msg, NodeMap::iterator ii)
{
    assert(ii != known.end());
    assert(get_state() != S_CLOSED && get_state() != S_JOINING);
    
    Node& inst(NodeMap::get_value(ii));
    evs_log_debug(D_GAP_MSGS) << "gap message " << msg;
    
    if (get_state()                           == S_RECOVERY && 
        install_message                       != 0          && 
        install_message->get_source_view_id() == msg.get_source_view_id()) 
    {
        evs_log_debug(D_STATE) << "install gap " << msg;
        inst.set_installed(true);
        inst.set_tstamp(Time::now());
        if (is_all_installed() == true)
        {
            shift_to(S_OPERATIONAL);
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
            // This is probably partition merge, see if it works out
            inst.set_tstamp(Time::now());
            inst.set_operational(true);
            shift_to(S_RECOVERY);
        } 
        else if (inst.get_installed() == false) 
        {
            // Probably caused by network partitioning during recovery
            // state, this will most probably lead to view 
            // partition/remerge. In order to do it in organized fashion,
            // don't trust the source instance during recovery phase.
            // Note: setting other instance to non-trust here is too harsh
            // LOG_WARN("Setting source status to no-trust");
        } 
        else 
        {
            log_warn << "unhandled gap message " << msg;
        }
        return;
    }
    
    
    gcomm_assert(msg.get_source_view_id() == current_view.get_id());
    
    // 
    const Seqno prev_safe(update_im_safe_seq(msg.get_source(), 
                                             msg.get_aru_seq()));
    if (prev_safe != input_map->get_safe_seq(msg.get_source()))
    {
        inst.set_tstamp(Time::now());
    }

    //
    if (msg.get_range_uuid() == get_uuid())
    {
        gu_trace(resend(msg.get_source(), msg.get_range()));
    }
    
    // 
    gu_trace(deliver());

    // 
    if (get_state() == S_OPERATIONAL)
    {
        while (output.empty() == false)
        {
            int err;
            gu_trace(err = send_user(send_window));
            if (err != 0)
                break;
        }
    }
    
    // 
    if (get_state()                    == S_RECOVERY                && 
        highest_reachable_safe_seq()   == input_map->get_aru_seq()  &&
        prev_safe                      != input_map->get_safe_seq()   )
    {
        gcomm_assert(output.empty() == true);
        const JoinMessage* jm(NodeMap::get_value(self_i).get_join_message());
        if (jm == 0 || is_consistent(*jm) == false)
        {
            gu_trace(send_join());
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
        const MessageNode& node(MessageNodeList::get_value(i));
        gcomm_assert(node.get_view_id() == current_view.get_id());
        const Seqno safe_seq(node.get_safe_seq());
        Seqno prev_safe_seq;
        gu_trace(prev_safe_seq = update_im_safe_seq(uuid, safe_seq));
        if (prev_safe_seq                 != safe_seq &&
            input_map->get_safe_seq(uuid) == safe_seq)
        {
            updated = true;
        }
    }
    return updated;
}


bool gcomm::evs::Proto::retrans_leaves(const MessageNodeList& node_list)
{
    bool sent = false;
    // @todo Check for nodes that have leave messages locally
    // but not seen in all same view nodes
    for (NodeMap::const_iterator li = known.begin(); li != known.end(); ++li)
    {
        const Node& local_node(NodeMap::get_value(li));
        if (local_node.get_leave_message() != 0 && 
            is_inactive(local_node)        == false)
        {
            MessageNodeList::const_iterator msg_li = 
                node_list.find(NodeMap::get_key(li));
            // @todo What if this fires?
            gcomm_assert(msg_li != node_list.end());
            const MessageNode& node(MessageNodeList::get_value(msg_li));
            gcomm_assert(node.get_view_id() == current_view.get_id());
            
            if (node.get_leaving() == false)
            {
                WriteBuf wb(0, 0);
                const LeaveMessage& lm(*NodeMap::get_value(li).get_leave_message());
                LeaveMessage send_lm(lm.get_source(),
                                     lm.get_source_view_id(),
                                     lm.get_seq(),
                                     lm.get_aru_seq(),
                                     lm.get_fifo_seq(),
                                     Message::F_RETRANS | Message::F_SOURCE);
                push_header(send_lm, &wb);
                gu_trace(send_delegate(&wb));
                sent = true;
            }
        }
    }
    return sent;
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
            inst.set_tstamp(Time::now());
            MessageNodeList same_view;
            for_each(msg.get_node_list().begin(), msg.get_node_list().end(),
                     SelectNodesOp(same_view, current_view.get_id(), 
                                   true, true));
            if (update_im_safe_seqs(same_view) == true)
            {
                gu_trace(send_leave(false));
            }
        }
        return;
    }
    else if (is_msg_from_previous_view(msg) == true)
    {
        return;
    }
    else if (install_message != 0)
    {
        // Someone may be still missing either join or install message.
        // Send join (without self handling), install (if representative)
        // and install gap.
        if (is_consistent(*install_message) == true)
        {
            inst.set_tstamp(Time::now());
            if (is_representative(get_uuid()) == true)
            {
                gu_trace(send_install());
            }
            else if (msg.get_source() != get_uuid())
            {
                gu_trace(send_join(false));
            }
            evs_log_debug(D_JOIN_MSGS)
                << "install message and received join, discarding";
            
            if (install_message != 0)
            {
                gu_trace(send_gap(UUID::nil(), 
                                  install_message->get_source_view_id(), 
                                  Range()));
            }
            return;
        }
        else
        {
            gu_trace(shift_to(S_RECOVERY, false));
        }
    }
    else if (get_state() != S_RECOVERY)
    {
        gu_trace(shift_to(S_RECOVERY, false));
    }
    
    // Instance previously declared unoperational seems to be operational now
    if (inst.get_operational() == false) 
    {
        inst.set_operational(true);
        log_debug << self_string() << " unop -> op";
    } 
    
    inst.set_join_message(&msg);
    inst.set_tstamp(Time::now());    
    gcomm_assert(output.empty() == true);

    bool is_consensus_b;
    gu_trace(is_consensus_b = is_consensus());
    if (is_consensus_b == true)
    {
        bool is_representative_b;
        gu_trace(is_representative_b = is_representative(get_uuid()));
        if (is_representative_b == true)
        {
            gu_trace(send_install());
        }
        else if (msg.get_source() != get_uuid())
        {
            if (install_message == 0)
            {
                gu_trace(send_join(false));
            }
        }
    }
    else if (msg.get_source() != get_uuid())
    {
        bool do_send_join(false);
        // Select nodes that are coming from the same view as seen by
        // message source
        MessageNodeList same_view;
        for_each(msg.get_node_list().begin(), msg.get_node_list().end(),
                 SelectNodesOp(same_view, current_view.get_id(), true, true));
        // Coming from the same view
        if (msg.get_source_view_id() == current_view.get_id())
        {
            if (update_im_safe_seqs(same_view) == true)
            {
                do_send_join = true;
            }

            // See if we need to retrans some user messages
            MessageNodeList::const_iterator nli(same_view.find(get_uuid()));
            if (nli != same_view.end())
            {
                const MessageNode& msg_node(MessageNodeList::get_value(nli));
                const Range mn_im_range(msg_node.get_im_range());
                const Range im_range(input_map->get_range(get_uuid()));
                if (mn_im_range.get_hs()   != Seqno::max() &&
                    mn_im_range.get_lu()   < mn_im_range.get_hs())
                {
                    gu_trace(resend(msg.get_source(), mn_im_range));
                    do_send_join = true;
                }
                if (im_range.get_hs()     != Seqno::max() &&
                    (mn_im_range.get_hs() == Seqno::max() ||
                     mn_im_range.get_hs() < im_range.get_hs()))
                {
                    gu_trace(resend(msg.get_source(),
                                    Range(
                                        (mn_im_range.get_hs() == Seqno::max() ?
                                         0 :
                                         mn_im_range.get_hs() + 1), 
                                        im_range.get_hs())));
                }
            }
            
            // Find out max hs and complete up to that if needed
            MessageNodeList::const_iterator max_hs_i(
                max_element(same_view.begin(), same_view.end(), RangeHsCmp()));
            Seqno max_hs;
            gu_trace(max_hs = MessageNodeList::get_value(max_hs_i).get_im_range().get_hs());
            if (max_hs     != Seqno::max()    &&
                (last_sent == Seqno::max() || 
                 last_sent < max_hs          )  )
            {
                gu_trace(complete_user(max_hs));
                do_send_join = true;
            }
            
            
            if (max_hs != Seqno::max())
            {
                // Find out min hs and try to recover messages if 
                // min hs uuid is not operational
                MessageNodeList::const_iterator min_hs_i(
                    min_element(same_view.begin(), same_view.end(), 
                                RangeHsCmp()));
                if (min_hs_i != same_view.end())
                {
                    const UUID& min_hs_uuid(MessageNodeList::get_key(min_hs_i));
                    const Seqno min_hs(MessageNodeList::get_value(min_hs_i).get_im_range().get_hs());                
                    const NodeMap::const_iterator local_i(known.find_checked(min_hs_uuid));
                    const Node& local_node(NodeMap::get_value(local_i));
                    const Range im_range(input_map->get_range(min_hs_uuid));
                    
                    if (local_node.get_operational() == false           &&
                        im_range.get_hs()            != Seqno::max()    &&
                        (min_hs                      == Seqno::max() ||
                         im_range.get_hs()            >  min_hs        )   )
                    {
                        gu_trace(recover(msg.get_source(), min_hs_uuid, 
                                         Range(min_hs == Seqno::max() ? 0 : min_hs,
                                               max_hs)));
                    }
                }
            }
            
            gu_trace((void)retrans_leaves(same_view));
        }
        
        if (do_send_join == true)
        {
            gu_trace(send_join(false));
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
    
    node.set_leave_message(&msg);
    if (msg.get_source() == get_uuid()) 
    {        
        // The last one to live, instant close. Otherwise continue 
        // serving until it becomes apparent that others have
        // leave message.
        if (current_view.get_members().size() == 1)
        {
            gu_trace(shift_to(S_CLOSED));
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
        const Seqno prev_safe_seq(update_im_safe_seq(msg.get_source(), msg.get_aru_seq()));
        if (get_state() == S_OPERATIONAL)
        {
            gu_trace(shift_to(S_RECOVERY, true));
        }
        else if (get_state() == S_RECOVERY &&
                 prev_safe_seq != input_map->get_safe_seq(msg.get_source()))
        {
            gu_trace(send_join());
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
                shift_to(S_CLOSED);
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
        if (is_consistent(msg) && 
            msg.get_source_view_id() == install_message->get_source_view_id())
        {
            evs_log_debug(D_INSTALL_MSGS)
                << " dropping already handled install message";
            gu_trace(send_gap(UUID::nil(), 
                              install_message->get_source_view_id(), 
                              Range()));
            return;
        }
        log_warn << self_string() 
                 << " shift to RECOVERY due to inconsistent install";
        shift_to(S_RECOVERY);
        return;
    }
    else if (inst.get_installed() == true) 
    {
        log_warn << self_string()
                 << " shift to RECOVERY due to inconsistent state";
        shift_to(S_RECOVERY);
        return;
    } 
    else if (is_representative(msg.get_source()) == false) 
    {
        log_warn << self_string() 
                 << " source is not supposed to be representative";
        shift_to(S_RECOVERY);
        return;
    } 
    
    
    assert(install_message == 0);
    
    bool is_consistent_p(is_consistent(msg));
    
    if (is_consistent_p == false)
    {
        // Try constructing join message representing install message, 
        // handling it and then checking again. This may be needed if 
        // representative gains complete information first
        const MessageNode& mn(
            MessageNodeList::get_value(
                msg.get_node_list().find_checked(msg.get_source())));
        JoinMessage jm(msg.get_source(),
                       mn.get_view_id(),
                       msg.get_seq(),
                       msg.get_aru_seq(),
                       msg.get_fifo_seq(),
                       &msg.get_node_list());
        
        handle_join(jm, ii);
        is_consistent_p = is_consistent(msg);
    }
    
    if (is_consistent_p == true && is_consensus() == true)
    {
        inst.set_tstamp(Time::now());
        install_message = new InstallMessage(msg);
        gu_trace(send_gap(UUID::nil(), install_message->get_source_view_id(), 
                          Range()));
    }
    else
    {
        evs_log_debug(D_INSTALL_MSGS)
            << "install message " << msg 
            << " not consistent with state " << *this;
        shift_to(S_RECOVERY, true);
    }
}

