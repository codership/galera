
#include "pc_proto.hpp"
#include "pc_message.hpp"
#include "gcomm/logger.hpp"

#include <set>

using std::multiset;
using std::string;

BEGIN_GCOMM_NAMESPACE

static const PCInst& get_state(PCProto::SMMap::const_iterator smap_i,
                               const UUID& uuid)
{
    PCInstMap::const_iterator i =
        PCProto::SMMap::get_instance(smap_i).get_inst_map().find(uuid);

    if (i == PCProto::SMMap::get_instance(smap_i).get_inst_map().end())
    {
        gcomm_throw_fatal << "UUID: " << uuid.to_string()
                          << " not found in the instance map";
    }

    return PCInstMap::get_instance(i);
}

void PCProto::send_state()
{
    log_info << self_string() << " sending state";

    PCStateMessage pcs;

    PCInstMap& im(pcs.get_inst_map());

    for (PCInstMap::const_iterator i = instances.begin(); i != instances.end();
         ++i)
    {
        im.insert(std::make_pair(PCInstMap::get_uuid(i), 
                                 PCInstMap::get_instance(i)));
    }

    Buffer buf(pcs.size());

    if (pcs.write(buf.get_buf(), buf.get_len(), 0) == 0)
    {
        gcomm_throw_fatal << "Failed to serialize state";
    }

    WriteBuf wb(buf.get_buf(), buf.get_len());

    if (pass_down(&wb, 0))
    {
        gcomm_throw_fatal << "pass down failed";
    }    
}

void PCProto::send_install()
{
    log_info << self_string() << " send install";
    
    PCInstallMessage pci;

    PCInstMap& im(pci.get_inst_map());

    for (SMMap::const_iterator i = state_msgs.begin(); i != state_msgs.end();
         ++i)
    {
        if (current_view.get_members().find(SMMap::get_uuid(i)) != 
            current_view.get_members().end()
            && im.insert(std::make_pair(
                             SMMap::get_uuid(i), 
                             gcomm::get_state(i, SMMap::get_uuid(i)))).second
            == false)
        {
            gcomm_throw_fatal << "Insert into instance map failed";
        }
    }

    Buffer buf(pci.size());

    if (pci.write(buf.get_buf(), buf.get_len(), 0) == 0)
    {
        gcomm_throw_fatal << "PCInstallMessage serialization failed";
    }

    WriteBuf wb(buf.get_buf(), buf.get_len());

    if (pass_down(&wb, 0))
    {
        gcomm_throw_fatal << "pass_down failed";
    }
}


void PCProto::deliver_view()
{
    View v(get_prim() == true ? View::V_PRIM : View::V_NON_PRIM,
           current_view.get_id());

    v.add_members(current_view.get_members().begin(), 
                  current_view.get_members().end());

    for (PCInstMap::const_iterator i = instances.begin(); 
         i != instances.end(); ++i)
    {
        if (current_view.get_members().find(PCInstMap::get_uuid(i)) ==
            current_view.get_members().end())
        {
            v.add_partitioned(PCInstMap::get_uuid(i), "");
        }
    }

    ProtoUpMeta um(&v);

    pass_up(0, 0, &um);
}

void PCProto::shift_to(const State s)
{
    // State graph
    static const bool allowed[S_MAX][S_MAX] = {
        
        // Closed
        { false, true, false,  false, false, false, false },
        // Joining
        { true,  false, true,  false, false, false, false },
        // States exch
        { true,  false, false, true,  false, true,  true  },
        // Install
        { true,  false, false, false, true,  true,  true  },
        // Prim
        { true,  false, false, false, false, true,  true  },
        // Trans
        { true,  false, true,  false, false, false, false },
        // Non-prim
        { true,  false, true,  false, false, false, true  }
    };
    
    LOG_INFO(self_string() + " shift_to: " + to_string(get_state()) + " -> " 
             + to_string(s));
    
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
        set_last_prim(current_view.get_id());
        set_prim(true);
        break;
    case S_TRANS:
        break;
    case S_NON_PRIM:
        set_prim(false);
        break;
    default:
        ;
    }
    state = s;
}


void PCProto::handle_first_trans(const View& view)
{
    assert(view.get_type() == View::V_TRANS);

    if (start_prim == true)
    {
        if (view.get_members().length() > 1 || view.is_empty())
        {
            gcomm_throw_fatal << "Corrupted view";
        }

        if (get_uuid(view.get_members().begin()) != uuid)
        {
            gcomm_throw_fatal << "Bad first UUID: "
                              <<get_uuid(view.get_members().begin()).to_string()
                              << ", expected: " << uuid.to_string();
        }

        set_last_prim(view.get_id());
        set_prim(true);
    }
}

void PCProto::handle_trans(const View& view)
{
    assert(view.get_type() == View::V_TRANS);
    assert(view.get_id()   == current_view.get_id());

    log_info << "Handle trans, current: " << current_view.get_id().to_string()
             << ", new: " << view.get_id().to_string();

    if (view.get_id() == get_last_prim())
    {
        if (view.get_members().length()*2 + view.get_left().length() <=
            current_view.get_members().length())
        {
            shift_to(S_NON_PRIM);
            return;
        }
    }
    else
    {
        if (get_last_prim() != ViewId())
        {
            log_warn << "Trans view during " + to_string(get_state());
        }
    }

    if (get_state() != S_NON_PRIM)
    {
        shift_to(S_TRANS);
    }
}

void PCProto::handle_first_reg(const View& view)
{
    assert(view.get_type() == View::V_REG);
    assert(get_state() == S_JOINING);
    
    if (start_prim == true)
    {
        if (view.get_members().length() > 1 || view.is_empty())
        {
            gcomm_throw_fatal << self_string() << " starting primary "
                              <<"but first reg view is not singleton";
        }
    }

    if (view.get_id().get_seq() <= current_view.get_id().get_seq())
    {
        gcomm_throw_fatal << "Non-increasing view ids: current view " 
                          << current_view.get_id().to_string() 
                          << " new view "
                          << view.get_id().to_string();
    }

    current_view = view;
    views.push_back(current_view);
    shift_to(S_STATES_EXCH);
    send_state();
}

void PCProto::handle_reg(const View& view)
{
    assert(view.get_type() == View::V_REG);
    
    if (view.is_empty() == false && 
        view.get_id().get_seq() <= current_view.get_id().get_seq())
    {
        gcomm_throw_fatal << "Non-increasing view ids: current view " 
                          << current_view.get_id().to_string() 
                          << " new view "
                          << view.get_id().to_string();
    }
    
    current_view = view;
    views.push_back(current_view);

    if (current_view.is_empty())
    {
        set_prim(false);
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
    if (view.get_type() != View::V_TRANS && view.get_type() != View::V_REG)
    {
        gcomm_throw_fatal << "Invalid view type";
    }
    
    // Make sure that self exists in view
    if (view.is_empty() == false &&
        view.get_members().find(uuid) == view.get_members().end())
    {
        gcomm_throw_fatal << "Self not found from non empty view: "
                          << view.to_string();
    }
    
    if (view.get_type() == View::V_TRANS)
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

bool PCProto::requires_rtr() const
{
    int64_t max_to_seq = -1;

    // find max seqno. @todo: can't we have it as a property of instance map?
    for (SMMap::const_iterator i = state_msgs.begin();
         i != state_msgs.end(); ++i)
    {
        PCInstMap::const_iterator ii =
            SMMap::get_instance(i).get_inst_map().find(SMMap::get_uuid(i));

        if (ii == SMMap::get_instance(i).get_inst_map().end())
        {
            gcomm_throw_fatal << "Internal logic error";
        }

        const PCInst& inst = PCInstMap::get_instance(ii);

        max_to_seq = std::max(max_to_seq, inst.get_to_seq());
    }

    for (SMMap::const_iterator i = state_msgs.begin();
         i != state_msgs.end(); ++i)
    {
        PCInstMap::const_iterator ii =
            SMMap::get_instance(i).get_inst_map().find(SMMap::get_uuid(i));

        if (ii == SMMap::get_instance(i).get_inst_map().end())
        {
            gcomm_throw_fatal << "Internal logic error 2";
        }

        const PCInst& inst      = PCInstMap::get_instance(ii);
        const int64_t to_seq    = inst.get_to_seq();
        const ViewId  last_prim = inst.get_last_prim();

        if (to_seq != -1 && to_seq != max_to_seq && last_prim != ViewId())
        {
            log_info << self_string() << " RTR is needed: " << to_seq
                     << " / " << last_prim.to_string();

            return true;
        }
    }

    return false;
}

void PCProto::validate_state_msgs() const
{
    // TODO:
}

bool PCProto::is_prim() const
{
    bool prim = false;
    ViewId last_prim;
    int64_t to_seq = -1;

    // Check if any of instances claims to come from prim view
    for (SMMap::const_iterator i = state_msgs.begin(); i != state_msgs.end();
         ++i)
    {
        const PCInst& i_state(gcomm::get_state(i, SMMap::get_uuid(i)));

        if (i_state.get_prim() == true)
        {
            prim      = true;
            last_prim = i_state.get_last_prim();
            to_seq    = i_state.get_to_seq();
            break;
        }
    }
    
    // Verify that all members are either coming from the same prim 
    // view or from non-prim
    for (SMMap::const_iterator i = state_msgs.begin(); i != state_msgs.end();
         ++i)
    {
        const PCInst& i_state(gcomm::get_state(i, SMMap::get_uuid(i)));

        if (i_state.get_prim() == true)
        {
            if (i_state.get_last_prim() != last_prim)
            {
                gcomm_throw_fatal << "Last prims not consistent";
            }

            if (i_state.get_to_seq() != to_seq)
            {
                gcomm_throw_fatal << "TO seqs not consistent";
            }
        }
        else
        {
            log_info << "Non-prim " << SMMap::get_uuid(i).to_string() <<" from "
                     << i_state.get_last_prim().to_string() << " joining prim";
        }
    }
    
    // No members coming from prim view, check if last known prim 
    // view can be recovered (all members from last prim alive)
    if (prim == false)
    {
        assert(last_prim == ViewId());

        multiset<ViewId> vset;

        for (SMMap::const_iterator i = state_msgs.begin(); 
             i != state_msgs.end();
             ++i)
        {
            const PCInst& i_state(gcomm::get_state(i, SMMap::get_uuid(i)));

            if (i_state.get_last_prim() != ViewId())
            {
                vset.insert(i_state.get_last_prim());
            }
        }

        uint32_t max_view_seq   = 0;
        size_t  great_view_len = 0;
        ViewId  great_view;
        
        for (multiset<ViewId>::const_iterator vi = vset.begin();
             vi != vset.end(); vi = vset.upper_bound(*vi))
        {
            max_view_seq = std::max(max_view_seq, vi->get_seq());

            const size_t vsc = vset.count(*vi);

            if (vsc >= great_view_len && vi->get_seq() >= great_view.get_seq())
            {
                great_view_len = vsc;
                great_view = *vi;
            }
        }

        if (great_view.get_seq() == max_view_seq)
        {
            list<View>::const_iterator lvi;

            for (lvi = views.begin(); lvi != views.end(); ++lvi)
            {
                if (lvi->get_id() == great_view) break;
            }

            if (lvi == views.end())
            {
                gcomm_throw_fatal << "View not found from list";
            }

            if (lvi->get_members().length() == great_view_len)
            {
                log_info << "Found common last prim view";

                prim = true;

                for (SMMap::const_iterator j = state_msgs.begin();
                     j != state_msgs.end(); ++j)
                {
                    const PCInst& j_state(gcomm::get_state(j,
                                                           SMMap::get_uuid(j)));

                    if (j_state.get_last_prim() == great_view)
                    {
                        if (to_seq == -1)
                        {
                            to_seq = j_state.get_to_seq();
                        }
                        else if (j_state.get_to_seq() != to_seq)
                        {
                            gcomm_throw_fatal << "Conflicting to_seq";
                        }
                    }
                }
            }
        }
        else
        {
            gcomm_throw_fatal << "Max view seq " << max_view_seq
                              << " not equal to seq of greatest views "
                              << great_view.get_seq()
                              <<  ", don't know how to recover";
        }
    }
    
    return prim;
}

void PCProto::handle_state(const PCMessage& msg, const UUID& source)
{
    assert(msg.get_type() == PCMessage::T_STATE);
    assert(get_state() == S_STATES_EXCH);
    assert(state_msgs.length() < current_view.get_members().length());

    log_info << self_string() << " handle state: " << msg.to_string();

    if (get_prim() == true)
    {
        const PCInst& si = PCInstMap::get_instance(msg.get_inst_map().find(source));
        if (si.get_prim() == true && si.get_last_prim() != get_last_prim())
        {
            log_warn << self_string() << " conflicting prims: my prim " 
                     << get_last_prim().to_string() 
                     << " other prim: " 
                     << si.get_last_prim().to_string();

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
    
    if (state_msgs.insert(std::make_pair(source, msg)).second == false)
    {
        gcomm_throw_fatal << "Failed to save state message";
    }
    
    if (state_msgs.length() == current_view.get_members().length())
    {
        for (SMMap::const_iterator i = state_msgs.begin(); 
             i != state_msgs.end();
             ++i)
        {
            const UUID& sm_uuid = SMMap::get_uuid(i);

            if (instances.find(sm_uuid) == instances.end())
            {
                if (instances.insert(
                        std::make_pair(sm_uuid,
                                       gcomm::get_state(i, sm_uuid))).second
                    == false)
                {
                    gcomm_throw_fatal
                        << "Failed to add state message source to instance map";
                }
            }
        }

        validate_state_msgs();

        if (is_prim())
        {
            // Requires RTR does not actually have effect, but let it 
            // be for debugging purposes until a while
            (void)requires_rtr();
            shift_to(S_INSTALL);

            if (current_view.get_members().find(uuid) ==
                current_view.get_members().begin())
            {
                send_install();
            }
        }
        else
        {
            shift_to(S_NON_PRIM);
            deliver_view();
        }
    }
}

void PCProto::handle_install(const PCMessage& msg, const UUID& source)
{
    assert(msg.get_type() == PCMessage::T_INSTALL);
    assert(get_state()    == S_INSTALL);

    log_info << self_string() << " handle install: " << msg.to_string();
    
    // Validate own state

    PCInstMap::const_iterator mi = msg.get_inst_map().find(uuid);

    if (mi == msg.get_inst_map().end())
    {
        gcomm_throw_fatal << "No self in the PCMessage instance map";
    }

    const PCInst& m_state = PCInstMap::get_instance(mi);

    if (m_state.get_prim()      != get_prim()      ||
        m_state.get_last_prim() != get_last_prim() ||
        m_state.get_to_seq()    != get_to_seq()    ||
        m_state.get_last_seq()  != get_last_seq())
    {
        gcomm_throw_fatal << self_string()
                          << "Install message self state does not match, "
                          << "message state: " << m_state.to_string()
                          << ", local state: "
                          << PCInstMap::get_instance(self_i).to_string();
    }
    
    // Set TO seqno according to state message
    int64_t to_seq = -1;
    
    for (mi = msg.get_inst_map().begin(); mi != msg.get_inst_map().end(); ++mi)
    {
        const PCInst& m_state = PCInstMap::get_instance(mi);

        if (m_state.get_prim() == true && to_seq != -1)
        {
            if (m_state.get_to_seq() != to_seq)
            {
                gcomm_throw_fatal << "Install message TO seqno inconsistent";
            }
        }

        if (m_state.get_prim() == true)
        {
            to_seq = std::max(to_seq, m_state.get_to_seq());
        }
    }
    
    log_info << self_string() << " setting TO seq to " << to_seq;

    set_to_seq(to_seq);
    
    shift_to(S_PRIM);
    deliver_view();
}

void PCProto::handle_user(const PCMessage& msg, const ReadBuf* rb,
                          const size_t roff, const ProtoUpMeta* um)
{
    int64_t to_seq = -1;

    if (get_state() == S_PRIM)
    {
        set_to_seq(get_to_seq() + 1);
        to_seq = get_to_seq();
    }

    ProtoUpMeta pum(um->get_source(), um->get_user_type(), to_seq);

    pass_up(rb, roff + msg.size(), &pum);
}

void PCProto::handle_msg(const PCMessage&   msg, 
                         const ReadBuf*     rb, 
                         const size_t       roff, 
                         const ProtoUpMeta* um)
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
        handle_state(msg, um->get_source());
        break;
    case PCMessage::T_INSTALL:
        handle_install(msg, um->get_source());
        break;
    case PCMessage::T_USER:
        handle_user(msg, rb, roff, um);
        break;
    default:
        gcomm_throw_fatal << "Invalid message";
    }
}

void PCProto::handle_up(const int cid, const ReadBuf* rb, const size_t roff,
                        const ProtoUpMeta* um)
{
    const View* v = um->get_view();

    if (v)
    {
        handle_view(*v);
    }
    else
    {
        PCMessage msg;

        if (msg.read(rb->get_buf(), rb->get_len(), roff) == 0)
        {
            gcomm_throw_fatal << "Could not read message";
        }

        handle_msg(msg, rb, roff, um);
    }
}

int PCProto::handle_down(WriteBuf* wb, const ProtoDownMeta* dm)
{
    if (get_state() != S_PRIM)
    {
        return EAGAIN;
    }
    
    uint32_t      seq = get_last_seq() + 1;
    PCUserMessage um(seq);
    byte_t        buf[8];

    if (um.write(buf, sizeof(buf), 0) == 0)
    {
        gcomm_throw_fatal << "Short buffer";
    }

    wb->prepend_hdr(buf, um.size());

    int ret = pass_down(wb, dm);

    wb->rollback_hdr(um.size());

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

END_GCOMM_NAMESPACE
