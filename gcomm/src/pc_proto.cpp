
#include "pc_proto.hpp"
#include "pc_message.hpp"
#include "gcomm/logger.hpp"
#include "gcomm/util.hpp"

#include <set>

using namespace std;
using namespace std::rel_ops;

using namespace gu;
using namespace gu::net;

using namespace gcomm;


void PCProto::send_state()
{
    log_debug << self_string() << " sending state";
    
    PCStateMessage pcs;
    
    PCInstMap& im(pcs.get_inst_map());
    
    for (PCInstMap::iterator i = instances.begin(); i != instances.end(); ++i)
    {
        // Assume all nodes in the view have reached current to_seq
        PCInst& local_state(PCInstMap::get_value(i));
        local_state.set_to_seq(get_to_seq());
        im.insert(make_pair(PCInstMap::get_key(i), local_state));
    }
    
    Buffer buf;
    serialize(pcs, buf);
    
    if (send_down(Datagram(buf), ProtoDownMeta()))
    {
        gcomm_throw_fatal << "pass down failed";
    }    
}

void PCProto::send_install()
{
    log_debug << self_string() << " send install";
    
    PCInstallMessage pci;
    
    PCInstMap& im(pci.get_inst_map());
    
    for (SMMap::const_iterator i = state_msgs.begin(); i != state_msgs.end();
         ++i)
    {
        if (current_view.get_members().find(SMMap::get_key(i)) != 
            current_view.get_members().end())
        {
            gu_trace(
                im.insert_checked(
                    make_pair(
                        SMMap::get_key(i), 
                        SMMap::get_value(i).get_inst((SMMap::get_key(i))))));
        }
    }
    
    Buffer buf;
    serialize(pci, buf);
    
    int ret = send_down(Datagram(buf), ProtoDownMeta());
    if (ret != 0)
    {
        log_warn << self_string() << " sending install message failed: "
                 << strerror(ret);
    }
}


void PCProto::deliver_view()
{
    View v(pc_view.get_id()); 
    
    v.add_members(current_view.get_members().begin(), 
                  current_view.get_members().end());
    
    for (PCInstMap::const_iterator i = instances.begin(); 
         i != instances.end(); ++i)
    {
        if (current_view.get_members().find(PCInstMap::get_key(i)) ==
            current_view.get_members().end())
        {
            v.add_partitioned(PCInstMap::get_key(i), "");
        }
    }
    
    ProtoUpMeta um(UUID::nil(), ViewId(), &v);
    log_info << self_string() << " delivering view " << v;
    send_up(Datagram(), um);
}


void PCProto::shift_to(const State s)
{
    // State graph
    static const bool allowed[S_MAX][S_MAX] = {
        
        // Closed
        { false, true, false,  false, false, false, false },
        // Joining
        { true,  false, false,  false, false, true, false },
        // States exch
        { true,  false, false, true,  false, true,  true  },
        // Install
        { true,  false, false, false, true,  true,  true  },
        // Prim
        { true,  false, false, false, false, true,  true  },
        // Trans
        { true,  false, true,  false, false, false, true },
        // Non-prim
        { true,  false, true,  false, false, true, true  }
    };
    

    
    if (allowed[get_state()][s] == false)
    {
        gcomm_throw_fatal << "Forbidden state transtion: "
                          << to_string(get_state()) << " -> " << to_string(s);
    }
    
    switch (s)
    {
    case S_CLOSED:
        break;
    case S_JOINING:
        break;
    case S_STATES_EXCH:
        state_msgs.clear();
        break;
    case S_INSTALL:
        break;
    case S_PRIM:
    {
        for (PCInstMap::iterator i = instances.begin(); i != instances.end();
             ++i)
        {
            const UUID& uuid(PCInstMap::get_key(i));
            if (current_view.get_members().find(uuid) != 
                current_view.get_members().end())
            {
                PCInst& inst(PCInstMap::get_value(i));
                inst.set_prim(true);
                inst.set_last_prim(ViewId(V_PRIM, current_view.get_id()));
                inst.set_last_seq(0);
                inst.set_to_seq(get_to_seq());
            }
        }
        set_prim(true);
        pc_view = ViewId(V_PRIM, current_view.get_id());
        break;
    }
    case S_TRANS:
        break;
    case S_NON_PRIM:
        set_prim(false);
        pc_view = ViewId(V_NON_PRIM, current_view.get_id());
        break;
    default:
        ;
    }

    log_info << self_string() << " shift_to: " << to_string(get_state()) 
             << " -> " <<  to_string(s) 
             << " prim " << get_prim()
             << " last prim " << get_last_prim()
             << " to_seq " << get_to_seq();

    state = s;
}


void PCProto::handle_first_trans(const View& view)
{
    gcomm_assert(get_state() == S_JOINING);
    gcomm_assert(view.get_type() == V_TRANS);
    
    if (start_prim == true)
    {
        if (view.get_members().size() > 1 || view.is_empty())
        {
            gcomm_throw_fatal << "Corrupted view";
        }
        
        if (NodeList::get_key(view.get_members().begin()) != get_uuid())
        {
            gcomm_throw_fatal << "Bad first UUID: "
                              << NodeList::get_key(view.get_members().begin())
                              << ", expected: " << get_uuid();
        }
        
        set_last_prim(ViewId(V_PRIM, view.get_id()));
        set_prim(true);
    }
    current_view = view;
    shift_to(S_TRANS);
}


void PCProto::handle_trans(const View& view)
{
    gcomm_assert(view.get_id().get_type() == V_TRANS);
    gcomm_assert(view.get_id().get_uuid() == current_view.get_id().get_uuid() &&
                 view.get_id().get_seq()  == current_view.get_id().get_seq());
    
    if (ViewId(V_PRIM, view.get_id()) == get_last_prim())
    {
        if (view.get_members().size()*2 + view.get_left().size() <=
            current_view.get_members().size())
        {
            current_view = view;
            shift_to(S_NON_PRIM);
            deliver_view();
            return;
        }
    }
    else
    {
        if (get_last_prim().get_uuid() != view.get_id().get_uuid() &&
            get_last_prim().get_seq()  != view.get_id().get_seq() )
        {
            log_debug << self_string() 
                      << " trans view during " << to_string(get_state());
        }
    }
    current_view = view;
    shift_to(S_TRANS);
}


void PCProto::handle_first_reg(const View& view)
{
    gcomm_assert(view.get_type() == V_REG);
    gcomm_assert(get_state() == S_TRANS);
    
    if (start_prim == true)
    {
        if (view.get_members().size() > 1 || view.is_empty())
        {
            gcomm_throw_fatal << self_string() << " starting primary "
                              <<"but first reg view is not singleton";
        }
    }
    
    if (view.get_id().get_seq() <= current_view.get_id().get_seq())
    {
        gcomm_throw_fatal << "Non-increasing view ids: current view " 
                          << current_view.get_id() 
                          << " new view "
                          << view.get_id();
    }
    
    current_view = view;
    views.push_back(current_view);
    shift_to(S_STATES_EXCH);
    send_state();
}


void PCProto::handle_reg(const View& view)
{
    gcomm_assert(view.get_type() == V_REG);
    
    if (view.is_empty() == false && 
        view.get_id().get_seq() <= current_view.get_id().get_seq())
    {
        gcomm_throw_fatal << "Non-increasing view ids: current view " 
                          << current_view.get_id() 
                          << " new view "
                          << view.get_id();
    }
    
    current_view = view;
    views.push_back(current_view);
    
    if (current_view.is_empty() == true)
    {
        shift_to(S_NON_PRIM);
        deliver_view();
        shift_to(S_CLOSED);
    }
    else
    {
        shift_to(S_STATES_EXCH);
        send_state();
    }
}


void PCProto::handle_view(const View& view)
{
    
    // We accept only EVS TRANS and REG views
    if (view.get_type() != V_TRANS && view.get_type() != V_REG)
    {
        gcomm_throw_fatal << "Invalid view type";
    }
    
    // Make sure that self exists in view
    if (view.is_empty()            == false &&
        view.is_member(get_uuid()) == false)
    {
        gcomm_throw_fatal << "Self not found from non empty view: "
                          << view;
    }
    
    log_debug << self_string() << " " << view;
    
    if (view.get_type() == V_TRANS)
    {
        if (get_state() == S_JOINING)
        {
            handle_first_trans(view);
        }
        else
        {
            handle_trans(view);
        }
    }
    else
    {
        if (get_state() == S_JOINING)
        {
            handle_first_reg(view);
        }
        else
        {
            handle_reg(view);
        }  
    }
}


class ToSeqCmpOp
{
public:
    bool operator()(const PCProto::SMMap::value_type& a,
                    const PCProto::SMMap::value_type& b) const
    {
        const PCInst& astate(
            PCInstMap::get_value(
                PCProto::SMMap::get_value(a).get_inst_map().find_checked(PCProto::SMMap::get_key(a))));
        const PCInst& bstate(
            PCInstMap::get_value(
                PCProto::SMMap::get_value(b).get_inst_map().find_checked(PCProto::SMMap::get_key(b))));
        return (astate.get_to_seq() < bstate.get_to_seq());
    }
};


// Convenience
static int64_t get_max_to_seq(const PCProto::SMMap& states)
{
    gcomm_assert(states.empty() == false);
    PCProto::SMMap::const_iterator max_i(
        max_element(states.begin(), states.end(), ToSeqCmpOp()));
    const PCInst& state(PCProto::SMMap::get_value(max_i).get_inst(PCProto::SMMap::get_key(max_i)));
    return state.get_to_seq();
}


// Validate state message agains local state
void PCProto::validate_state_msgs() const
{
    const int64_t max_to_seq(get_max_to_seq(state_msgs));
    
    for (SMMap::const_iterator i = state_msgs.begin(); i != state_msgs.end();
         ++i)
    {
        const UUID& msg_source_uuid(SMMap::get_key(i));
        const PCInst& msg_source_state(SMMap::get_value(i).get_inst(msg_source_uuid));
        
        const PCInstMap& msg_state_map(SMMap::get_value(i).get_inst_map());
        for (PCInstMap::const_iterator si = msg_state_map.begin(); 
             si != msg_state_map.end(); ++si)
        {
            const UUID& uuid(PCInstMap::get_key(si));
            const PCInst& msg_state(PCInstMap::get_value(si));
            const PCInst& local_state(PCInstMap::get_value(instances.find_checked(uuid)));
            if (get_prim() == true && msg_source_state.get_prim() &&
                msg_state.get_prim() == true)
            {
                // Msg source claims to come from prim view and this node
                // is in prim. All message prim view states must be equal
                // to local ones.
                gcomm_assert(msg_state == local_state)
                    << self_string()
                    << " node " << uuid
                    << " prim state message and local states not consistent:"
                    << " msg node "   << msg_state
                    << " local state " << local_state;
                gcomm_assert(msg_state.get_to_seq() == max_to_seq)
                    << self_string()
                    << " node " << uuid
                    << " to seq not consistent with local state:"
                    << " max to seq " << max_to_seq
                    << " msg state to seq " << msg_state.get_to_seq();
            }
            else if (get_prim() == true)
            {
                log_debug << self_string()
                          << " node " << uuid 
                          << " from " << msg_state.get_last_prim()
                          << " joining " << get_last_prim();
            }
            else if (msg_state.get_prim() == true)
            {
                // @todo: Cross check with other state messages coming from prim
                log_debug << self_string()
                          << " joining to " << msg_state.get_last_prim();
            }
        }
    }
}


// @note This method is currently for sanity checking only. RTR is not 
// implemented yet.
bool PCProto::requires_rtr() const
{
    bool ret = false;
    
    // Find maximum reported to_seq
    const int64_t max_to_seq(get_max_to_seq(state_msgs));
    
    for (SMMap::const_iterator i = state_msgs.begin(); i != state_msgs.end(); 
         ++i)
    {
        PCInstMap::const_iterator ii(
            SMMap::get_value(i).get_inst_map().find_checked(SMMap::get_key(i)));
        
        
        const PCInst& inst      = PCInstMap::get_value(ii);
        const int64_t to_seq    = inst.get_to_seq();
        const ViewId  last_prim = inst.get_last_prim();
        
        if (to_seq                 != -1         && 
            to_seq                 != max_to_seq && 
            last_prim.get_type()   != V_NON_PRIM)
        {
            log_warn << self_string() << " RTR is needed: " << to_seq
                     << " / " << last_prim;
            ret = true;
        }
    }
    
    return ret;
}


void PCProto::cleanup_instances()
{
    gcomm_assert(get_state() == S_PRIM);
    gcomm_assert(current_view.get_type() == V_REG);
    
    PCInstMap::iterator i, i_next;
    for (i = instances.begin(); i != instances.end(); i = i_next)
    {
        i_next = i, ++i_next;
        const UUID& uuid(PCInstMap::get_key(i));
        if (current_view.is_member(uuid) == false) 
        {
            log_info << self_string()
                     << " cleaning up instance " << uuid;
            instances.erase(i);
        }
    }
}


bool PCProto::is_prim() const
{
    bool prim = false;
    ViewId last_prim(V_NON_PRIM);
    int64_t to_seq = -1;
    
    // Check if any of instances claims to come from prim view
    for (SMMap::const_iterator i = state_msgs.begin(); i != state_msgs.end();
         ++i)
    {
        const PCInst& state(SMMap::get_value(i).get_inst(SMMap::get_key(i)));
        
        if (state.get_prim() == true)
        {
            prim      = true;
            last_prim = state.get_last_prim();
            to_seq    = state.get_to_seq();
            break;
        }
    }
    
    // Verify that all members are either coming from the same prim 
    // view or from non-prim
    for (SMMap::const_iterator i = state_msgs.begin(); i != state_msgs.end();
         ++i)
    {
        const PCInst& state(SMMap::get_value(i).get_inst(SMMap::get_key(i)));
        
        if (state.get_prim() == true)
        {
            if (state.get_last_prim() != last_prim)
            {
                gcomm_throw_fatal 
                    << self_string()
                    << " last prims not consistent";
            }
            
            if (state.get_to_seq() != to_seq)
            {
                gcomm_throw_fatal 
                    << self_string()
                    << " TO seqs not consistent";
            }
        }
        else
        {
            log_info << "Non-prim " << SMMap::get_key(i) <<" from "
                     << state.get_last_prim() << " joining prim";
        }
    }
    
    // No members coming from prim view, check if last known prim 
    // view can be recovered (majority of members from last prim alive)
    if (prim == false)
    {
        gcomm_assert(last_prim == ViewId(V_NON_PRIM)) 
            << last_prim << " != " << ViewId(V_NON_PRIM);
        
        MultiMap<ViewId, UUID> last_prim_uuids;
        
        for (SMMap::const_iterator i = state_msgs.begin(); 
             i != state_msgs.end();
             ++i)
        {
            for (PCInstMap::const_iterator 
                     j = SMMap::get_value(i).get_inst_map().begin(); 
                 j != SMMap::get_value(i).get_inst_map().end(); ++j)
            {
                const UUID& uuid(PCInstMap::get_key(j));
                const PCInst& inst(PCInstMap::get_value(j));
                
                if (inst.get_last_prim() != ViewId(V_NON_PRIM) &&
                    find<MultiMap<ViewId, UUID>::iterator,
                    pair<const ViewId, UUID> >(last_prim_uuids.begin(), 
                                               last_prim_uuids.end(),
                                               make_pair(inst.get_last_prim(), uuid)) == 
                    last_prim_uuids.end())
                {
                    last_prim_uuids.insert(make_pair(inst.get_last_prim(), uuid));
                }
            }
        }
        
        if (last_prim_uuids.empty() == true)
        {
            log_warn << "no nodes coming from prim view, prim not possible";
            return false;
        }
        
        const ViewId greatest_view_id(last_prim_uuids.rbegin()->first);
        set<UUID> greatest_view;
        pair<MultiMap<ViewId, UUID>::const_iterator, 
            MultiMap<ViewId, UUID>::const_iterator> gvi =
            last_prim_uuids.equal_range(greatest_view_id);
        for (MultiMap<ViewId, UUID>::const_iterator i = gvi.first;
             i != gvi.second; ++i)
        {
            pair<set<UUID>::iterator, bool> iret = greatest_view.insert(
                MultiMap<ViewId, UUID>::get_value(i));
            gcomm_assert(iret.second == true);
        }
        log_debug << self_string()
                  << " greatest view id " << greatest_view_id;
        set<UUID> present;
        for (NodeList::const_iterator i = current_view.get_members().begin();
             i != current_view.get_members().end(); ++i)
        {
            present.insert(NodeList::get_key(i));
        }
        set<UUID> intersection;
        set_intersection(greatest_view.begin(), greatest_view.end(),
                         present.begin(), present.end(),
                         inserter(intersection, intersection.begin()));
        log_info << self_string()
                 << " intersection size " << intersection.size()
                 << " greatest view size " << greatest_view.size();
        if (intersection.size()*2 > greatest_view.size())
        {
            prim = true;
        }
    }
    
    return prim;
}

void PCProto::handle_state(const PCMessage& msg, const UUID& source)
{
    gcomm_assert(msg.get_type() == PCMessage::T_STATE);
    gcomm_assert(get_state() == S_STATES_EXCH);
    gcomm_assert(state_msgs.size() < current_view.get_members().size());
    
    log_info << self_string() << " handle state from " << source << " " << msg;
    
    // Early check for possibly conflicting primary components. The one 
    // with greater view id may continue (as it probably has been around
    // for longer timer). However, this should be configurable policy.
    if (get_prim() == true)
    {
        const PCInst& si(PCInstMap::get_value(msg.get_inst_map().find(source)));
        if (si.get_prim() == true && si.get_last_prim() != get_last_prim())
        {
            log_warn << self_string() << " conflicting prims: my prim " 
                     << get_last_prim() 
                     << " other prim: " 
                     << si.get_last_prim();
            
            if (get_last_prim() < si.get_last_prim())
            {
                log_warn << "discarding other";
                return;
            }
            else
            {
                gcomm_throw_fatal << self_string()
                                  << " aborting due to conflicting prims";
            }
        }
    }
    
    state_msgs.insert_checked(make_pair(source, msg));
    
    if (state_msgs.size() == current_view.get_members().size())
    {
        // Insert states from previously unseen nodes into local state map
        for (SMMap::const_iterator i = state_msgs.begin(); 
             i != state_msgs.end(); ++i)
        {
            const UUID& sm_uuid(SMMap::get_key(i));
            if (instances.find(sm_uuid) == instances.end())
            {
                const PCInst& sm_state(SMMap::get_value(i).get_inst(sm_uuid));
                instances.insert_checked(make_pair(sm_uuid, sm_state));
            }
        }
        
        // Validate that all state messages are consistent before proceeding
        validate_state_msgs();
        
        if (is_prim() == true)
        {
            // @note Requires RTR does not actually have effect, but let it 
            // be for debugging purposes until a while
            (void)requires_rtr();
            shift_to(S_INSTALL);
            
            if (current_view.get_members().find(get_uuid()) ==
                current_view.get_members().begin())
            {
                send_install();
            }
        }
        else 
        {
            const bool was_prim(get_prim());
            shift_to(S_NON_PRIM);
            if (was_prim == true)
            {
                deliver_view();
            }
        }
    }
}

void PCProto::handle_install(const PCMessage& msg, const UUID& source)
{
    gcomm_assert(msg.get_type() == PCMessage::T_INSTALL);
    gcomm_assert(get_state()    == S_INSTALL);
    
    log_info << self_string() 
             << " handle install from " << source << " " << msg;
    
    // Validate own state
    
    PCInstMap::const_iterator mi(msg.get_inst_map().find_checked(get_uuid()));
    
    const PCInst& m_state(PCInstMap::get_value(mi));
    
    if (m_state != PCInstMap::get_value(self_i))
    {
        gcomm_throw_fatal << self_string()
                          << "Install message self state does not match, "
                          << "message state: " << m_state
                          << ", local state: "
                          << PCInstMap::get_value(self_i);
    }
    
    // Set TO seqno according to install message
    int64_t to_seq = -1;
    
    for (mi = msg.get_inst_map().begin(); mi != msg.get_inst_map().end(); ++mi)
    {
        const PCInst& m_state = PCInstMap::get_value(mi);
        
        if (m_state.get_prim() == true && to_seq != -1)
        {
            if (m_state.get_to_seq() != to_seq)
            {
                gcomm_throw_fatal << "Install message TO seqno inconsistent";
            }
        }
        
        if (m_state.get_prim() == true)
        {
            to_seq = max(to_seq, m_state.get_to_seq());
        }
    }
    
    log_info << self_string() << " setting TO seq to " << to_seq;
    
    set_to_seq(to_seq);
    
    shift_to(S_PRIM);
    deliver_view();
    cleanup_instances();
}


void PCProto::handle_user(const PCMessage& msg, const Datagram& dg,
                          const ProtoUpMeta& um)
{
    int64_t to_seq(-1);

    if (get_prim() == true)
    {
        set_to_seq(get_to_seq() + 1);
        to_seq = get_to_seq();
    }
    else if (current_view.get_members().find(um.get_source()) ==
             current_view.get_members().end())
    {
        gcomm_assert(current_view.get_type() == V_TRANS);
        // log_debug << self_string()
        //        << " dropping message from out of view source in non-prim";
        return;
    }
    
    
    PCInst& state(PCInstMap::get_value(instances.find_checked(um.get_source())));
    state.set_last_seq(msg.get_seq());
    
    Datagram up_dg(dg, dg.get_offset() + msg.serial_size());
    gu_trace(send_up(up_dg, 
                     ProtoUpMeta(um.get_source(), 
                                 pc_view.get_id(), 
                                 0,
                                 um.get_user_type(), 
                                 to_seq)));
}


void PCProto::handle_msg(const PCMessage&   msg, 
                         const Datagram&    rb, 
                         const ProtoUpMeta& um)
{
    enum Verdict
    {
        ACCEPT,
        DROP,
        FAIL
    };
    
    static const Verdict verdicts[S_MAX][PCMessage::T_MAX] = {
        // Msg types
        // NONE,   STATE,   INSTALL,  USER
        {  FAIL,   FAIL,    FAIL,     FAIL    },  // Closed

        {  FAIL,   FAIL,    FAIL,     FAIL    },  // Joining
        
        {  FAIL,   ACCEPT,  FAIL,     FAIL    },  // States exch

        {  FAIL,   FAIL,    ACCEPT,   FAIL    },  // INSTALL
        
        {  FAIL,   FAIL,    FAIL,     ACCEPT  },  // PRIM

        {  FAIL,   DROP,    DROP,     ACCEPT  },  // TRANS
        
        {  FAIL,   ACCEPT,  FAIL,     ACCEPT  }   // NON-PRIM
    };
    
    PCMessage::Type msg_type = msg.get_type();
    Verdict         verdict  = verdicts[get_state()][msg.get_type()];
    
    if (verdict == FAIL)
    {
        gcomm_throw_fatal << "Invalid input, message " << msg.to_string()
                          << " in state " << to_string(get_state());
    }
    else if (verdict == DROP)
    {
        log_warn << "Dropping input, message " << msg.to_string()
                 << " in state " << to_string(get_state());
        return;
    }
    
    switch (msg_type)
    {
    case PCMessage::T_STATE:
        gu_trace(handle_state(msg, um.get_source()));
        break;
    case PCMessage::T_INSTALL:
        gu_trace(handle_install(msg, um.get_source()));
        break;
    case PCMessage::T_USER:
        gu_trace(handle_user(msg, rb, um));
        break;
    default:
        gcomm_throw_fatal << "Invalid message";
    }
}


void PCProto::handle_up(int cid, const Datagram& rb,
                        const ProtoUpMeta& um)
{
    if (um.has_view() == true)
    {
        handle_view(um.get_view());
    }
    else
    {
        PCMessage msg;
        
        if (msg.unserialize(&rb.get_payload()[0], rb.get_payload().size(), 
                            rb.get_offset()) == 0)
        {
            gcomm_throw_fatal << "Could not read message";
        }
        
        handle_msg(msg, rb, um);
    }
}


int PCProto::handle_down(const Datagram& wb, const ProtoDownMeta& dm)
{
    if (get_state() != S_PRIM)
    {
        return EAGAIN;
    }
    
    uint32_t      seq = get_last_seq() + 1;
    PCUserMessage um(seq);
    Datagram down_dg(wb);

    push_header(um, down_dg);
    
    int ret;
    // If number of nodes is less than 3 we can send messages in agreed order
    // since in case of crash we enter NON_PRIM anyway
    if (current_view.get_members().size() < 3)
    {
        ret = send_down(down_dg, ProtoDownMeta(dm.get_user_type(), SP_AGREED));
    }
    else
    {
        ret = send_down(down_dg, dm);
    }
    if (ret == 0)
    {
        set_last_seq(seq);
    }
    else if (ret != EAGAIN)
    {
        log_warn << "PCProto::handle_down: " << strerror(ret);
    }

    return ret;
}


