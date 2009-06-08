


#include "pc_proto.hpp"

#include "pc_message.hpp"

#include "gcomm/logger.hpp"

BEGIN_GCOMM_NAMESPACE


void PCProto::send_state()
{
    LOG_INFO(self_string() + " sending state");
    PCStateMessage pcs;
    PCInstMap& im(pcs.get_inst_map());
    for (PCInstMap::const_iterator i = instances.begin(); i != instances.end();
         ++i)
    {
        im.insert(make_pair(PCInstMap::get_uuid(i), 
                            PCInstMap::get_instance(i)));
    }
    Buffer buf(pcs.size());
    if (pcs.write(buf.get_buf(), buf.get_len(), 0) == 0)
    {
        throw FatalException("");
    }
    WriteBuf wb(buf.get_buf(), buf.get_len());
    if (pass_down(&wb, 0))
    {
        throw FatalException("todo");
    }
    
}

void PCProto::send_install()
{
    LOG_INFO(self_string() + " send install");

    PCInstallMessage pci;
    PCInstMap& im(pci.get_inst_map());
    for (PCInstMap::const_iterator i = instances.begin(); i != instances.end();
         ++i)
    {
        if (current_view.get_members().find(PCInstMap::get_uuid(i)) != 
            current_view.get_members().end()
            && im.insert(make_pair(PCInstMap::get_uuid(i), 
                                   PCInstMap::get_instance(i))).second == false)
        {
            throw FatalException("");
        }
    }
    Buffer buf(pci.size());
    if (pci.write(buf.get_buf(), buf.get_len(), 0) == 0)
    {
        throw FatalException("");
    }
    WriteBuf wb(buf.get_buf(), buf.get_len());
    if (pass_down(&wb, 0))
    {
        throw FatalException("");
    }
}


void PCProto::deliver_view()
{
    View v(get_prim() == true ? View::V_PRIM : View::V_NON_PRIM,
           current_view.get_id());
    v.add_members(current_view.get_members().begin(), 
                  current_view.get_members().end());
    ProtoUpMeta um(&v);
    pass_up(0, 0, &um);
}

void PCProto::shift_to(const State s)
{
    // State graph
    static const bool allowed[S_MAX][S_MAX] = {
        {false, true, false, false, false, false},
        {true, false, true, false, false, false},
        {true, false, false, true, false, false},
        {true, false, true, false, true, true},
        {true, false, true, false, false, true},
        {true, false, true, false, false, false}
    };

    LOG_INFO(self_string() + " shift_to: " + to_string(get_state()) + " -> " 
             + to_string(s));
 
    if (allowed[get_state()][s] == false)
    {
        LOG_INFO("invalid state transtion: " + to_string(get_state()) + " -> " 
                 + to_string(s));
        throw FatalException("invalid state transition");
    }
    switch (s)
    {
    case S_CLOSED:
        break;
    case S_JOINING:
        break;
    case S_STATES_EXCH:
        state_msgs.clear();
        send_state();
        break;
    case S_RTR:
        break;
    case S_PRIM:
        set_last_prim(current_view.get_id());
        set_prim(true);
        deliver_view();
        break;
    case S_NON_PRIM:
        set_prim(false);
        deliver_view();
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
            throw FatalException("");
        }
        if (get_uuid(view.get_members().begin()) != uuid)
        {
            throw FatalException("");
        }
        set_last_prim(view.get_id());
        set_prim(true);
    }
    else
    {

    }
}

void PCProto::handle_trans(const View& view)
{
    assert(view.get_type() == View::V_TRANS);
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
            throw FatalException("partition-merge not supported");
        }
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
            LOG_FATAL(self_string() + " starting primary but first reg view is not singleton");
            throw FatalException("");
        }
    }
    if (view.get_id().get_seq() <= current_view.get_id().get_seq())
    {
        LOG_FATAL("decreasing view ids: current view " 
                  + current_view.get_id().to_string() 
                  + " new view "
                  + view.get_id().to_string());
        
        throw FatalException("decreasing view ids");
    }

    current_view = view;
    views.push_back(current_view);
    shift_to(S_STATES_EXCH);
}

void PCProto::handle_reg(const View& view)
{
    assert(view.get_type() == View::V_REG);
    
    if (view.get_id().get_seq() <= current_view.get_id().get_seq())
    {
        LOG_FATAL("decreasing view ids: current view " 
                  + current_view.get_id().to_string() 
                  + " new view "
                  + view.get_id().to_string());
        
        throw FatalException("decreasing view ids");
    }

    current_view = view;
    views.push_back(current_view);
    shift_to(S_STATES_EXCH);
}


void PCProto::handle_view(const View& view)
{
    assert(get_state() == S_JOINING || 
           get_state() == S_PRIM ||
           get_state() == S_NON_PRIM);
    // We accept only EVS TRANS and REG views
    if (view.get_type() != View::V_TRANS && view.get_type() != View::V_REG)
    {
        LOG_FATAL("invalid view type");
        throw FatalException("invalid view type");
    }
    
    // Make sure that self exists in view
    if (view.get_members().find(uuid) == view.get_members().end())
    {
        LOG_FATAL("self not found from view: " + view.to_string());
        throw FatalException("self not found from view");
    }
    
    
    if (view.get_type() == View::V_TRANS)
    {
        if (get_state() == S_JOINING)
        {
            handle_first_trans(view);
        }
    }
    else
    {
        if (get_state() != S_JOINING)
        {
            handle_reg(view);
        }
        else
        {
            handle_first_reg(view);
        }  
    }
}

bool PCProto::requires_rtr() const
{
    int64_t max_to_seq = -1;
    for (SMMap::const_iterator i = state_msgs.begin();
         i != state_msgs.end(); ++i)
    {
        PCInstMap::const_iterator ii = SMMap::get_instance(i).get_inst_map().find(SMMap::get_uuid(i));
        if (ii == SMMap::get_instance(i).get_inst_map().end())
        {
            throw FatalException("");
        }
        const PCInst& inst = PCInstMap::get_instance(ii);
        const int64_t to_seq = inst.get_to_seq();
        const ViewId last_prim = inst.get_last_prim();
        if (to_seq != -1 && to_seq != max_to_seq && last_prim != ViewId())
        {
            return true;
        }
    }
    return false;
}

void PCProto::validate_state_msgs() const
{
    // TODO:
}

static const PCInst& get_state(PCProto::SMMap::const_iterator smap_i, const UUID& uuid)
{
    PCInstMap::const_iterator i = PCProto::SMMap::get_instance(smap_i).get_inst_map().find(uuid);
    if (i == PCProto::SMMap::get_instance(smap_i).get_inst_map().end())
    {
        throw FatalException("");
    }
    return PCInstMap::get_instance(i);
}

bool PCProto::is_prim() const
{
    bool prim = false;
    ViewId last_prim;
    int64_t to_seq = -1;


    for (SMMap::const_iterator i = state_msgs.begin(); i != state_msgs.end();
         ++i)
    {
        const PCInst& i_state(gcomm::get_state(i, SMMap::get_uuid(i)));
        if (i_state.get_prim() == true)
        {
            prim = true;
            last_prim = i_state.get_last_prim();
            to_seq = i_state.get_to_seq();
        }
    }
    
    for (SMMap::const_iterator i = state_msgs.begin(); i != state_msgs.end();
         ++i)
    {
        const PCInst& i_state(gcomm::get_state(i, SMMap::get_uuid(i)));
        if (i_state.get_prim() == true)
        {
            if (i_state.get_last_prim() != last_prim)
            {
                LOG_FATAL("last prims not consistent");
                throw FatalException("");
            }
            if (i_state.get_to_seq() != to_seq)
            {
                LOG_FATAL("TO seqs not consistent");
                throw FatalException("");
            }
        }
        else
        {
            LOG_INFO("non prim " + SMMap::get_uuid(i).to_string() + " from "
                     + i_state.get_last_prim().to_string() 
                     + " joining prim");
        }
    }

    return prim;
}

void PCProto::handle_state(const PCMessage& msg, const UUID& source)
{
    assert(msg.get_type() == PCMessage::T_STATE);
    assert(state_msgs.length() < current_view.get_members().length());
    
    if (state_msgs.insert(make_pair(source, msg)).second == false)
    {
        throw FatalException("");
    }
    
    if (state_msgs.length() == current_view.get_members().length())
    {
        for (SMMap::const_iterator i = state_msgs.begin(); 
             i != state_msgs.end();
             ++i)
        {
            if (instances.find(SMMap::get_uuid(i)) == instances.end())
            {
                if (instances.insert(
                        make_pair(SMMap::get_uuid(i),
                                  gcomm::get_state(
                                      i, 
                                      SMMap::get_uuid(i)))).second == false)
                {
                    throw FatalException("");
                }
            }
        }
        validate_state_msgs();
        if (is_prim())
        {
            shift_to(S_RTR);
            if (requires_rtr())
            {
                throw FatalException("retransmission not implemented");
            }
            if (self_i == instances.begin())
            {
                send_install();
            }
        }
        else
        {
            shift_to(S_NON_PRIM);
        }
    }
}

void PCProto::handle_install(const PCMessage& msg, const UUID& source)
{
    assert(msg.get_type() == PCMessage::T_INSTALL);
    assert(get_state() == S_RTR);
    
    PCInstMap::const_iterator mi = msg.get_inst_map().find(uuid);
    if (mi == msg.get_inst_map().end())
    {
        throw FatalException("");
    }
    const PCInst& m_state = PCInstMap::get_instance(mi);
    if (m_state.get_prim() != get_prim() ||
        m_state.get_last_prim() != get_last_prim() ||
        m_state.get_to_seq() != get_to_seq() ||
        m_state.get_last_seq() != get_last_seq())
    {
        throw FatalException("install message self state does not match");
    }
    
    shift_to(S_PRIM);
}

void PCProto::handle_msg(const PCMessage& msg, 
                         const ReadBuf* rb, 
                         const size_t roff, 
                         const ProtoUpMeta *um)
{
    switch (msg.get_type())
    {
    case PCMessage::T_STATE:
        handle_state(msg, um->get_source());
        break;
    case PCMessage::T_INSTALL:
        handle_install(msg, um->get_source());
        break;
    case PCMessage::T_USER:
        // 
        break;
    default:
        throw FatalException("invalid message");
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
            throw FatalException("could not read message");
        }
        handle_msg(msg, rb, roff, um);
    }
}

int PCProto::handle_down(WriteBuf* wb, const ProtoDownMeta* dm)
{
    return ENOTCONN;
}


END_GCOMM_NAMESPACE
