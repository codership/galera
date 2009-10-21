
#include "evs_proto.hpp"
#include "evs_message2.hpp"
#include "evs_input_map2.hpp"

#include "gcomm/transport.hpp"

#include <stdexcept>
#include <algorithm>

using namespace std;
using namespace std::rel_ops;
using namespace gcomm;
using namespace gcomm::evs;


template <class C>
class SameViewSelect
{
public:
    SameViewSelect(C& c_, const ViewId& view_id_) : c(c_), view_id(view_id_) { }
    
    void operator()(const MessageNodeList::value_type& vt) const
    {
        if (MessageNodeList::get_value(vt).get_view_id() == view_id)
        {
            c.insert_checked(vt);
        }
    }
private:
    C& c;
    const ViewId& view_id;
};



template <class C>
class OperationalSelect
{
public:
    OperationalSelect(C& c_) : c(c_) { }
    
    void operator()(const NodeMap::value_type& vt) const
    {
        if (NodeMap::get_value(vt).get_operational() == true)
        {
            c.push_back(&vt);
        }
    }
private:
    C& c;
};

template <class C>
class LeavingSelectOp
{
public:
    LeavingSelectOp(C& c_) : c(c_) { }
    
    void operator()(const NodeMap::value_type& vt) const
    {
        if (NodeMap::get_value(vt).get_leave_message() != 0)
        {
            c.push_back(&vt);
        }
    }
private:
    C& c;
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

static bool msg_from_previous_view(const list<pair<ViewId, Time> >& views, 
                                   const Message& msg)
{
    for (list<pair<ViewId, Time> >::const_iterator i = views.begin();
         i != views.end(); ++i)
    {
        if (msg.get_source_view_id() == i->first)
        {
            log_debug << " message " << msg 
                      << " from previous view " << i->first;
            return true;
        }
    }
    return false;
}


gcomm::evs::Proto::Proto(EventLoop* el_, 
                         Transport* t, 
                         const UUID& my_uuid_, 
                         Monitor* mon_) : 
    timers(),
    mon(mon_),
    tp(t),
    collect_stats(true),
    hs_safe("0.0,0.0005,0.001,0.002,0.005,0.01,0.02,0.05,0.1,0.5,1.,5.,10.,30."),
    delivering(false),
    my_uuid(my_uuid_), 
    known(),
    self_i(),
    view_forget_timeout("PT5M"),
    inactive_timeout("PT5S"),
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





void gcomm::evs::Proto::handle_inactivity_timer()
{
    log_debug << "inactivity timer";
    gu_trace(check_inactive());
    gu_trace(cleanup_views());
}


void gcomm::evs::Proto::handle_retrans_timer()
{
    log_debug << "retrans timer";
    if (get_state() == S_RECOVERY)
    {
        log_debug << self_string() << " send join timer handler";
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
        if (output.empty() == true)
        {
            WriteBuf wb(0, 0);
            gu_trace((void)send_user(&wb, 0xff, SP_DROP, send_window, 
                                         Seqno::max()));
        }
        else
        {
            gu_trace(send_user());
        }
    }
}


void gcomm::evs::Proto::handle_consensus_timer()
{
    log_debug << "consensus timer";
    if (get_state() != S_OPERATIONAL)
    {
        log_warn << "consensus timer expired";
        shift_to(S_RECOVERY, true);
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

#if 0
namespace gcomm
{
    namespace evs {
    static ostream& operator<<(ostream& os, 
                               const pair<const gu::datetime::Date, 
                               gcomm::evs::Proto::Timer>& p)
    {
        return (os << "timer " << p.second << " time " << p.first.get_utc());
    }
    }
}
#endif 

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
            log_warn << "resetting timer " << t;
            timers.erase(ii);
        }
        gu_trace((void)timers.insert(make_pair(get_next_expiration(t), t)));
    }
    
    if (timers.empty() == true)
    {
        log_debug << self_string() << "no timers set";
        return Time::max();
    }
    return TimerList::get_key(timers.begin());
}



void gcomm::evs::Proto::check_inactive()
{
    bool has_inactive = false;
    for (NodeMap::iterator i = known.begin(); i != known.end(); ++i)
    {
        if (NodeMap::get_key(i) != get_uuid() &&
            NodeMap::get_value(i).get_operational() == true &&
            NodeMap::get_value(i).get_tstamp() + inactive_timeout < Time::now())
        {
            log_info << self_string() << " detected inactive node: " 
                     << NodeMap::get_key(i);
            
            i->second.set_operational(false);
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
    log_debug << self_string() << " setting " << uuid << " inactive";
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
            log_debug << self_string() << " erasing " 
                      << NodeMap::get_key(i);
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
            log_info << self_string() << " erasing view: " << i->first;
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
        if (NodeMap::get_value(i).get_installed())
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
                if (MessageNodeList::get_value(inst_i).get_leaving())
                {
                    view.add_left(NodeMap::get_key(i), "");
                }
                else
                {
                    view.add_partitioned(NodeMap::get_key(i), "");
                }
            }
            assert(NodeMap::get_key(i) != get_uuid());
            NodeMap::get_value(i).set_operational(false);
        }
    }
    
    log_debug << view;
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
    
    gu_trace(log_debug << self_string());
    
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
    gu_trace(log_debug << view);
    gcomm_assert(view.get_members().find(get_uuid()) != view.get_members().end());
    ProtoUpMeta up_meta(UUID::nil(), ViewId(), &view);
    gu_trace(pass_up(0, 0, up_meta));
}


void gcomm::evs::Proto::deliver_empty_view()
{
    View view(V_REG);
    log_debug << view;
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
        log_debug << self_string() << " no own join message";
        return false;
    }
    
    if (is_consistent_same_view(*my_jm) == false) 
    {
        log_debug << self_string() << " own join message is not consistent";
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
            log_debug << self_string() << " no join message for " 
                      << NodeMap::get_key(i);
            return false;
        }
        
        if (is_consistent(*jm) == false)
        {
            log_debug << self_string() 
                      << " join message "
                      << *inst.get_join_message() 
                      << " not consistent with state "
                      << *this;
            return false;
        }
    }
    log_debug << self_string() << " consensus reached";
    return true;
}

bool gcomm::evs::Proto::is_representative(const UUID& uuid) const
{
    for (NodeMap::const_iterator i = known.begin(); i != known.end(); ++i) 
    {
        if (NodeMap::get_value(i).get_operational()) 
        {
            return (uuid == NodeMap::get_key(i));
        }
    }

    return false;
}


bool gcomm::evs::Proto::is_consistent_input_map(const Message& msg) const
{
    gcomm_assert(msg.get_type() == Message::T_JOIN || 
                 msg.get_type() == Message::T_INSTALL);
    gcomm_assert(msg.get_source_view_id() == current_view.get_id());
    
    Map<const UUID, Range> local_insts, msg_insts;
    
    for (NodeMap::const_iterator i = known.begin(); i != known.end(); ++i)
    {
        const UUID& uuid(NodeMap::get_key(i));
        const Node& inst(NodeMap::get_value(i));
        if (inst.get_operational() == true &&
            ((inst.get_join_message() != 0 &&
              inst.get_join_message()->get_source_view_id() == 
              current_view.get_id()) || 
             (inst.get_join_message() == 0 &&
              current_view.get_members().find(uuid) != current_view.get_members().end())))
        {
            gu_trace((void)local_insts.insert_checked(make_pair(uuid, input_map->get_range(uuid))));
        }
    }
    
    assert(msg.has_node_list() == true);
    const MessageNodeList& m_insts(msg.get_node_list());
    
    for (MessageNodeList::const_iterator i = m_insts.begin();
         i != m_insts.end(); ++i)
    {
        const UUID& msg_uuid(MessageNodeList::get_key(i));
        const MessageNode& msg_inst(MessageNodeList::get_value(i));
        if (msg_inst.get_operational() == true &&
            msg_inst.get_leaving() == false &&
            msg_inst.get_view_id() == current_view.get_id())
        {
            gu_trace((void)msg_insts.insert_checked(make_pair(msg_uuid, msg_inst.get_im_range())));
        }
    }
    if (msg_insts != local_insts)
    {
        if (msg.get_source() == get_uuid())
        {
            log_warn << "own join not consistent with input map";
        }
        return false;
    }
    return true;
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
    
    assert(msg.has_node_list() == true);
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
    if (local_insts != msg_insts)
    {
        if (msg.get_source() == get_uuid())
        {
            log_warn << "own join not consistent with partitioning";
        }
        return false;
    }
    return true;
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
        if (inst.get_operational() == false &&
            inst.get_leave_message() != 0  &&
            current_view.get_members().find(uuid) != current_view.get_members().end())
        {
            gu_trace((void)local_insts.insert_checked(
                         make_pair(uuid, input_map->get_range(uuid))));
        }
    }
    
    assert(msg.has_node_list() == true);
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
    if (local_insts != msg_insts)
    {
        if (msg.get_source() == get_uuid())
        {
            log_warn << "own join not consistent with leaving";
        }
        return false;
    }
    return true;
}

bool gcomm::evs::Proto::is_consistent_same_view(const Message& msg) const
{
    gcomm_assert(msg.get_type() == Message::T_JOIN ||
                 msg.get_type() == Message::T_INSTALL);
    gcomm_assert(msg.get_source_view_id() == current_view.get_id());
    
    // Compare aru seqs
    if (input_map->get_aru_seq() != msg.get_aru_seq())
    {
        log_debug << self_string() 
                  << " aru seq not consistent, local " << input_map->get_aru_seq()
                  << " msg " << msg.get_aru_seq();
        if (msg.get_source() == get_uuid())
        {
            log_warn << self_string() << " own join not consistent with input map aru seq " << input_map->get_aru_seq() << " " << msg;
        }
        return false;
    }
    
    // Compare safe seqs
    if (input_map->get_safe_seq() != msg.get_seq())
    {
        log_debug << self_string() 
                  << " safe seq not consistent, local " << input_map->get_safe_seq()
                  << " msg " << msg.get_seq();
        if (msg.get_source() == get_uuid())
        {
            log_warn << self_string() << " own join not consistent with input map safe seq " << input_map->get_safe_seq() << " " << msg;
        }
        return false;
    }
    
    if (is_consistent_input_map(msg) == false)
    {
        log_debug << self_string() 
                  << " input map not consistent " << *input_map 
                  << " with message " << msg;
        return false;
    }
    
    if (is_consistent_partitioning(msg) == false)
    {
        log_debug << self_string() << " partitioning not consistent";
        return false;
    }
    
    if (is_consistent_leaving(msg) == false)
    {
        log_debug << self_string() << " leaving not consistent";
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
                log_warn << "own join not consistent with joining";
            }
            return false;
        }
        
        if (msg.get_source_view_id() == jm->get_source_view_id())
        { 
            if (msg.get_aru_seq() != jm->get_aru_seq())
            {
                if (msg.get_source() == get_uuid())
                {
                    log_warn << "own join not consistent with joining jm aru seq";
                }
                return false;
            }
            if (msg.get_seq() != jm->get_seq())
            {
                if (msg.get_source() == get_uuid())
                {
                    log_warn << "own join not consistent with joining jm seq";
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
    
    if (local_insts != msg_insts)
    {
        if (msg.get_source() == get_uuid())
        {
            log_warn << "own join not consistent with joining";
        }
        return false;
    }
    
    return true;
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
    gcomm_assert(up_to_seqno == Seqno::max() || last_sent == Seqno::max() ||
                 up_to_seqno >= last_sent);
    
    int ret;
    const Seqno seq(last_sent == Seqno::max() ? 0 : last_sent + 1);
    
    // Allow flow control only in S_OPERATIONAL state to make 
    // S_RECOVERY state output flush possible.
    if (local                     == false         && 
        get_state()               == S_OPERATIONAL && 
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
        is_flow_control(last_msg_seq + 1, win) == true)
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
    if (input_map->get_aru_seq() != Seqno::max())
    {
        gu_trace(input_map->set_safe_seq(get_uuid(), input_map->get_aru_seq()));
    }
    
    if (local == false)
    {
        // Rewrite message hdr to include correct aru
        msg.set_aru_seq(input_map->get_aru_seq());
        push_header(msg, wb);
        if ((ret = pass_down(wb, 0)) != 0)
        {
            log_debug << " pass down: "  << ret;
        }
        pop_header(msg, wb);
    }
    
    if (delivering == false)
    {
        gu_trace(deliver());
    }
    return 0;
}

int gcomm::evs::Proto::send_user()
{
    
    if (output.empty())
        return 0;
    assert(get_state() == S_OPERATIONAL || get_state() == S_RECOVERY);
    pair<WriteBuf*, ProtoDownMeta> wb = output.front();
    int ret;
    if ((ret = send_user(wb.first, wb.second.get_user_type(), 
                         wb.second.get_safety_prefix(), 
                         send_window, Seqno::max())) == 0) {
        output.pop_front();
        delete wb.first;
    }
    return ret;
}


void gcomm::evs::Proto::complete_user(const Seqno high_seq)
{
    log_debug << self_string() << " completing seqno to " << high_seq;;
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
    log_debug << self_string() << " to "  << range_uuid 
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
        log_warn << "send failed " << strerror(err);
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
        const Range range(in_current == true ? input_map->get_range(uuid) : Range());
        const MessageNode mnode(node.get_operational(),
                                node.get_leave_message() != 0,
                                vid, 
                                safe_seq, 
                                range);
        log_debug << " inserting " << uuid;
        gu_trace((void)node_list->insert_checked(make_pair(uuid, mnode)));
    }
}

JoinMessage gcomm::evs::Proto::create_join()
{
    
    MessageNodeList node_list;
    
    gu_trace(populate_node_list(&node_list));
    JoinMessage jm(get_uuid(),
                   current_view.get_id(),
                   input_map->get_safe_seq(),
                   input_map->get_aru_seq(),
                   ++fifo_seq,
                   &node_list);
    log_debug << self_string() << " created join message " << jm;
    if (is_consistent_same_view(jm) == false)
    {
        gcomm_throw_fatal << "inconsistent JOIN message "
                          << jm << " local state "
                          << *this;
    }
    return jm;
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
        log_warn << "Duplicate leave:\told: "
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
    
    log_debug << self_string() << " sending join " << jm;
    
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
    else
    {
        set_join(jm, get_uuid());
    }
}

void gcomm::evs::Proto::send_leave()
{
    assert(get_state() == S_LEAVING);
    
    log_debug << self_string() << " send leave as " << last_sent;
    
    LeaveMessage lm(get_uuid(),
                    current_view.get_id(),
                    last_sent,
                    input_map->get_aru_seq(), 
                    ++fifo_seq);

    WriteBuf wb(0, 0);
    push_header(lm, &wb);
    
    int err = pass_down(&wb, 0);
    if (err != 0)
    {
        log_warn << "send failed " << strerror(err);
    }
    
    handle_leave(lm, self_i);
}


struct ViewIdCmp
{
    bool operator()(const NodeMap::value_type* a,
                    const NodeMap::value_type* b) const
    {
        gcomm_assert(NodeMap::get_value(*a).get_join_message() != 0&&
                     NodeMap::get_value(*b).get_join_message() != 0);
        return (NodeMap::get_value(*a).get_join_message()->get_source_view_id().get_seq() <
                NodeMap::get_value(*b).get_join_message()->get_source_view_id().get_seq());
        
    }
};


void gcomm::evs::Proto::send_install()
{
    gcomm_assert(is_consensus() == true && 
                 is_representative(get_uuid()) == true);
    
    list<const NodeMap::value_type*> oper_list;
    for_each(known.begin(), known.end(), 
             OperationalSelect<list<const NodeMap::value_type*> >(oper_list));
    list<const NodeMap::value_type*>::const_iterator max_node = 
        max_element(oper_list.begin(), oper_list.end(), ViewIdCmp());
    
    const uint32_t max_view_id_seq = 
        NodeMap::get_value(**max_node).get_join_message()->get_source_view_id().get_seq();
    
    MessageNodeList node_list;
    populate_node_list(&node_list);
    
    InstallMessage imsg(get_uuid(),
                        ViewId(V_REG, get_uuid(), max_view_id_seq + 1),
                        input_map->get_safe_seq(),
                        input_map->get_aru_seq(),
                        ++fifo_seq,
                        &node_list);
    
    log_debug << self_string() << " sending install: " << imsg;
    
    vector<byte_t> buf(imsg.serial_size());
    gu_trace((void)imsg.serialize(&buf[0], buf.size(), 0));
    WriteBuf wb (&buf[0], buf.size());
    
    int err = pass_down(&wb, 0);;
    if (err != 0) 
    {
        log_warn << "send failed " << strerror(err);
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
        log_warn << "lu <= safe_seq, can't recover message";
        return;
    }
    
    log_debug << self_string() << " resending, requested by " 
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
            log_warn << "retrans failed " << strerror(err);
            break;
        }
        else
        {
            log_debug << "retransmitted " << um;
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
    
    if (input_map->get_safe_seq() == Seqno::max() ||
        range.get_lu() <= input_map->get_safe_seq())
    {
        log_warn << "lu <= safe_seq, can't recover message";
        return;
    }
    
    const Range im_range(input_map->get_range(range_uuid));
    
    log_debug << self_string() << " recovering message from "
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
            log_warn << "recovery failed " << strerror(err);
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
    
    const UUID& source = msg.get_source();
    
    log_debug << self_string() << " detected new source: "
              << source;
    
    NodeMap::iterator i;
    gu_trace(i = known.insert_checked(make_pair(source, Node())));
    assert(NodeMap::get_value(i).get_operational() == true);
    
    if (get_state() == S_JOINING || get_state() == S_RECOVERY || 
        get_state() == S_OPERATIONAL)
    {
        log_debug << self_string()
                  << " shift to S_RECOVERY due to foreign message";
        shift_to(S_RECOVERY, true);
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
        log_debug << " dropping message in closed state";
        return;
    }
    if (msg.get_source() == get_uuid())
    {
        log_debug << " dropping self originated message";
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
            log_warn << "droppoing non-fifo message " << msg
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
        log_warn << "Invalid message type: " << msg.get_type();
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
    Critical crit(mon);
    
    Message msg;
    
    if (rb == 0)
    {
        gcomm_throw_fatal << "Invalid input: rb == 0";
    }
    
    if (get_state() == S_CLOSED)
    {
        log_debug << " dropping message in closed state";
        return;
    }
    
    gcomm_assert(um.get_source() != UUID::nil());    
    if (um.get_source() == get_uuid())
    {
        log_warn << "dropping self originated message";
        return;
    }
    
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
    Critical crit(mon);
    
    if (get_state() == S_RECOVERY)
    {
        log_debug << "state == S_RECOVERY";
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
            log_error << "Send error: " << err;
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
        log_debug << "output.size()=" << output.size() << " "
            "max_output_size=" << max_output_size;
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
        log_debug << self_string() << " state change: " 
                  << to_string(state) << " -> " << to_string(s);
    }
    switch (s) {
    case S_CLOSED:
        if (collect_stats)
        {
            log_info << "delivery stats (safe): " << hs_safe;
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
        state = S_RECOVERY;
        log_debug << self_string() << " shift to recovery, flushing "
                  << output.size() << " messages";
        while (output.empty() == false)
        {
            // @fixme What if send_user() fails?
            int err = send_user();
            if (err != 0)
            {
                gcomm_throw_fatal << self_string() 
                                  << "send_user() failed in shifto to S_RECOVERY: "
                                  << strerror(err);
            }
        }
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
        if (collect_stats)
        {
            log_info << "delivery stats (safe): " << hs_safe;
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
        log_debug << self_string() << " new view: " << current_view;
        // start_resend_timer();
        gcomm_assert(get_state() == S_OPERATIONAL);
        reset_timers();
        break;
    }
    default:
        gcomm_throw_fatal << "Invalid state";
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
        gcomm_throw_fatal << "Reg validate: not current view";
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
        gcomm_throw_fatal << "Invalid state";
    
    log_debug << self_string() 
              << " aru_seq: "   << input_map->get_aru_seq() 
              << " safe_seq: " << input_map->get_safe_seq();
    
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
    log_debug << self_string() << " " << msg;
    
    if (msg.get_source_view_id() != current_view.get_id())
    {
        // @todo: do we have to freak out here?
        gcomm_throw_fatal << "Reg validate: not current view";
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
        gcomm_throw_fatal << "Invalid state";
    
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


void gcomm::evs::Proto::handle_user(const UserMessage& msg, 
                                    NodeMap::iterator ii, 
                                    const ReadBuf* rb, 
                                    const size_t roff)
{
    assert(ii != known.end());
    Node& inst(NodeMap::get_value(ii));
    
    if (msg.get_flags() & Message::F_RETRANS)
    {
        log_debug << self_string() << " msg with retrans flag " << msg;
    }
    else 
    {
        
        log_debug << self_string() << " " << msg;
    }
    
    if (get_state() == S_JOINING || get_state() == S_CLOSED) 
    {
        // Drop message
        log_debug << self_string() << " dropping " << msg;
        return;
    } 
    else if (msg.get_source_view_id() != current_view.get_id()) 
    {
        if (get_state() == S_LEAVING) 
        {
            log_debug << self_string() << " leaving, dropping " 
                      << msg;
            return;
        }
        
        if (msg_from_previous_view(previous_views, msg))
        {
            log_debug << self_string() << " user message " 
                      << msg 
                      << " from previous view";
            return;
        }
        
        if (inst.get_operational() == false) 
        {
            // This is probably partition merge, see if it works out
            log_debug << self_string() << " unoperational source " 
                      << msg;
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
                log_debug << self_string() << " recovery user message "
                          << msg;
                
                // Other instances installed view before this one, so it is 
                // safe to shift to S_OPERATIONAL if consensus has been reached
                for (MessageNodeList::const_iterator
                         mi = install_message->get_node_list().begin(); 
                     mi != install_message->get_node_list().end(); ++mi)
                {
                    NodeMap::iterator jj;
                    gu_trace(jj = known.find_checked(
                                 MessageNodeList::get_key(mi)));
                    NodeMap::get_value(jj).set_installed(true);
                }
                
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
            log_debug << self_string() << " unknown user message "
                      << msg;
            return;
        }
    }
    
    gcomm_assert(msg.get_source_view_id() == current_view.get_id());
    
    const Seqno prev_aru(input_map->get_aru_seq());
    const Seqno prev_safe(input_map->get_safe_seq(msg.get_source()));
    const Range prev_range(input_map->get_range(msg.get_source()));
    Range range;
    
    // Insert only if msg seq is greater or equal than current lowest unseen
    if (msg.get_seq() >= prev_range.get_lu())
    {
        gu_trace(range = input_map->insert(msg.get_source(), msg, rb, roff));
    }
    else
    {
        range = prev_range;
    }
    
    
    if (input_map->get_aru_seq() != Seqno::max() &&
        (prev_aru == Seqno::max() || prev_aru < input_map->get_aru_seq()))
    {
        input_map->set_safe_seq(get_uuid(), input_map->get_aru_seq());
    }
    
    if (msg.get_aru_seq() != Seqno::max() &&
        (prev_safe == Seqno::max() || prev_safe < msg.get_aru_seq()))
    {
        gu_trace(input_map->set_safe_seq(msg.get_source(), msg.get_aru_seq()));
    }
    
    if (range.get_lu() > prev_range.get_lu())
    {
        inst.set_tstamp(Time::now());
    }
    
    // Missing messages
    if (range.get_hs()                         >  range.get_lu() && 
        (msg.get_flags() & Message::F_RETRANS) == 0                 )
    {
        log_debug << self_string() 
                  << " requesting retrans from " 
                  << msg.get_source() << " "
                  << range 
                  << " due to input map gap, aru " 
                  << input_map->get_aru_seq();
        gu_trace(send_gap(msg.get_source(), current_view.get_id(), range));
    }
    
    if (output.empty() == true &&
        (msg.get_flags() & Message::F_MSG_MORE) == false &&
        (last_sent == Seqno::max() || last_sent < range.get_hs()))
    {
        // Message not originated from this instance, output queue is empty
        // and last_sent seqno should be advanced
        gu_trace(complete_user(range.get_hs()));
    }
    else if ((output.empty() == true && 
              input_map->get_aru_seq() != prev_aru) ||
             get_state() == S_LEAVING)
    {
        // Output queue empty and aru changed, send gap to inform others
        // @todo Why doing this always in leaving state?
        log_debug << self_string() << " sending empty gap";
        gu_trace(send_gap(UUID::nil(), current_view.get_id(), Range()));
    }
    
    gu_trace(deliver());
    while (output.empty() == false)
    {
        int err;
        gu_trace(err = send_user());
        if (err != 0)
        {
            break;
        }
    }
    
    
    if (get_state() == S_RECOVERY && 
        last_sent == input_map->get_aru_seq() && 
        (prev_aru != input_map->get_aru_seq() ||
         prev_safe != input_map->get_safe_seq()))
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
    Message umsg;
    gu_trace(offset = unserialize_message(UUID::nil(), rb, offset, &umsg));
    gu_trace(handle_msg(umsg, rb, offset));
}

void gcomm::evs::Proto::handle_gap(const GapMessage& msg, NodeMap::iterator ii)
{
    assert(ii != known.end());
    Node& inst(NodeMap::get_value(ii));
    log_debug << self_string() << " " << msg;
    
    if (get_state() == S_JOINING || get_state() == S_CLOSED) 
    {	
        // Silent drop
        return;
    } 
    else if (get_state() == S_RECOVERY && 
             install_message != 0 && 
             install_message->get_source_view_id() == msg.get_source_view_id()) 
    {
        log_debug << self_string() << " install gap " << msg;
        inst.set_installed(true);
        if (is_all_installed() == true)
        {
            shift_to(S_OPERATIONAL);
        }
        return;
    } 
    else if (msg.get_source_view_id() != current_view.get_id()) 
    {
        if (msg_from_previous_view(previous_views, msg) == true)
        {
            log_debug << "gap message from previous view";
            return;
        }
        if (inst.get_operational() == false) 
        {
            // This is probably partition merge, see if it works out
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
            log_debug << self_string() << " unknown gap message " << msg;
        }
        return;
    }
    
    gcomm_assert(msg.get_source_view_id() == current_view.get_id());
    
    const Seqno prev_safe(input_map->get_safe_seq(msg.get_source()));
    if (msg.get_aru_seq() != Seqno::max() &&
        (prev_safe == Seqno::max() || prev_safe < msg.get_aru_seq()))
    {
        gu_trace(input_map->set_safe_seq(msg.get_source(), msg.get_aru_seq()));
    }
    
    // Scan through gap list and resend or recover messages if appropriate.
    log_debug << self_string() << " range uuid " << msg.get_range_uuid();
    if (msg.get_range_uuid() == get_uuid())
    {
        gu_trace(resend(msg.get_source(), msg.get_range()));
    }
    
    // Deliver messages 
    gu_trace(deliver());
    while (get_state() == S_OPERATIONAL && output.empty() == false)
    {
        int err;
        gu_trace(err = send_user());
        if (err != 0)
            break;
    }
    
    if (get_state() == S_RECOVERY && 
        last_sent == input_map->get_aru_seq() &&
        prev_safe != input_map->get_safe_seq())
    {
        gcomm_assert(output.empty() == true);
        const JoinMessage* jm(NodeMap::get_value(self_i).get_join_message());
        if (jm == 0 || is_consistent(*jm) == false)
        {
            gu_trace(send_join());
        }
    }
}


void gcomm::evs::Proto::handle_join(const JoinMessage& msg, NodeMap::iterator ii)
{
    gcomm_assert(ii != known.end());
    Node& inst(NodeMap::get_value(ii));
    
    log_debug << self_string() << " " << current_view.get_id();
    log_debug << " ================ enter handle_join ==================";
    log_debug << self_string() << " " << msg;
    
    if (get_state() == S_LEAVING) 
    {
        log_debug << "================ leave handle_join ==================";
        return;
    }
    else if (msg_from_previous_view(previous_views, msg))
    {
        log_debug << self_string() 
                  << " join message from one of the previous views " 
                  << msg.get_source_view_id();
        log_debug << "================ leave handle_join ==================";
        return;
    }
    else if (install_message != 0)
    {
        // Someone may be still missing either join or install message.
        // Send join (without self handling), install (if representative)
        // and install gap.
        send_join(false);
        if (is_representative(get_uuid()) == true)
        {
            gu_trace(send_install());
        }
        
        log_debug << self_string()
                  << " install message and received join, discarding";
        log_debug << "================ leave handle_join ==================";
        send_gap(UUID::nil(), install_message->get_source_view_id(), 
                 Range());
        return;
    }
    else if (get_state() != S_RECOVERY)
    {
        shift_to(S_RECOVERY, false);
    }

    // Instance previously declared unoperational seems to be operational now
    if (inst.get_operational() == false) 
    {
        inst.set_operational(true);
        log_debug << self_string() << " unop -> op";
    } 
    
    inst.set_join_message(&msg);
    
    gcomm_assert(output.empty() == true);
    
    if (is_consensus() == true)
    {
        if (is_representative(get_uuid()) == true)
        {
            gu_trace(send_install());
        }
        else
        {
            // do nothing, wait for install message
        }
    }
    else
    {
        bool do_send_join(false);
        // Select nodes that are coming from the same view as seen by
        // message source
        MessageNodeList same_view;
        for_each(msg.get_node_list().begin(), msg.get_node_list().end(),
                 SameViewSelect<MessageNodeList>(same_view, current_view.get_id()));
        // Coming from the same view
        if (msg.get_source_view_id() == current_view.get_id())
        {
            // Update input map state
            const Seqno im_safe_seq(input_map->get_safe_seq(msg.get_source()));
            if (msg.get_aru_seq() != Seqno::max()         && 
                (im_safe_seq      == Seqno::max()    || 
                 im_safe_seq      < msg.get_aru_seq()  )     )
            {
                gu_trace(input_map->set_safe_seq(msg.get_source(),
                                                 msg.get_aru_seq()));
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
                if (im_range.get_hs() != Seqno::max() &&
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
            const Seqno max_hs(MessageNodeList::get_value(max_hs_i).get_im_range().get_hs());
            log_debug << self_string() << " same view max hs " << max_hs;
            if (max_hs != Seqno::max() &&
                (last_sent == Seqno::max() || last_sent < max_hs))
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
                const UUID& min_hs_uuid(MessageNodeList::get_key(min_hs_i));
                const Seqno min_hs(MessageNodeList::get_value(min_hs_i).get_im_range().get_hs());                
                const NodeMap::const_iterator local_i(known.find_checked(min_hs_uuid));
                const Node& local_node(NodeMap::get_value(local_i));
                const Range im_range(input_map->get_range(min_hs_uuid));
                log_debug << self_string() << " same view min hs " << min_hs;
                if (local_node.get_operational() == false &&
                    im_range.get_hs()            != Seqno::max() &&
                    im_range.get_hs()            >  min_hs)
                {
                    gu_trace(recover(msg.get_source(), min_hs_uuid, 
                                     Range(min_hs, max_hs)));
                    do_send_join = true;
                }
            }

            // @todo Check for nodes that have leave messages locally
            // but not seen in all same view nodes
            list<const NodeMap::value_type*> leaving;
            for_each(known.begin(), known.end(), 
                     LeavingSelectOp<list<const NodeMap::value_type*> >(leaving));
            for (list<const NodeMap::value_type*>::const_iterator 
                     li = leaving.begin(); 
                 li != leaving.end(); ++li)
            {
                MessageNodeList::const_iterator msg_li =
                    same_view.find(NodeMap::get_key(**li));
                // @todo What if this fires?
                gcomm_assert(msg_li != same_view.end());
                if (MessageNodeList::get_value(msg_li).get_leaving() == false)
                {
                    WriteBuf wb(0, 0);
                    const LeaveMessage& lm(*NodeMap::get_value(**li).get_leave_message());
                    LeaveMessage send_lm(lm.get_source(),
                                         lm.get_source_view_id(),
                                         lm.get_seq(),
                                         lm.get_aru_seq(),
                                         lm.get_fifo_seq(),
                                         Message::F_RETRANS | Message::F_SOURCE);
                    push_header(send_lm, &wb);
                    gu_trace(send_delegate(&wb));
                }
            }
        }
        
        if (do_send_join == true)
        {
            gu_trace(send_join(false));
        }
    }
    
    log_debug << "================ leave handle_join ==================";
}


void gcomm::evs::Proto::handle_leave(const LeaveMessage& msg, 
                                     NodeMap::iterator ii)
{
    assert(ii != known.end());
    Node& inst(NodeMap::get_value(ii));
    log_debug << self_string() << " leave message " << msg;
    
    set_leave(msg, msg.get_source());
    if (msg.get_source() == get_uuid()) 
    {
        /* Move all pending messages from output to input map */
        while (output.empty() == false)
        {
            pair<WriteBuf*, ProtoDownMeta> wb = output.front();
            if (send_user(wb.first, 
                          wb.second.get_user_type(), 
                          wb.second.get_safety_prefix(), 
                          0, Seqno::max(), true) != 0)
            {
                gcomm_throw_fatal << "send_user() failed";
            }
            
            output.pop_front();
            delete wb.first;
        }
        
        /* Deliver all possible messages in reg view */
        gu_trace(deliver());
        setall_installed(false);
        inst.set_installed(true);
        gu_trace(deliver_trans_view(true));
        gu_trace(deliver_trans());
        gu_trace(deliver_empty_view());
        gu_trace(shift_to(S_CLOSED));
    } 
    else 
    {
        if (msg_from_previous_view(previous_views, msg) == true)
        {
            log_debug << self_string() << " leave message from previous view";
            return;
        }
        inst.set_operational(false);
        shift_to(S_RECOVERY, true);
        if (is_consensus() == true && is_representative(get_uuid()) == true)
        {
            gu_trace(send_install());
        }
    }
}

void gcomm::evs::Proto::handle_install(const InstallMessage& msg, 
                                       NodeMap::iterator ii)
{
    
    assert(ii != known.end());
    Node& inst(NodeMap::get_value(ii));
    
    if (get_state() == S_LEAVING) 
    {
        log_debug << self_string() << " dropping install message in leaving state";
        return;
    }
    
    log_debug << self_string() << " " << msg;
    
    if (get_state() == S_JOINING || get_state() == S_CLOSED) 
    {
        log_debug << self_string() 
                  << " dropping install message from " << msg.get_source();
        return;
    } 
    else if (get_state() == S_OPERATIONAL &&
             current_view.get_id() == msg.get_source_view_id())
    {
        log_debug << self_string()
                  << " dropping install message in already installed view";
        return;
    }
    else if (inst.get_operational() == false) 
    {
        log_debug << self_string() << " setting other as operational";
        inst.set_operational(true);
        shift_to(S_RECOVERY);
        return;
    } 
    else if (msg_from_previous_view(previous_views, msg))
    {
        log_debug << self_string() 
                  << " dropping install message from previous view";
        return;
    }
    else if (install_message != 0)
    {
        if (is_consistent(msg) && 
            msg.get_source_view_id() == install_message->get_source_view_id())
        {
            log_debug << self_string()
                      << " dropping already handled install message";
            gu_trace(send_gap(UUID::nil(), 
                              install_message->get_source_view_id(), 
                              Range()));
            return;
        }
        log_warn << self_string() 
                 << " shift to S_RECOVERY due to inconsistent install";
        shift_to(S_RECOVERY);
        return;
    }
    else if (inst.get_installed() == true) 
    {
        log_debug << self_string()
                  << " shift to S_RECOVERY due to inconsistent state";
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
        install_message = new InstallMessage(msg);
        gu_trace(send_gap(UUID::nil(), install_message->get_source_view_id(), 
                          Range()));
    }
    else
    {
        log_warn << self_string() 
                 << " install message " 
                 << msg 
                 << " not consistent with state " << *this;
        shift_to(S_RECOVERY, true);
    }
}

