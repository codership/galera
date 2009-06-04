
#include "evs_proto.hpp"
#include "gcomm/transport.hpp"


BEGIN_GCOMM_NAMESPACE

static bool msg_from_previous_view(const list<pair<ViewId, Time> >& views, 
                                   const EVSMessage& msg)
{
    for (list<pair<ViewId, Time> >::const_iterator i = views.begin();
         i != views.end(); ++i)
    {
        if (msg.get_source_view() == i->first)
        {
            LOG_DEBUG("message " + msg.to_string() + " from previous view " 
                      + i->first.to_string());
            return true;
        }
    }
    // LOG_DEBUG("message from unknown view: " + msg.to_string());
    return false;
}


void EVSProto::check_inactive()
{
    bool has_inactive = false;
    for (InstMap::iterator i = known.begin(); i != known.end(); ++i)
    {
        if (get_pid(i) != my_addr &&
            get_instance(i).operational == true &&
            get_instance(i).tstamp + inactive_timeout < Time::now())
        {
            LOG_WARN(self_string() + " detected inactive node: " +
                     get_pid(i).to_string() + ":" 
                     + get_instance(i).get_name());
            i->second.operational = false;
            has_inactive = true;
        }
    }
    if (has_inactive == true && get_state() == OPERATIONAL)
    {
        SHIFT_TO_P(this, RECOVERY, true);
        if (is_consensus() && is_representative(my_addr))
        {
            send_install();
        }
    }
}

void EVSProto::cleanup_unoperational()
{
    InstMap::iterator i, i_next;
    for (i = known.begin(); i != known.end(); i = i_next) 
    {
        i_next = i, ++i_next;
        if (i->second.installed == false)
        {
            LOG_INFO("erasing " + i->first.to_string() + " at " + self_string());
            known.erase(i);
        }
    }
}

void EVSProto::cleanup_views()
{
    Time now(Time::now());
    list<pair<ViewId, Time> >::iterator i = previous_views.begin();
    while (i != previous_views.end())
    {
        if (i->second + Time(300, 0) < now)
        {
            LOG_INFO("erasing view: " + i->first.to_string());
            previous_views.erase(i);
        }
        else
        {
            break;
        }
        i = previous_views.begin();
    }
}

size_t EVSProto::n_operational() const {
    InstMap::const_iterator i;
    size_t ret = 0;
    for (i = known.begin(); i != known.end(); ++i) {
        if (i->second.operational)
            ret++;
    }
    return ret;
}

void EVSProto::deliver_reg_view()
{
    if (install_message == 0)
    {
        LOG_FATAL("protocol error: no install message in deliver reg view");
        throw FatalException("protocol error");
    }
    
    View view(View::V_REG, install_message->get_source_view());
    if (previous_views.size() == 0)
    {
        throw FatalException("");
    }
    const View& prev_view(previous_view);
    for (InstMap::iterator i = known.begin(); i != known.end(); ++i)
    {
        if (get_instance(i).installed)
        {
            view.add_member(get_pid(i), get_instance(i).get_name());            
            if (prev_view.get_members().find(get_pid(i)) ==
                prev_view.get_members().end())
            {
                view.add_joined(get_pid(i), get_instance(i).get_name());
            }
        }
        else if (get_instance(i).installed == false)
        {
            const EVSMessage::InstMap* instances = install_message->get_instances();
            EVSMessage::InstMap::const_iterator inst_i;
            if ((inst_i = instances->find(get_pid(i))) != instances->end())
            {
                if (inst_i->second.get_left())
                {
                    view.add_left(get_pid(i), get_instance(i).get_name());
                }
                else
                {
                    view.add_partitioned(get_pid(i), 
                                         get_instance(i).get_name());
                }
            }
            assert(get_pid(i) != my_addr);
            i->second.operational = false;
        }
    }
    LOG_DEBUG(view.to_string());
    ProtoUpMeta up_meta(&view);
    pass_up(0, 0, &up_meta);
}

void EVSProto::deliver_trans_view(bool local) 
{
    if (local == false && install_message == 0)
    {
        LOG_FATAL("protocol error: no install message in deliver trans view");
        throw FatalException("protocol error");
    }
    
    View view(View::V_TRANS, current_view.get_id());
    for (InstMap::const_iterator i = known.begin(); i != known.end(); ++i)
    {
        if (get_instance(i).installed && 
            current_view.get_members().find(get_pid(i)) != 
            current_view.get_members().end())
        {
            view.add_member(get_pid(i), get_instance(i).get_name());
        }
        else if (get_instance(i).installed == false)
        {
            if (local == false)
            {
                const EVSMessage::InstMap* instances = install_message->get_instances();
                EVSMessage::InstMap::const_iterator inst_i;
                if ((inst_i = instances->find(get_pid(i))) != instances->end())
                {
                    if (inst_i->second.get_left())
                    {
                        view.add_left(get_pid(i), get_instance(i).get_name());
                    }
                    else
                    {
                        view.add_partitioned(get_pid(i), 
                                             get_instance(i).get_name());
                    }
                }
            }
            else
            {
                // Just assume others have partitioned, it does not matter
                // for leaving node anyway and it is not guaranteed if
                // the others get the leave message, so it is not safe
                // to assume then as left.
                view.add_partitioned(get_pid(i), get_instance(i).get_name());
            }
        }
    }
    LOG_DEBUG(view.to_string());
    ProtoUpMeta up_meta(&view);
    pass_up(0, 0, &up_meta);
}

void EVSProto::deliver_empty_view()
{
    View view(View::V_REG, ViewId());
    LOG_DEBUG(view.to_string());
    ProtoUpMeta up_meta(&view);
    pass_up(0, 0, &up_meta);
}

void EVSProto::setall_installed(bool val)
{
    for (InstMap::iterator i = known.begin(); i != known.end(); ++i) 
    {
        i->second.installed = val;
    }
}

void EVSProto::cleanup_joins()
{
    for (InstMap::iterator i = known.begin(); i != known.end(); ++i)
    {
        delete i->second.join_message;
        i->second.join_message = 0;
    }
}

bool EVSProto::is_all_installed() const
{
    std::string v;
    for (std::map<const UUID, EVSInstance>::const_iterator i = known.begin();
         i != known.end(); ++i)
        v += i->first.to_string() + ":" + i->second.to_string() + " ";
    LOG_DEBUG(self_string() + ": " 
              + current_view.to_string() + " -> " 
              + install_message->get_source_view().to_string() + " " + v);
    for (std::map<const UUID, EVSInstance>::const_iterator i =
             known.begin();
         i != known.end(); ++i) {
        if (i->second.operational && 
            i->second.installed == false)
            return false;
    }
    return true;
}

bool EVSProto::is_consensus() const
{
    LOG_DEBUG(to_string());
    const EVSMessage* my_jm = known.find(my_addr)->second.join_message;
    if (my_jm == 0) {
        LOG_DEBUG(self_string() + " is_consensus(): no own join message");
        return false;
    }
    if (is_consistent(*my_jm) == false) 
    {
        LOG_DEBUG(self_string() 
                  + " is_consensus(): own join message is not consistent");
        return false;
    }
    
    for (InstMap::const_iterator i = known.begin(); i != known.end(); ++i)
    {
        if (i->second.operational == false)
        {
            continue;
        }
        if (i->second.join_message == 0)
        {
            LOG_DEBUG(self_string() 
                      + " is_consensus(): no join message for " 
                      + get_pid(i).to_string());
            return false;
        }
        if (is_consistent(*i->second.join_message) == false)
        {
            LOG_DEBUG(self_string() 
                      + " join message not consistent: "
                      + i->second.join_message->to_string());
            return false;
        }
    }
    LOG_DEBUG("consensus reached at " + self_string());
    return true;
}

bool EVSProto::is_representative(const UUID& pid) const
{
    for (InstMap::const_iterator i = known.begin(); i != known.end(); ++i) 
    {
        if (i->second.operational) 
        {
            if (pid == i->first)
                return true;
            else
                return false;
        }
    }
    return false;
}



bool EVSProto::is_consistent(const EVSMessage& jm) const
{

    if (jm.get_type() != EVSMessage::JOIN &&
        jm.get_type() != EVSMessage::INSTALL)
    {
        throw FatalException("");
    }

    std::map<const UUID, EVSRange> local_insts;
    std::map<const UUID, EVSRange> jm_insts;
    
    // TODO/FIXME: 

    if (jm.get_source_view() == current_view.get_id()) 
    {
        // Compare instances that originate from the current view and 
        // should proceed to next view
        
        // First check agains input map state
        if (!(seqno_eq(input_map.get_aru_seq(), jm.get_aru_seq()) &&
              seqno_eq(input_map.get_safe_seq(), jm.get_seq())))
        {
            LOG_DEBUG(self_string() + " not consistent with " 
                      + jm.to_string() 
                      + ": input map: aru_seqs: "
                      + UInt32(input_map.get_aru_seq()).to_string() 
                      + " "
                      + UInt32(jm.get_aru_seq()).to_string() 
                      + " safe_seqs: " 
                      + UInt32(input_map.get_safe_seq()).to_string() 
                      + " "
                      + UInt32(jm.get_seq()).to_string());
            return false;
        }
        
        for (InstMap::const_iterator i = known.begin(); i != known.end(); ++i) 
        {
            if (i->second.operational == true && 
                i->second.join_message && 
                i->second.join_message->get_source_view() == current_view.get_id())
                local_insts.insert(make_pair(i->first, 
                                             input_map.get_sa_gap(i->first)));
        }
        const EVSMessage::InstMap* jm_instances = 
            jm.get_instances();
        for (EVSMessage::InstMap::const_iterator 
                 i = jm_instances->begin(); i != jm_instances->end(); ++i) 
        {
            if (i->second.get_operational() == true && 
                i->second.get_left() == false &&
                i->second.get_view_id() == current_view.get_id()) 
            {
                jm_insts.insert(make_pair(i->second.get_pid(),
                                          i->second.get_range()));
            } 
        }
        if (jm_insts != local_insts)
        {
            LOG_DEBUG(self_string() 
                      + " not consistent: join message instances");
            LOG_DEBUG("jm_instances:");
            for (EVSMessage::InstMap::const_iterator i = jm_instances->begin();
                 i != jm_instances->end(); ++i)
            {
                LOG_DEBUG(i->second.to_string());
            }
            LOG_DEBUG("local_inst:");
            for (InstMap::const_iterator i = known.begin(); i != known.end();
                 ++i)
            {
                LOG_DEBUG(i->second.to_string());
            }
            LOG_DEBUG("jm_inst:");
            for (map<const UUID, EVSRange>::const_iterator i = jm_insts.begin();
                 i != jm_insts.end(); ++i)
            {
                LOG_DEBUG(i->first.to_string() + " " + i->second.to_string());
            }
            LOG_DEBUG("local_inst:");
            for (map<const UUID, EVSRange>::const_iterator i = local_insts.begin();
                 i != local_insts.end(); ++i)
            {
                LOG_DEBUG(i->first.to_string() + " " + i->second.to_string());
            }
#ifdef STRICT_JOIN_CHECK
            if (jm.get_source() == my_addr)
            {
                throw FatalException("");
            }
#endif
            return false;
        }
        jm_insts.clear();
        local_insts.clear();
        
        // Compare instances that originate from the current view but 
        // are not going to proceed to next view
        
        for (InstMap::const_iterator i = known.begin(); i != known.end(); ++i)
        {
            if (i->second.operational == false)
            {
                local_insts.insert(make_pair(i->first, 
                                             input_map.contains_sa(i->first) ?
                                             input_map.get_sa_gap(i->first) : 
                                             EVSRange()));
            }
            const EVSMessage::InstMap* jm_instances = jm.get_instances();
            for (EVSMessage::InstMap::const_iterator
                     i = jm_instances->begin(); i != jm_instances->end(); ++i)
            {
                if (i->second.get_operational() == false)
                {
                    jm_insts.insert(make_pair(i->second.get_pid(),
                                              i->second.get_range()));
                }
            }
        }
        if (jm_insts != local_insts)
        {
            LOG_DEBUG(self_string() + " not consistent: local instances");
            return false;
        }
        jm_insts.clear();
        local_insts.clear();
    } 
    else 
    {
        // Instances are originating from different view, need to check
        // only that new view is consistent
        for (InstMap::const_iterator i = known.begin();
             i != known.end(); ++i) 
        {
            if (i->second.operational == true &&
                i->second.join_message)
            {
                local_insts.insert(make_pair(i->first, EVSRange()));
            }
        }
        const EVSMessage::InstMap* jm_instances = jm.get_instances();
        for (EVSMessage::InstMap::const_iterator 
                 i = jm_instances->begin(); i != jm_instances->end(); ++i) {
            if (i->second.get_operational() == true) 
            {
                jm_insts.insert(make_pair(i->second.get_pid(), EVSRange()));
            } 
        }
        if (jm_insts != local_insts)
        {
            LOG_DEBUG(self_string() + " not consistent: local instances, different view: own state " + to_string() + " msg " + jm.to_string());
            return false;
        }
        jm_insts.clear();
        local_insts.clear();
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// Message sending
/////////////////////////////////////////////////////////////////////////////

bool EVSProto::is_flow_control(const uint32_t seq, const uint32_t win) const
{
    // Flow control
    if (!seqno_eq(input_map.get_aru_seq(), SEQNO_MAX) && 
        seqno_lt(seqno_add(input_map.get_aru_seq(), win), seq)) {
        // LOG_DEBUG("flow control");
        return true;
    } else if (seqno_eq(input_map.get_aru_seq(), SEQNO_MAX) && 
               seqno_lt(win, seq)) {
        return true;
    }
    return false;
}

int EVSProto::send_user(WriteBuf* wb, 
                        const uint8_t user_type,
                        const EVSSafetyPrefix sp, 
                        const uint32_t win,
                        const uint32_t up_to_seqno,
                        bool local)
{
    assert(get_state() == LEAVING || 
           get_state() == RECOVERY || 
           get_state() == OPERATIONAL);
    int ret;
    uint32_t seq = seqno_eq(last_sent, SEQNO_MAX) ? 0 : seqno_next(last_sent);
    
    // Allow flow control only in OPERATIONAL state to make 
    // RECOVERY state output flush possible.
    if (local == false && 
        get_state() == OPERATIONAL && 
        is_flow_control(seq, win))
    {
        return EAGAIN;
    }
    uint32_t seq_range = seqno_eq(up_to_seqno, SEQNO_MAX) ? 0 :
        seqno_dec(up_to_seqno, seq);
    assert(seq_range < 0x100U);
    uint32_t last_msg_seq = seqno_add(seq, seq_range);
    
    uint8_t flags;
    
    if (output.size() < 2 || 
        !seqno_eq(up_to_seqno, SEQNO_MAX) ||
        is_flow_control(seqno_next(last_msg_seq), win)) {
        flags = 0;
    } else {
        LOG_DEBUG("msg more");
        flags = EVSMessage::F_MSG_MORE;
    }
    
    EVSUserMessage msg(my_addr, 
                       user_type,
                       sp, 
                       seq, 
                       seq_range & 0xffU, 
                       input_map.get_aru_seq(), 
                       current_view.get_id(), 
                       flags);
    wb->prepend_hdr(msg.get_hdr(), msg.get_hdrlen());
    
    // Insert first to input map to determine correct aru seq
    ReadBuf* rb = wb->to_readbuf();
    if (collect_stats == true)
    {
        msg.set_tstamp(Time::now());
    }
    EVSRange range = input_map.insert(EVSInputMapItem(my_addr, msg, rb, 0));
    last_sent = last_msg_seq;
    assert(seqno_eq(range.get_high(), last_sent));
    input_map.set_safe(my_addr, input_map.get_aru_seq());
    rb->release();

    // Rewrite message hdr to include correct aru
    msg.set_aru_seq(input_map.get_aru_seq());
    wb->rollback_hdr(msg.get_hdrlen());
    wb->prepend_hdr(msg.get_hdr(), msg.get_hdrlen());
    
    if (local == false)
    {
        if ((ret = pass_down(wb, 0)))
        {
            LOG_DEBUG("pass down: " + make_int(ret).to_string());
        }
    }
    
    wb->rollback_hdr(msg.get_hdrlen());
    deliver();
    return 0;
}

int EVSProto::send_user()
{
    
    if (output.empty())
        return 0;
    assert(get_state() == OPERATIONAL || get_state() == RECOVERY);
    pair<WriteBuf*, ProtoDownMeta> wb = output.front();
    int ret;
    if ((ret = send_user(wb.first, wb.second.get_user_type(), 
                         SAFE, send_window, SEQNO_MAX)) == 0) {
        output.pop_front();
        delete wb.first;
    }
    return ret;
}

void EVSProto::complete_user(const uint32_t high_seq)
{
    LOG_DEBUG(self_string() + " completing seqno to " 
              + make_int(high_seq).to_string());
    WriteBuf wb(0, 0);
    send_user(&wb, 0xff, DROP, send_window, high_seq);
}

int EVSProto::send_delegate(const UUID& sa, WriteBuf* wb)
{
    EVSDelegateMessage dm(sa);
    wb->prepend_hdr(dm.get_hdr(), dm.get_hdrlen());
    int ret = pass_down(wb, 0);
    wb->rollback_hdr(dm.get_hdrlen());
    return ret;
}

void EVSProto::send_gap(const UUID& pid, const ViewId& source_view, 
                        const EVSRange& range)
{
    LOG_DEBUG("send gap at " + self_string() + " to "  + pid.to_string() + " requesting range " + range.to_string());
    // TODO: Investigate if gap sending can be somehow limited, 
    // message loss happen most probably during congestion and 
    // flooding network with gap messages won't probably make 
    // conditions better
    EVSGap gap(pid, range);
    EVSGapMessage gm(my_addr, source_view, last_sent, input_map.get_aru_seq(), gap);
    
    size_t bufsize = gm.size();
    unsigned char* buf = new unsigned char[bufsize];
    if (gm.write(buf, bufsize, 0) == 0)
        throw FatalException("");
    
    WriteBuf wb(buf, bufsize);
    int err;
    if ((err = pass_down(&wb, 0))) {
        LOG_WARN(std::string("send failed ") 
                 + strerror(err));
    }
    delete[] buf;
    handle_gap(gm, my_addr);
}


EVSJoinMessage EVSProto::create_join()
{
    EVSJoinMessage jm(my_addr,
                      my_name,
                      current_view.get_id(), 
                      input_map.get_aru_seq(), 
                      input_map.get_safe_seq(),
                      ++fifo_seq);
    for (std::map<const UUID, EVSInstance>::const_iterator i = known.begin();
         i != known.end(); ++i)
    {
        const UUID& pid = get_pid(i);
        const EVSInstance& ei = get_instance(i);
        jm.add_instance(pid, 
                        ei.name,
                        ei.operational, 
                        has_leave(pid),
                        (ei.join_message ? 
                         ei.join_message->get_source_view() : 
                         (input_map.contains_sa(pid) ? current_view.get_id() : 
                          ViewId())), 
                        (input_map.contains_sa(pid) ? 
                         input_map.get_sa_gap(pid) : EVSRange()),
                        (input_map.contains_sa(pid) ?
                         input_map.get_sa_safe_seq(pid) : SEQNO_MAX));
    }
#ifdef STRICT_JOIN_CHECK
    if (is_consistent(jm) == false)
    {
        throw FatalException("");
    }
#endif
    return jm;
}

void EVSProto::set_join(const EVSMessage& jm, const UUID& source)
{
    if (jm.get_type() != EVSMessage::JOIN)
    {
        throw FatalException("");
    }
    InstMap::iterator i = known.find(source);
    if (i == known.end())
    {
        throw FatalException("");
    }
    delete i->second.join_message;
    i->second.join_message = new EVSMessage(jm);
}

void EVSProto::set_leave(const EVSMessage& lm, const UUID& source)
{
    if (lm.get_type() != EVSMessage::LEAVE)
    {
        throw FatalException("");
    }
    InstMap::iterator i = known.find(source);
    if (i == known.end())
    {
        throw FatalException("");
    }
    if (i->second.leave_message != 0)
    {
        LOG_WARN("duplicate leave: previous: "
                 + i->second.leave_message->to_string() 
                 + " new: "
                 + lm.to_string());
    }
    else
    {
        i->second.leave_message = new EVSMessage(lm);
    }
}

bool EVSProto::has_leave(const UUID& uuid) const
{
    InstMap::const_iterator ii = known.find(uuid);
    if (ii == known.end())
    {
        throw FatalException("");
    }
    
    if (ii->second.leave_message != 0)
    {
        return true;
    }
    
    for (ii = known.begin(); ii != known.end(); ++ii)
    {
        if (ii->second.join_message != 0)
        {
            const EVSMessage::InstMap* im = 
                ii->second.join_message->get_instances();
            if (im == 0)
            {
                throw FatalException("");
            }
            EVSMessage::InstMap::const_iterator ji = im->find(uuid); 
            if (ji != im->end() && ji->second.get_left() == true)
            {
                return true;
            }
        }
    }
    return false;
}

void EVSProto::send_join(bool handle)
{
    assert(output.empty());
    EVSJoinMessage jm = create_join();
    LOG_DEBUG(self_string() + " sending join " + jm.to_string());
    size_t bufsize = jm.size();
    unsigned char* buf = new unsigned char[bufsize];
    if (jm.write(buf, bufsize, 0) == 0)
        throw FatalException("failed to serialize join message");
    WriteBuf wb(buf, bufsize);
    int err;
    if ((err = pass_down(&wb, 0))) {
        LOG_WARN(std::string("EVSProto::send_join(): Send failed ") 
                 + strerror(err));
    }
    delete[] buf;
    if (handle)
    {
        handle_join(jm, my_addr);
    }
    else
    {
        set_join(jm, my_addr);
    }
}

void EVSProto::send_leave()
{
    assert(get_state() == LEAVING);
    
    LOG_DEBUG(self_string() + " send leave as " + UInt32(last_sent).to_string());
    EVSLeaveMessage lm(my_addr, 
                       my_name,
                       current_view.get_id(), 
                       input_map.get_aru_seq(), 
                       last_sent,
                       ++fifo_seq);
    size_t bufsize = lm.size();
    unsigned char* buf = new unsigned char[bufsize];
    if (lm.write(buf, bufsize, 0) == 0)
        throw FatalException("failed to serialize leave message");
    
    WriteBuf wb(buf, bufsize);
    int err;
    if ((err = pass_down(&wb, 0))) {
        LOG_WARN(std::string("EVSProto::send_leave(): Send failed ") 
                 + strerror(err));
    }
    delete[] buf;
    handle_leave(lm, my_addr);
}

void EVSProto::send_install()
{
    LOG_DEBUG(self_string() 
              + " installing flag " + Int(installing).to_string());
    if (installing)
    {
        return;
    }
    if (is_consensus() != true || is_representative(my_addr) != true)
    {
        throw FatalException("");
    }
    InstMap::const_iterator self = known.find(my_addr);
    EVSInstallMessage im(my_addr,
                         my_name,
                         ViewId(my_addr, 
                                current_view.get_id().get_seq() + 1),
                         input_map.get_aru_seq(), 
                         input_map.get_safe_seq(),
                         ++fifo_seq);
    for (InstMap::const_iterator i = known.begin(); i != known.end(); ++i) 
    {
        const UUID& pid = get_pid(i);
        const EVSInstance& ei = get_instance(i);
        im.add_instance(pid, 
                        ei.name,
                        ei.operational,
                        has_leave(pid),
                        (ei.join_message ? 
                         ei.join_message->get_source_view() : 
                         (input_map.contains_sa(pid) ? 
                          current_view.get_id() : ViewId())), 
                        (input_map.contains_sa(pid) ? 
                         input_map.get_sa_gap(pid) : EVSRange()),
                        (input_map.contains_sa(pid) ?
                         input_map.get_sa_safe_seq(pid) : SEQNO_MAX));
    }
    LOG_DEBUG(self_string() + " sending install: " + im.to_string());
    size_t bufsize = im.size();
    unsigned char* buf = new unsigned char[bufsize];
    if (im.write(buf, bufsize, 0) == 0)
        throw FatalException("");
    
    WriteBuf wb(buf, bufsize);
    int err;
    if ((err = pass_down(&wb, 0))) 
    {
        LOG_WARN(string("send failed ") + strerror(err));
    }
    delete[] buf;
    installing = true;
    handle_install(im, my_addr);
}


void EVSProto::resend(const UUID& gap_source, const EVSGap& gap)
{
    assert(gap.source == my_addr);
    if (seqno_eq(gap.get_high(), SEQNO_MAX)) {
        LOG_DEBUG("empty gap: " 
                  + UInt32(gap.get_low()).to_string() + " -> " 
                  + UInt32(gap.get_high()).to_string());
        return;
    } else if (!seqno_eq(gap.get_low(), SEQNO_MAX) &&
               seqno_gt(gap.get_low(), gap.get_high())) {
        LOG_DEBUG("empty gap: " 
                  + UInt32(gap.get_low()).to_string() + " -> " 
                  + UInt32(gap.get_high()).to_string());
        return;
    }
    
    uint32_t start_seq = seqno_eq(gap.get_low(), SEQNO_MAX) ? 0 : gap.get_low();
    LOG_DEBUG(self_string() + " resending, requested by " 
              + gap_source.to_string() 
              + " " 
              + make_int(start_seq).to_string() + " -> " 
              + make_int(gap.get_high()).to_string());

    for (uint32_t seq = start_seq; !seqno_gt(seq, gap.get_high()); ) {


        std::pair<EVSInputMapItem, bool> i = input_map.recover(my_addr,
                                                               seq);
        if (i.second == false) 
        {
            LOG_FATAL("could not recover message for " 
                      + gap_source.to_string());
            throw FatalException("");
        } else {
            const ReadBuf* rb = i.first.get_readbuf();
            const EVSMessage& msg = i.first.get_evs_message();
            assert(msg.get_type() == EVSMessage::USER);
            assert(seqno_eq(msg.get_seq(), seq));
            EVSUserMessage new_msg(msg.get_source(), 
                                   msg.get_user_type(),
                                   msg.get_safety_prefix(),
                                   msg.get_seq(), 
                                   msg.get_seq_range(),
                                   input_map.get_aru_seq(),
                                   msg.get_source_view(), 
                                   EVSMessage::F_RESEND);
            LOG_DEBUG("resend: " + new_msg.to_string());
            WriteBuf wb(rb ? rb->get_buf(i.first.get_payload_offset()) : 0, 
                        rb ? rb->get_len(i.first.get_payload_offset()) : 0);
            wb.prepend_hdr(new_msg.get_hdr(), new_msg.get_hdrlen());
            if (pass_down(&wb, 0))
            {
                break;
            }
            seq = seqno_add(seq, msg.get_seq_range() + 1);
        }
    }
}

void EVSProto::recover(const EVSGap& gap)
{
    if (seqno_eq(gap.get_high(), SEQNO_MAX)) {
        LOG_DEBUG("empty gap: " 
                  + UInt32(gap.get_low()).to_string() + " -> " 
                  + UInt32(gap.get_high()).to_string());
        return;
    } else if (!seqno_eq(gap.get_low(), SEQNO_MAX) &&
               seqno_gt(gap.get_low(), gap.get_high())) {
        LOG_DEBUG("empty gap: " 
                  + UInt32(gap.get_low()).to_string() + " -> " 
                  + UInt32(gap.get_high()).to_string());
        return;
    }
    
    // TODO: Find out a way to select only single instance that
    // is allowed to recover messages
    for (uint32_t seq = seqno_eq(gap.get_low(), SEQNO_MAX) ? 0 : gap.get_low();
         !seqno_gt(seq, gap.get_high()); ) {
        std::pair<EVSInputMapItem, bool> i = input_map.recover(gap.source,
                                                               seq);
        if (i.second == true) 
        {
            const ReadBuf* rb = i.first.get_readbuf();
            const EVSMessage& msg = i.first.get_evs_message();
            EVSUserMessage new_msg(msg.get_source(), 
                                   msg.get_user_type(),
                                   msg.get_safety_prefix(),
                                   msg.get_seq(), 
                                   msg.get_seq_range(),
                                   input_map.get_aru_seq(),
                                   msg.get_source_view(), 
                                   EVSMessage::F_RESEND);
            WriteBuf wb(rb ? rb->get_buf(i.first.get_payload_offset()) : 0,
                        rb ? rb->get_len(i.first.get_payload_offset()) : 0);
            wb.prepend_hdr(new_msg.get_hdr(), new_msg.get_hdrlen());
            LOG_DEBUG("recovered " + new_msg.to_string());
            if (send_delegate(gap.source, &wb))
            {
                break;
            }
            seq = seqno_add(seq, msg.get_seq_range() + 1);
        } 
        else 
        {
            seq = seqno_next(seq);
        }
    }
}


void EVSProto::handle_msg(const EVSMessage& msg, const UUID& source,
                          const ReadBuf* rb, const size_t roff)
{
    // Filter out unwanted/duplicate membership messages
    if (msg.is_membership())
    {
        InstMap::iterator ii = known.find(source);
        if (ii != known.end())
        {
            if (get_instance(ii).fifo_seq >= msg.get_fifo_seq())
            {
                LOG_WARN("dropping non-fifo membership message");
                return;
            }
            else
            {
                LOG_TRACE("local fifo_seq " 
                          + make_int(get_instance(ii).fifo_seq).to_string() 
                          + " msg fifo_seq "
                          + make_int(msg.get_fifo_seq()).to_string());
                get_instance(ii).fifo_seq = msg.get_fifo_seq();
            }
        }
    }

    if (source != my_addr)
    {
        switch (msg.get_type()) {
        case EVSMessage::USER:
            handle_user(msg, source, rb, roff);
            break;
        case EVSMessage::DELEGATE:
            handle_delegate(msg, source, rb, roff);
            break;
        case EVSMessage::GAP:
            handle_gap(msg, source);
            break;
        case EVSMessage::JOIN:
            handle_join(msg, source);
            break;
        case EVSMessage::LEAVE:
            handle_leave(msg, source);
            break;
        case EVSMessage::INSTALL:
            handle_install(msg, source);
            break;
        default:
            LOG_WARN(std::string("EVS::handle_msg(): Invalid message type: ") 
                     + EVSMessage::to_string(msg.get_type()));
        }
    }
}

////////////////////////////////////////////////////////////////////////
// Protolay interface
////////////////////////////////////////////////////////////////////////



void EVSProto::handle_up(const int cid, const ReadBuf* rb, const size_t roff,
                         const ProtoUpMeta* um)
{
    Critical crit(mon);
    
    EVSMessage msg;
    
    if (rb == 0 && um == 0)
        throw FatalException("EVS::handle_up(): Invalid input rb == 0 && um == 0");
    
    if (get_state() == CLOSED)
    {
        LOG_DEBUG("dropping message in closed state");
        return;
    }
    
    if (msg.read(rb->get_buf(), rb->get_len(), roff) == 0)
    {
        LOG_WARN("EVS::handle_up(): Invalid message");
        return;
    }
    if (um)
    {
        msg.set_source(um->get_source());
    }
    if (msg.get_source() == UUID::nil())
    {
        throw FatalException("");
    }
    
    LOG_TRACE(self_string() + " message " + EVSMessage::to_string(msg.get_type()) + " from " + msg.get_source().to_string());
    
    const UUID& source(msg.get_source());
    

    handle_msg(msg, source, rb, roff);
}

int EVSProto::handle_down(WriteBuf* wb, const ProtoDownMeta* dm)
{
    Critical crit(mon);
    
    LOG_TRACE("user message in state " + to_string(get_state()));
    
    if (get_state() == RECOVERY)
    {
        return EAGAIN;
    }
    else if (get_state() != OPERATIONAL)
    {
        LOG_WARN("user message in state " + to_string(get_state()));
        return ENOTCONN;
    }

    if (dm && dm->get_user_type() == 0xff)
    {
        return EINVAL;
    }
    
    int ret = 0;
    
    if (output.empty()) 
    {
        int err = send_user(wb, dm ? dm->get_user_type() : 0xff,
                            SAFE, send_window/2, SEQNO_MAX);
        switch (err) 
        {
        case EAGAIN:
        {
            LOG_DEBUG("EVSProto::handle_down(): flow control");
            WriteBuf* priv_wb = wb->copy();
            output.push_back(make_pair(priv_wb, dm ? ProtoDownMeta(*dm) : ProtoDownMeta(0xff)));
            // Fall through
        }
        case 0:
            break;
        default:
            LOG_ERROR("Send error: " + Int(err).to_string());
            ret = err;
        }
    } 
    else if (output.size() < max_output_size)
    {
        LOG_DEBUG("EVSProto::handle_down(): queued message");
        WriteBuf* priv_wb = wb->copy();
        output.push_back(make_pair(priv_wb, dm ? ProtoDownMeta(*dm) : ProtoDownMeta(0xff)));
    } 
    else 
    {
        LOG_WARN("Output queue full");
        ret = EAGAIN;
    }
    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// State handler
/////////////////////////////////////////////////////////////////////////////

void EVSProto::shift_to(const State s, const bool send_j)
{
    if (shift_to_rfcnt > 0)
    {
        throw FatalException("");
    }
    shift_to_rfcnt++;
    static const bool allowed[STATE_MAX][STATE_MAX] = {
    // CLOSED
        {false, true, false, false, false},
    // JOINING
        {false, false, true, true, false},
    // LEAVING
        {true, false, false, false, false},
    // RECOVERY
        {false, false, true, true, true},
    // OPERATIONAL
        {false, false, true, true, false}
    };
    
    assert(s < STATE_MAX);
    if (allowed[state][s] == false) {
        LOG_FATAL(std::string("invalid state transition: ") 
                  + to_string(state) + " -> " + to_string(s));
        throw FatalException("invalid state transition");
    }
    
    if (get_state() != s)
    {
        LOG_DEBUG(self_string() + ": state change: " + 
                  to_string(state) + " -> " + to_string(s));
    }
    switch (s) {
    case CLOSED:
        if (collect_stats)
        {
            LOG_INFO("delivery stats (safe): " + hs_safe.to_string());
        }
        hs_safe.clear();
        stop_inactivity_timer();
        cleanup_unoperational();
        cleanup_views();
        cleanup();
        state = CLOSED;
        break;
    case JOINING:
        // tp->set_loopback(true);
        state = JOINING;
        start_inactivity_timer();
        break;
    case LEAVING:
        // send_leave();
        // tp->set_loopback(true);
        unset_consensus_timer();
        state = LEAVING;
        break;
    case RECOVERY:
    {
        // tp->set_loopback(true);
        stop_resend_timer();
        stop_send_join_timer();
        start_send_join_timer();
        if (get_state() != RECOVERY)
        {
            cleanup_joins();
        }
        setall_installed(false);
        delete install_message;
        install_message = 0;
        installing = false;
        if (is_set_consensus_timer())
        {
            unset_consensus_timer();
        }
        set_consensus_timer();
        state = RECOVERY;
        while (output.empty() == false)
        {
            send_user();
        }
        if (send_j == true)
        {
            send_join(false);
            
        }

        break;
    }
    case OPERATIONAL:
    {
        // tp->set_loopback(false);
        assert(output.empty() == true);
        assert(install_message && is_consistent(*install_message));
        assert(is_all_installed() == true);
        unset_consensus_timer();
        stop_send_join_timer();
        deliver();
        deliver_trans_view(false);
        deliver_trans();
        // Reset input map
        input_map.clear();

        previous_view = current_view;
        previous_views.push_back(make_pair(current_view.get_id(), Time::now()));
        const EVSMessage::InstMap* imap = install_message->get_instances();
        if (imap == 0)
        {
            throw FatalException("");
        }
        for (EVSMessage::InstMap::const_iterator i = imap->begin();
             i != imap->end(); ++i)
        {
            previous_views.push_back(make_pair(i->second.get_view_id(), 
                                               Time::now()));
        }
        current_view = View(View::V_REG, 
                            install_message->get_source_view());
        for (InstMap::const_iterator i = known.begin(); i != known.end(); ++i)
        {
            if (get_instance(i).installed)
            {
                current_view.add_member(get_pid(i), get_instance(i).get_name());
                input_map.insert_sa(get_pid(i));
            }
        }
        
        last_sent = SEQNO_MAX;
        state = OPERATIONAL;
        deliver_reg_view();
        if (collect_stats)
        {
            LOG_INFO("delivery stats (safe): " + hs_safe.to_string());
        }
        hs_safe.clear();
        cleanup_unoperational();
        cleanup_views();
        delete install_message;
        install_message = 0;
        LOG_DEBUG("new view: " + current_view.to_string());
        start_resend_timer();
        assert(get_state() == OPERATIONAL);
        break;
    }
    default:
        throw FatalException("Invalid state");
    }
    shift_to_rfcnt--;
}

////////////////////////////////////////////////////////////////////////////
// Message delivery
////////////////////////////////////////////////////////////////////////////

void EVSProto::validate_reg_msg(const EVSMessage& msg)
{
    if (msg.get_type() != EVSMessage::USER)
    {
        throw FatalException("reg validate: not user message");
    }
    if (msg.get_source_view() != current_view.get_id())
    {
        throw FatalException("reg validate: not current view");
    }
    if (collect_stats && msg.get_safety_prefix() == SAFE)
    {
        Time now(Time::now());
        hs_safe.insert(now.to_double() - msg.get_tstamp().to_double());
    }
}

void EVSProto::deliver()
{
    if (get_state() != OPERATIONAL && get_state() != RECOVERY && 
        get_state() != LEAVING)
        throw FatalException("Invalid state");
    LOG_DEBUG("aru_seq: " + UInt32(input_map.get_aru_seq()).to_string() 
              + " safe_seq: "
              + UInt32(input_map.get_safe_seq()).to_string());
    
    EVSInputMap::iterator i, i_next;
    // First deliver all messages that qualify at least as safe
    for (i = input_map.begin();
         i != input_map.end() && input_map.is_safe(i); i = i_next) {
        i_next = i;
        ++i_next;
        validate_reg_msg(i->get_evs_message());
        if (i->get_evs_message().get_safety_prefix() != DROP)
        {
            ProtoUpMeta um(i->get_sockaddr(), i->get_evs_message().get_user_type());
            pass_up(i->get_readbuf(), i->get_payload_offset(), &um);
        }
        input_map.erase(i);
    }
    // Deliver all messages that qualify as agreed
    for (; i != input_map.end() && 
             i->get_evs_message().get_safety_prefix() == AGREED &&
             input_map.is_agreed(i); i = i_next)
    {
        i_next = i;
        ++i_next;
        validate_reg_msg(i->get_evs_message());
        ProtoUpMeta um(i->get_sockaddr(), i->get_evs_message().get_user_type());
        pass_up(i->get_readbuf(), i->get_payload_offset(), &um);
        input_map.erase(i);
    }
    // And finally FIFO or less 
    for (; i != input_map.end() &&
             i->get_evs_message().get_safety_prefix() == FIFO &&
             input_map.is_fifo(i); i = i_next) {
        i_next = i;
        ++i_next;
        validate_reg_msg(i->get_evs_message());
        ProtoUpMeta um(i->get_sockaddr(), i->get_evs_message().get_user_type());
        pass_up(i->get_readbuf(), i->get_payload_offset(), &um);
        input_map.erase(i);
    }
    

}

void EVSProto::validate_trans_msg(const EVSMessage& msg)
{
    LOG_DEBUG(msg.to_string());
    if (msg.get_type() != EVSMessage::USER)
    {
        throw FatalException("reg validate: not user message");
    }
    if (msg.get_source_view() != current_view.get_id())
    {
        throw FatalException("reg validate: not current view");
    }
    if (collect_stats && msg.get_safety_prefix() == SAFE)
    {
        Time now(Time::now());
        hs_safe.insert(now.to_double() - msg.get_tstamp().to_double());
    }
}

void EVSProto::deliver_trans()
{
    if (get_state() != RECOVERY && get_state() != LEAVING)
        throw FatalException("Invalid state");
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
    
    EVSInputMap::iterator i, i_next;
    for (i = input_map.begin(); i != input_map.end() && 
             input_map.is_agreed(i); i = i_next) {
        i_next = i;
        ++i_next;
        validate_trans_msg(i->get_evs_message());
        if (i->get_evs_message().get_safety_prefix() != DROP)
        {
            ProtoUpMeta um(i->get_sockaddr(), i->get_evs_message().get_user_type());
            pass_up(i->get_readbuf(), i->get_payload_offset(), &um);
        }
        input_map.erase(i);
    }
    
    for (; i != input_map.end() && input_map.is_fifo(i); i = i_next)
    {
        i_next = i;
        ++i_next;
        validate_trans_msg(i->get_evs_message());
        if (i->get_evs_message().get_safety_prefix() != DROP)
        {
            ProtoUpMeta um(i->get_sockaddr(), i->get_evs_message().get_user_type());
            pass_up(i->get_readbuf(), i->get_payload_offset(), &um);
        }
        input_map.erase(i);
    }
    
    // Sanity check:
    // There must not be any messages left that 
    // - Are originated from outside of trans conf and are FIFO
    // - Are originated from trans conf
    for (i = input_map.begin(); i != input_map.end(); i = i_next)
    {
        i_next = i;
        ++i_next;
        std::map<const UUID, EVSInstance>::iterator ii =
            known.find(i->get_sockaddr());
        if (ii->second.installed)
        {
            LOG_FATAL("Protocol error in transitional delivery "
                      "(self delivery constraint)");
            throw FatalException("Protocol error in transitional delivery "
                                 "(self delivery constraint)");
        }
        else if (input_map.is_fifo(i))
        {
            LOG_FATAL("Protocol error in transitional delivery "
                                 "(fifo from partitioned component)");
            throw FatalException("Protocol error in transitional delivery "
                                 "(fifo from partitioned component)");
        }
        input_map.erase(i);
    }
    
}


/////////////////////////////////////////////////////////////////////////////
// Message handlers
/////////////////////////////////////////////////////////////////////////////


void EVSProto::handle_user(const EVSMessage& msg, const UUID& source, 
                           const ReadBuf* rb, const size_t roff)
{

    if (msg.get_flags() & EVSMessage::F_RESEND)
    {
        LOG_DEBUG(self_string() + " msg with resend flag " + msg.to_string());
    }
    else 
    {
        
        LOG_DEBUG(self_string() + " " + msg.to_string());
    }
    
    std::map<const UUID, EVSInstance>::iterator i = known.find(source);
    if (i == known.end()) {
        // Previously unknown instance has appeared and it seems to
        // be operational, assume that it can be trusted and start
        // merge/recovery
        LOG_DEBUG(self_string() + " new instance: " + source.to_string());
        std::pair<std::map<const UUID, EVSInstance>::iterator, bool> iret;
        iret = known.insert(make_pair(source,
                                      EVSInstance(source.to_string())));
        assert(iret.second == true);
        i = iret.first;
        assert(i->second.get_operational() == true);
        if (state == JOINING || state == RECOVERY || state == OPERATIONAL) 
        {
            SHIFT_TO(RECOVERY);
        }
        return;
    } 
    else if (state == JOINING || state == CLOSED) 
    {
        // Drop message
        LOG_DEBUG(self_string() + " dropping: " + msg.to_string());
        return;
    } 
    else if (msg.get_source_view() != current_view.get_id()) 
    {
        if (get_state() == LEAVING) 
        {
            LOG_DEBUG(self_string() + " leaving, dropping: " + msg.to_string());
            return;
        }
        
        if (msg_from_previous_view(previous_views, msg))
        {
            LOG_DEBUG(self_string() + " user message " + msg.to_string() + 
                      " from previous view");
            return;
        }
        
        if (i->second.operational == false) 
        {
            // This is probably partition merge, see if it works out
            LOG_DEBUG(self_string() + " unoperational source " + source.to_string());
            i->second.operational = true;
            SHIFT_TO(RECOVERY);
            return;
        } 
        else if (i->second.installed == false) 
        {
            if (install_message && 
                msg.get_source_view() == install_message->get_source_view()) 
            {
                assert(state == RECOVERY);
                LOG_DEBUG(self_string() + " recovery user message source");
                // Other instances installed view before this one, so it is 
                // safe to shift to OPERATIONAL if consensus has been reached
                for (EVSMessage::InstMap::const_iterator mi = install_message->get_instances()->begin(); mi != install_message->get_instances()->end(); ++mi)
                {
                    InstMap::iterator ii = known.find(mi->second.get_pid());
                    if (ii == known.end())
                    {
                        throw FatalException("");
                    }
                    ii->second.installed = true;
                }
                
                if (is_consensus()) 
                {
                    SHIFT_TO(OPERATIONAL);
                } 
                else 
                {
                    SHIFT_TO(RECOVERY);
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
            LOG_INFO(self_string() + " unknown user message: " + msg.to_string());
            return;
        }
    }
    
//    assert((i->second.operational == true || i->second.leave_message) &&
//           (i->second.installed == true || get_state() == RECOVERY) &&
    assert(msg.get_source_view() == current_view.get_id());
    
    const uint32_t prev_aru = input_map.get_aru_seq();
    const uint32_t prev_safe = input_map.get_safe_seq();
    if (collect_stats == true)
    {
        msg.set_tstamp(Time::now());
    }
    const EVSRange range(input_map.insert(EVSInputMapItem(source, msg, rb, roff)));
    
    if (!seqno_eq(range.low, i->second.prev_range.low))
    {
        i->second.tstamp = Time::now();
    }
    i->second.prev_range = range;
    
    if (!seqno_eq(input_map.get_safe_seq(), prev_safe))
    {
        LOG_DEBUG(self_string() + " safe seq " 
                  + UInt32(input_map.get_safe_seq()).to_string() 
                  + " prev " 
                  + UInt32(prev_safe).to_string());
    }
    
    
    // Some messages are missing
    // 
    // TODO: 
    // - Gap messages should take list of gaps to avoid sending gap 
    //   message for each missing message
    // - There should be guard (timer etc) to avoid sending gap message
    //   for each incoming packet from the source of missing packet 
    //   (maybe this is not too bad if gap message contains gap list
    //   and message loss is infrequent)
    if ((seqno_eq(range.get_low(), SEQNO_MAX) ||
         seqno_gt(range.get_high(), range.get_low())) &&
        !(msg.get_flags() & EVSMessage::F_RESEND)) 
    {
        LOG_DEBUG("requesting at " 
                  + self_string() 
                  + " from " 
                  + source.to_string() 
                  + " " + range.to_string() 
                  + " due to input map gap, aru " 
                  + UInt32(input_map.get_aru_seq()).to_string());
        send_gap(source, current_view.get_id(), range);
    }
    
    if (!(i->first == my_addr) && 
        ((output.empty() && !(msg.get_flags() & EVSMessage::F_MSG_MORE)) ||
         get_state() == RECOVERY) && 
        /* !seqno_eq(range.get_high(), SEQNO_MAX) && */
        (seqno_eq(last_sent, SEQNO_MAX) || 
         seqno_lt(last_sent, range.get_high()))) 
    {
        // Message not originated from this instance, output queue is empty
        // and last_sent seqno should be advanced
        complete_user(range.get_high());
    } 
    else if (((output.empty() && 
               (seqno_eq(input_map.get_aru_seq(), SEQNO_MAX) ||
                !seqno_eq(input_map.get_aru_seq(), prev_aru))) &&
              !(msg.get_flags() & EVSMessage::F_RESEND)) || 
             get_state() == LEAVING) 
    {
        // Output queue empty and aru changed, send gap to inform others
        LOG_DEBUG(self_string() + " sending gap");
        send_gap(source, current_view.get_id(), EVSRange(SEQNO_MAX, SEQNO_MAX));
    }
    
    deliver();
    while (output.empty() == false)
    {
        if (send_user())
            break;
    }

    
    if (get_state() == RECOVERY && 
        seqno_eq(last_sent, input_map.get_aru_seq()) && 
        (seqno_eq(prev_aru, input_map.get_aru_seq()) == false ||
         seqno_eq(prev_safe, input_map.get_safe_seq()) == false))
    {
        assert(output.empty() == true);
        InstMap::const_iterator self_i = known.find(my_addr);
        if (self_i == known.end())
        {
            throw FatalException("");
        }
        
        if (self_i->second.join_message == 0 ||
            is_consistent(*self_i->second.join_message) == false)
        {
            send_join();
        }
    }

    LOG_DEBUG(self_string() 
              + " aru_seq: " + UInt32(input_map.get_aru_seq()).to_string()
              + " safe_seq: " + UInt32(input_map.get_safe_seq()).to_string());
}

void EVSProto::handle_delegate(const EVSMessage& msg, const UUID& source,
                               const ReadBuf* rb, const size_t roff)
{
    EVSMessage umsg;
    if (umsg.read(rb->get_buf(roff), 
                  rb->get_len(roff), msg.size()) == 0)
    {
        throw FatalException("failed to read user msg from delegate");
    }
    handle_user(umsg, umsg.get_source(), rb, roff + msg.size());
}

void EVSProto::handle_gap(const EVSMessage& msg, const UUID& source)
{
    LOG_DEBUG("gap message at " + self_string() + " source " +
              msg.get_source().to_string() + " source view: " + 
              msg.get_source_view().to_string() 
              + " seq " + UInt32(msg.get_seq()).to_string()
              + " aru_seq " + UInt32(msg.get_aru_seq()).to_string());
    std::map<UUID, EVSInstance>::iterator i = known.find(source);
    if (i == known.end()) 
    {
        std::pair<std::map<const UUID, EVSInstance>::iterator, bool> iret;
        iret = known.insert(make_pair(source,
                                      EVSInstance(source.to_string())));
        assert(iret.second == true);
        i = iret.first;
        assert(i->second.get_operational() == true);
        if (state == JOINING || state == RECOVERY || state == OPERATIONAL) 
        {
            SHIFT_TO(RECOVERY);
        }
        return;
    } 
    else if (state == JOINING || state == CLOSED) 
    {	
        // Silent drop
        return;
    } 
    else if (state == RECOVERY && install_message && 
             install_message->get_source_view() == msg.get_source_view()) 
    {
        i->second.installed = true;
        if (is_all_installed())
            SHIFT_TO(OPERATIONAL);
        return;
    } 
    else if (msg.get_source_view() != current_view.get_id()) 
    {
        if (msg_from_previous_view(previous_views, msg))
        {
            LOG_DEBUG("gap message from previous view");
            return;
        }
        if (i->second.operational == false) 
        {
            // This is probably partition merge, see if it works out
            i->second.operational = true;
            SHIFT_TO(RECOVERY);
        } 
        else if (i->second.installed == false) 
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
            LOG_INFO(self_string() + " unknown gap message: " + msg.to_string());
        }
        return;
    }
    
//    assert((i->second.operational == true || i->second.leave_message) &&
//           (i->second.installed == true || get_state() == RECOVERY) &&
    assert(msg.get_source_view() == current_view.get_id());
    
    uint32_t prev_safe = input_map.get_safe_seq();
    // Update safe seq for source
    if (!seqno_eq(msg.get_aru_seq(), SEQNO_MAX))
    {
        input_map.set_safe(source, msg.get_aru_seq());
        if (!seqno_eq(input_map.get_safe_seq(), prev_safe)) 
        {
            LOG_DEBUG("handle gap " + self_string() +  " safe seq " 
                      + UInt32(input_map.get_safe_seq()).to_string() 
                      + " aru seq " 
                      + UInt32(input_map.get_aru_seq()).to_string());
        }
    }
    

    // Scan through gap list and resend or recover messages if appropriate.
    EVSGap gap = msg.get_gap();
    LOG_DEBUG("gap source " + gap.get_source().to_string());
    if (gap.get_source() == my_addr)
    {
        resend(i->first, gap);
    }
    else if (get_state() == RECOVERY && !seqno_eq(gap.get_high(), SEQNO_MAX))
    {
        recover(gap);
    }

    
    
    // If it seems that some messages from source instance are missing,
    // send gap message
    EVSRange source_gap(input_map.get_sa_gap(source));
    if (!seqno_eq(msg.get_seq(), SEQNO_MAX) && 
        (seqno_eq(source_gap.get_low(), SEQNO_MAX) ||
         !seqno_gt(source_gap.get_low(), msg.get_seq()))) 
    {
        // TODO: Sending gaps here causes excessive flooding, need to 
        // investigate if gaps can be sent only in send_user
        // LOG_DEBUG("requesting at " + self_string() + " from " + source.to_string() + " " + EVSRange(source_gap.get_low(), msg.get_seq()).to_string() + " due to gap message");
        // send_gap(source, current_view, EVSRange(source_gap.get_low(),
        //                                      msg.get_seq()));
    }
    
    // Deliver messages 
    deliver();
    while (get_state() == OPERATIONAL && output.empty() == false)
    {
        if (send_user())
            break;
    }

    if (get_state() == RECOVERY && 
        seqno_eq(last_sent, input_map.get_aru_seq()) && 
        seqno_eq(prev_safe, input_map.get_safe_seq()) == false)
    {
        assert(output.empty() == true);
        InstMap::const_iterator self_i = known.find(my_addr);
        if (self_i == known.end())
        {
            throw FatalException("");
        }
        
        if (self_i->second.join_message == 0 ||
            is_consistent(*self_i->second.join_message) == false)
        {
            send_join();
        }
    }
}



bool EVSProto::states_compare(const EVSMessage& msg) 
{

    const EVSMessage::InstMap* instances = msg.get_instances();
    bool send_join_p = false;
    uint32_t low_seq = SEQNO_MAX;
    UUID low_uuid = UUID::nil();
    uint32_t high_seq = SEQNO_MAX;
    
    // Compare view of operational instances
    for (EVSMessage::InstMap::const_iterator ii = instances->begin(); 
         ii != instances->end(); ++ii) 
    {
        InstMap::iterator local_ii = known.find(ii->second.get_pid());
        if (local_ii == known.end())
        {
            LOG_DEBUG(self_string() + " new instance from join message");
            if (known.insert(make_pair(ii->second.get_pid(), 
                                       EVSInstance(""))).second == false)
            {
                throw FatalException("");
            }
        }
        else if (local_ii->second.operational != ii->second.get_operational()) 
        {
            if (local_ii->second.operational == true && 
                get_pid(local_ii) != my_addr) 
            {
                if (local_ii->second.tstamp + inactive_timeout <
                    Time::now() ||
                    ii->second.get_left()) 
                {
                    LOG_DEBUG("setting " + local_ii->first.to_string() 
                              + " as unoperational at " + self_string());
                    local_ii->second.operational = false;
                    send_join_p = true;
                }
            } 
            else 
            {
                send_join_p = true;
            }
        }
        if (input_map.contains_sa(ii->second.get_pid()))
        {
            uint32_t imseq = input_map.get_sa_safe_seq(ii->second.get_pid());
            uint32_t msseq = ii->second.get_safe_seq();

            if ((seqno_eq(imseq, SEQNO_MAX) && 
                 seqno_eq(msseq, SEQNO_MAX) == false) ||
                (seqno_eq(imseq, SEQNO_MAX) == false && 
                 seqno_eq(msseq, SEQNO_MAX) == false &&
                 seqno_gt(msseq, imseq)))
            {
                input_map.set_safe(ii->second.get_pid(), msseq);
            }
        }
    }
    
    // Update highest known seqno
    for (EVSMessage::InstMap::const_iterator ii = instances->begin(); 
         ii != instances->end(); ++ii) 
    {
        // Coming from the same view
        if (ii->second.get_view_id() == current_view.get_id()) 
        {
            // Update highest and lowest known high seqno
            if (!seqno_eq(ii->second.get_range().get_high(), SEQNO_MAX))
            {
                if ((seqno_eq(high_seq, SEQNO_MAX) || 
                     seqno_gt(ii->second.get_range().get_high(), high_seq))) 
                {
                    high_seq = ii->second.get_range().get_high();
                }
            }
        }
    }
    
    // Update lowest known seqno
    for (EVSMessage::InstMap::const_iterator ii = instances->begin(); 
         ii != instances->end(); ++ii) 
    {
        // Coming from the same view
        if (ii->second.get_view_id() == current_view.get_id())
        {
            if (seqno_eq(ii->second.get_range().get_low(), SEQNO_MAX))
            {
                low_seq = SEQNO_MAX;
                break;
            }
            else if (seqno_eq(low_seq, SEQNO_MAX) || 
                     seqno_lt(ii->second.get_range().get_low(), low_seq))
            {
                low_uuid = ii->first;
                low_seq = ii->second.get_range().get_low();
            }
        }
    }
    
    // Output must be empty to seqno completion to work correctly
    assert(output.empty());
    
    // last locally generated seqno is not equal to high seqno, generate
    // completing dummy message
    if (!seqno_eq(high_seq, SEQNO_MAX))
    {
        if (seqno_eq(last_sent, SEQNO_MAX) || seqno_lt(last_sent, high_seq)) 
        {
            complete_user(high_seq);
        }
        else if (msg.get_source() != my_addr && msg.get_source() == low_uuid)
        {
            EVSRange range(low_seq, high_seq);
            resend(msg.get_source(), EVSGap(my_addr, range));
        }
    }
    
    // Try recovery for all messages between low_seq/high_seq
    EVSRange range(low_seq, high_seq);
    for (InstMap::const_iterator i = known.begin(); i != known.end(); ++i)
    {
        if (i->second.operational == false)
        {
            recover(EVSGap(get_pid(i), range));
        }
    }

    return send_join_p;
}



void EVSProto::handle_join(const EVSMessage& msg, const UUID& source)
{
    if (msg.get_type() != EVSMessage::JOIN)
    {
        throw FatalException("invalid input");
    }
    LOG_DEBUG(self_string() + " join message " + msg.to_string());
    
    if (get_state() == LEAVING) {
        return;
    }
    
    InstMap::iterator i = known.find(source);
    if (i == known.end()) 
    {
        if (source == my_addr)
        {
            throw FatalException("");
        }
        LOG_DEBUG(self_string() + " handle_join(): new instance");
        std::pair<std::map<const UUID, EVSInstance>::iterator, bool> iret;
        iret = known.insert(make_pair(source,
                                      EVSInstance(msg.get_source_name())));
        assert(iret.second == true);
        i = iret.first;
        i->second.operational = true;
        if (get_state() == JOINING || get_state() == RECOVERY || 
            get_state() == OPERATIONAL) 
        {
            SHIFT_TO(RECOVERY);
        }
        // Set join after shift to recovery, str may clean up join messages
        set_join(msg, source);
        return;
    }
    
    if (msg_from_previous_view(previous_views, msg))
    {
        LOG_DEBUG("join message from one of the previous views " + 
                  msg.get_source_view().to_string());
        return;
    }
    
    if (install_message)
    {
        LOG_WARN(self_string() 
                 + " install message and received join, discarding");
        return;
    }
    
    if (get_state() == RECOVERY && install_message && is_consistent(msg)) 
    {
        LOG_DEBUG(self_string() + " redundant join message: "
                  + msg.to_string() + " install message: "
                  + install_message->to_string());
        return;
    }
    
    if ((get_state() == OPERATIONAL || install_message) && is_consistent(msg))
    {
        LOG_DEBUG(self_string() + " redundant join message in state "
                  + to_string(get_state()) + ": "
                  + msg.to_string() + " install message: "
                  + install_message->to_string());
        return;
    }


    i->second.tstamp = Time::now();    
    i->second.set_name(msg.get_source_name());
    
    bool send_join_p = false;
    if (get_state() == JOINING || get_state() == OPERATIONAL)
    {
        send_join_p = true;
        SHIFT_TO2(RECOVERY, false);
    }

    assert(i->second.installed == false);
    
    // Instance previously declared unoperational seems to be operational now
    if (i->second.operational == false) 
    {
        i->second.operational = true;
        LOG_DEBUG("unop -> op");
        send_join_p = true;
    } 
    
    if (msg.get_source_view() == current_view.get_id()) 
    {
        uint32_t prev_safe = input_map.get_safe_seq();
        if (!seqno_eq(msg.get_aru_seq(), SEQNO_MAX))
        {
            input_map.set_safe(source, msg.get_aru_seq());
        }
        if (!seqno_eq(prev_safe, input_map.get_safe_seq())) 
        {
            LOG_DEBUG("safe seq changed");
        }
        // Aru seqs are not the same
        if (!seqno_eq(msg.get_aru_seq(), input_map.get_aru_seq())) 
        {
            LOG_DEBUG(self_string() + " noneq aru, local "
                      + make_int(input_map.get_aru_seq()).to_string() 
                      + " msg "
                      + make_int(msg.get_aru_seq()).to_string());
        }
        
        // Safe seqs are not the same
        if (!seqno_eq(msg.get_seq(), input_map.get_safe_seq())) 
        {
            LOG_DEBUG(self_string() + " noneq safe seq, local " 
                      + make_int(input_map.get_safe_seq()).to_string() 
                      + " msg " 
                      + make_int(msg.get_seq()).to_string());
        }
    }
    
    // Store join message
    set_join(msg, source);
    
    // Converge towards consensus
    const EVSMessage::InstMap* instances = msg.get_instances();
    EVSMessage::InstMap::const_iterator selfi = instances->find(my_addr);
    if (selfi == instances->end()) 
    {
        // Source instance does not know about this instance, so there 
        // is no sense to compare states yet
        LOG_DEBUG("this instance not known by source instance");
        send_join_p = true;
    } 
    else if (current_view.get_id() != msg.get_source_view()) 
    {
        // Not coming from same views, there's no point to compare 
        // states further
        LOG_DEBUG(self_string() 
                  + " join from different view " 
                  + msg.get_source_view().to_string());
        if (is_consistent(msg) == false)
        {
            send_join_p = true;
        }
    } 
    else 
    {
        LOG_DEBUG("states compare");
        if (states_compare(msg) == true)
            send_join_p = true;
    }

    InstMap::const_iterator self_i = known.find(my_addr);
    if (self_i == known.end())
    {
        throw FatalException("");
    }
    if ((self_i->second.join_message == 0 ||
         is_consistent(*self_i->second.join_message) == false) &&
        send_join_p == true)
    {
        send_join_p = true;
    }
    else
    {
        send_join_p = false;
    }
    
    set_join(create_join(), my_addr);
    
    if (is_consensus())
    { 
        if (is_representative(my_addr))
        {
            LOG_DEBUG("is consensus and representative: " + to_string());
            send_install();
        }
    }    
    else if (send_join_p && output.empty() == true)
    {
        LOG_DEBUG("send join");
        send_join(false);
    }
}


void EVSProto::handle_leave(const EVSMessage& msg, const UUID& source)
{
    LOG_INFO("leave message at " + self_string() + " source: " +
              msg.get_source().to_string() + " source view: " + 
              msg.get_source_view().to_string());
    set_leave(msg, source);
    if (source == my_addr) 
    {
        /* Move all pending messages from output to input map */
        while (output.empty() == false)
        {
            pair<WriteBuf*, ProtoDownMeta> wb = output.front();
            if (send_user(wb.first, 
                          wb.second.get_user_type(), 
                          SAFE, 0, SEQNO_MAX, true) != 0)
            {
                throw FatalException("");
            }
            output.pop_front();
            delete wb.first;
        }
        /* Deliver all possible messages in reg view */
        deliver();
        setall_installed(false);
        InstMap::iterator ii = known.find(source);
        if (ii == known.end())
        {
            throw FatalException("");
        }
        ii->second.installed = true;
        deliver_trans_view(true);
        deliver_trans();
        deliver_empty_view();
        SHIFT_TO(CLOSED);
    } 
    else 
    {
        if (msg_from_previous_view(previous_views, msg))
        {
            LOG_DEBUG("leave message from previous view");
            return;
        }
        InstMap::iterator ii = known.find(source);
        if (ii == known.end()) {
            LOG_WARN("instance not found");
            return;
        }
        ii->second.operational = false;
        SHIFT_TO_P(this, RECOVERY, true);
        if (is_consensus() && is_representative(my_addr))
        {
            send_install();
        }
    }

}

void EVSProto::handle_install(const EVSMessage& msg, const UUID& source)
{
    
    if (get_state() == LEAVING) {
        LOG_DEBUG("dropping install message in leaving state");
        return;
    }

    LOG_DEBUG(self_string() + " " + msg.to_string());

    InstMap::iterator i = known.find(source);
    
    if (i == known.end()) 
    {
        std::pair<std::map<const UUID, EVSInstance>::iterator, bool> iret;
        iret = known.insert(make_pair(source,
                                      EVSInstance(msg.get_source_name())));
        assert(iret.second == true);
        i = iret.first;
        if (state == RECOVERY || state == OPERATIONAL) 
        {
            SHIFT_TO(RECOVERY);
        }
        return;	
    } 
    else if (state == JOINING || state == CLOSED) 
    {
        LOG_DEBUG("dropping install message from " + source.to_string());
        return;
    } 
    else if (i->second.operational == false) 
    {
        LOG_DEBUG("setting other as operational");
        i->second.operational = true;
        SHIFT_TO(RECOVERY);
        return;
    } 
    else if (msg_from_previous_view(previous_views, msg))
    {
        LOG_DEBUG("install message from previous view");
        return;
    }
    else if (install_message)
    {
        if (is_consistent(msg) && 
            msg.get_source_view() == install_message->get_source_view())
        {
            return;
        }
        LOG_DEBUG("what?");
        SHIFT_TO(RECOVERY);
        return;
    }
    else if (i->second.installed == true) 
    {
        LOG_DEBUG("what?");
        SHIFT_TO(RECOVERY);
        return;
    } 
    else if (is_representative(source) == false) 
    {
        LOG_DEBUG("source is not supposed to be representative");
        SHIFT_TO(RECOVERY);
        return;
    } 

    
    assert(install_message == 0);
    
    i->second.tstamp = Time::now();
    i->second.set_name(msg.get_source_name());
    
    if (is_consistent(msg))
    {
        install_message = new EVSMessage(msg);
        send_gap(my_addr, install_message->get_source_view(), 
                 EVSRange(SEQNO_MAX, SEQNO_MAX));
    }
    else
    {
        LOG_DEBUG(self_string() + " install message not consistent with state");
        SHIFT_TO_P(this, RECOVERY, true);
    }
}

END_GCOMM_NAMESPACE
