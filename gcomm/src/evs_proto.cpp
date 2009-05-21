
#include "evs_proto.hpp"
#include "gcomm/transport.hpp"


BEGIN_GCOMM_NAMESPACE

static bool msg_from_previous_view(const list<pair<View, Time> >& views, 
                                   const EVSMessage& msg)
{
    for (list<pair<View, Time> >::const_iterator i = views.begin();
         i != views.end(); ++i)
    {
        if (msg.get_source_view() == i->first.get_id())
        {
            LOG_DEBUG("message " + msg.to_string() + " from previous view " 
                      + i->first.get_id().to_string());
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
            LOG_WARN("detected inactive node: " +
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
            LOG_INFO("Erasing " + i->first.to_string() + " at " + self_string());
            known.erase(i);
        }
    }
}

void EVSProto::cleanup_views()
{
    Time now(Time::now());
    list<pair<View, Time> >::iterator i = previous_views.begin();
    while (i != previous_views.end())
    {
        if (i->second + Time(300, 0) < now)
        {
            LOG_WARN("erasing view: " + i->first.to_string());
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
    const View& prev_view(previous_views.back().first);
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
    for (std::map<const UUID, EVSInstance>::iterator i = known.begin();
         i != known.end(); ++i) {
        i->second.installed = val;
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
            i->second.trusted && 
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
        LOG_DEBUG("is_consensus(): no own join message");
        return false;
    }
    if (is_consistent(*my_jm) == false) 
    {
        LOG_INFO("is_consensus(): own join message is not consistent");
        return false;
    }
    
    for (std::map<const UUID, EVSInstance>::const_iterator 
             i = known.begin(); i != known.end(); ++i) {
        if (!(i->second.operational && i->second.trusted))
            continue;
        if (i->second.join_message == 0)
        {
            LOG_DEBUG("is_consensus(): no join message for " + get_pid(i).to_string());
            return false;
        }
        if (is_consistent(*i->second.join_message) == false)
        {
            LOG_DEBUG("is_consensus(): " + get_pid(i).to_string() + 
                      " join message not consistent");
            return false;
        }
    }
    LOG_DEBUG("consensus reached at " + self_string());
    return true;
}

bool EVSProto::is_representative(const UUID& pid) const
{
    for (std::map<const UUID, EVSInstance>::const_iterator i =
             known.begin();
         i != known.end(); ++i) {
        if (i->second.operational && i->second.trusted) {
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
    std::map<const UUID, EVSRange> local_insts;
    std::map<const UUID, EVSRange> jm_insts;

        // TODO/FIXME: 

    if (jm.get_source_view() == current_view.get_id()) {
        // Compare instances that originate from the current view and 
        // should proceed to next view

        // First check agains input map state
        if (!(seqno_eq(input_map.get_aru_seq(), jm.get_aru_seq()) &&
              seqno_eq(input_map.get_safe_seq(), jm.get_seq())))
        {
            LOG_DEBUG(self_string() + " not consistent: input map ");
            return false;
        }
        
        for (InstMap::const_iterator i = known.begin(); i != known.end(); ++i) 
        {
            if (i->second.operational == true && 
                i->second.trusted == true &&
                i->second.join_message && 
                i->second.join_message->get_source_view() == current_view.get_id())
                local_insts.insert(make_pair(i->first, 
                                             input_map.get_sa_gap(i->first)));
        }
        const std::map<UUID, EVSMessage::Instance>* jm_instances = 
            jm.get_instances();
        for (std::map<UUID, EVSMessage::Instance>::const_iterator 
                 i = jm_instances->begin(); i != jm_instances->end(); ++i) 
        {
            if (i->second.get_operational() == true && 
                i->second.get_trusted() == true &&
                i->second.get_left() == false &&
                i->second.get_view_id() == current_view.get_id()) {
                jm_insts.insert(
                    std::pair<const UUID, EVSRange>(
                        i->second.get_pid(),
                        i->second.get_range()));
            } 
        }
        if (jm_insts != local_insts)
        {
            LOG_DEBUG(self_string() 
                      + " not consistent: join message instances");
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
        
        for (std::map<const UUID, EVSInstance>::const_iterator 
                 i = known.begin(); i != known.end(); ++i)
        {
            if (!(i->second.operational == true && i->second.trusted == true))
            {
                local_insts.insert(std::pair<const UUID, EVSRange>(
                                       i->first, input_map.contains_sa(i->first) ?
                                       input_map.get_sa_gap(i->first) : EVSRange()));
            }
            const std::map<UUID, EVSMessage::Instance>* jm_instances =
                jm.get_instances();
            for (std::map<UUID, EVSMessage::Instance>::const_iterator
                     i = jm_instances->begin(); i != jm_instances->end(); ++i)
            {
                if (!(i->second.get_operational() == true &&
                      i->second.get_trusted() == true))
                {
                    jm_insts.insert(std::pair<const UUID,
                                    EVSRange>(i->second.get_pid(),
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
    } else {
        // Instances are originating from different view, need to check
        // only that new view is consistent
        for (std::map<const UUID, EVSInstance>::const_iterator i =
                 known.begin();
             i != known.end(); ++i) {
            if (i->second.operational == true && i->second.trusted == true
                &&
                i->second.join_message)
                local_insts.insert(std::pair<const UUID, EVSRange>(
                                       i->first, EVSRange()));
        }
        const std::map<UUID, EVSMessage::Instance>* jm_instances = 
            jm.get_instances();
        for (std::map<UUID, EVSMessage::Instance>::const_iterator 
                 i = jm_instances->begin(); i != jm_instances->end(); ++i) {
            if (i->second.get_operational() == true && 
                i->second.get_trusted() == true) {
                jm_insts.insert(
                    std::pair<const UUID, EVSRange>(
                        i->second.get_pid(), EVSRange()));
            } 
        }
        if (jm_insts != local_insts)
        {
            LOG_DEBUG(self_string() + " not consistent: local instances, different view");
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
    if (local == false && get_state() == OPERATIONAL && 
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
    if (local == false)
    {
        ret = pass_down(wb, 0); 
    }
    else
    {
        ret = 0;
    }
    if (ret == 0) 
    {
        last_sent = last_msg_seq;
        
        ReadBuf* rb = wb->to_readbuf();
        EVSRange range = input_map.insert(EVSInputMapItem(my_addr, msg, rb, 0));
        assert(seqno_eq(range.get_high(), last_sent));
        input_map.set_safe(my_addr, input_map.get_aru_seq());
        rb->release();
    }
    wb->rollback_hdr(msg.get_hdrlen());
    deliver();
    return ret;
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


EVSJoinMessage EVSProto::create_join() const
{
    EVSJoinMessage jm(my_addr,
                      my_name,
                      current_view.get_id(), 
                      input_map.get_aru_seq(), 
                      input_map.get_safe_seq());
    for (std::map<const UUID, EVSInstance>::const_iterator i = known.begin();
         i != known.end(); ++i)
    {
        const UUID& pid = get_pid(i);
        const EVSInstance& ei = get_instance(i);
        jm.add_instance(pid, 
                        ei.name,
                        ei.operational, 
                        ei.trusted,
                        (ei.leave_message ? true : false),
                        (ei.join_message ? 
                         ei.join_message->get_source_view() : 
                         (input_map.contains_sa(pid) ? current_view.get_id() : 
                          ViewId())), 
                        (input_map.contains_sa(pid) ? 
                         input_map.get_sa_gap(pid) : EVSRange()));
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

void EVSProto::send_join(bool handle)
{
    assert(output.empty());
    LOG_DEBUG("send join at " + self_string());
    EVSJoinMessage jm = create_join();
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
                       last_sent);
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
#if 0
    if (el)
    {
        Time start(Time::now());
        do
        {
            if (el->poll(50) < 0)
                break;
        }
        while (!seqno_eq(input_map.get_safe_seq(), seq) &&
               start + Time(1, 0) > Time::now());
    }
#endif // 0
    handle_leave(lm, my_addr);
}

void EVSProto::send_install()
{
    LOG_DEBUG("sending install at " + self_string() + " installing flag " + Int(installing).to_string());
    if (installing)
        return;
    std::map<const UUID, EVSInstance>::iterator self = known.find(my_addr);
    EVSInstallMessage im(my_addr,
                         my_name,
                         ViewId(my_addr, 
                                current_view.get_id().get_seq() + 1),
                         input_map.get_aru_seq(), 
                         input_map.get_safe_seq());
    for (std::map<const UUID, EVSInstance>::iterator i = known.begin();
         i != known.end(); ++i) 
    {
        const UUID& pid = get_pid(i);
        const EVSInstance& ei = get_instance(i);
        im.add_instance(pid, 
                        ei.name,
                        ei.operational,
                        ei.trusted,
                        (ei.leave_message ? true : false),
                        (ei.join_message ? 
                         ei.join_message->get_source_view() : 
                         (input_map.contains_sa(pid) ? 
                          current_view.get_id() : ViewId())), 
                        (input_map.contains_sa(pid) ? 
                         input_map.get_sa_gap(pid) : EVSRange()));
    }
    size_t bufsize = im.size();
    unsigned char* buf = new unsigned char[bufsize];
    if (im.write(buf, bufsize, 0) == 0)
        throw FatalException("");
    
    WriteBuf wb(buf, bufsize);
    int err;
    if ((err = pass_down(&wb, 0))) {
        LOG_WARN(std::string("send failed ") 
                 + strerror(err));
    }
    delete[] buf;
    installing = true;
    handle_install(im, my_addr);
}


void EVSProto::resend(const UUID& gap_source, const EVSGap& gap)
{
    assert(gap.source == my_addr);
    if (seqno_eq(gap.get_high(), SEQNO_MAX)) {
        LOG_DEBUG(std::string("empty gap") 
                  + UInt32(gap.get_low()).to_string() + " -> " 
                  + UInt32(gap.get_high()).to_string());
        return;
    } else if (!seqno_eq(gap.get_low(), SEQNO_MAX) &&
               seqno_gt(gap.get_low(), gap.get_high())) {
        LOG_DEBUG(std::string("empty gap") 
                  + UInt32(gap.get_low()).to_string() + " -> " 
                  + UInt32(gap.get_high()).to_string());
        return;
    }
    
    uint32_t start_seq = seqno_eq(gap.get_low(), SEQNO_MAX) ? 0 : gap.get_low();
    LOG_DEBUG("resending at " + self_string() + " requested by " + gap_source.to_string() + " " + UInt32(start_seq).to_string() + " -> " + UInt32(gap.get_high()).to_string());

    for (uint32_t seq = start_seq; !seqno_gt(seq, gap.get_high()); ) {


        std::pair<EVSInputMapItem, bool> i = input_map.recover(my_addr,
                                                               seq);
        if (i.second == false) {
            std::map<const UUID, EVSInstance>::iterator ii =
                known.find(gap_source);
            assert(ii != known.end());
            LOG_DEBUG(std::string("setting ") + ii->first.to_string() + " as untrusted at " + self_string());
            ii->second.trusted = false;
            if (get_state() != LEAVING) {
                SHIFT_TO(RECOVERY);
            }
            return;
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
            LOG_DEBUG("resend: " 
                     + make_int(i.first.get_payload_offset()).to_string());
            WriteBuf wb(rb ? rb->get_buf(i.first.get_payload_offset()) : 0, 
                        rb ? rb->get_len(i.first.get_payload_offset()) : 0);
            wb.prepend_hdr(new_msg.get_hdr(), new_msg.get_hdrlen());
            if (pass_down(&wb, 0))
                break;
            seq = seqno_add(seq, msg.get_seq_range() + 1);
        }
    }
}

void EVSProto::recover(const EVSGap& gap)
{
    
    LOG_DEBUG("recovering message");
    if (gap.get_low() == gap.get_high()) {
        LOG_WARN(std::string("EVSProto::recover(): Empty gap: ") 
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
        if (i.second == true) {
            const ReadBuf* rb = i.first.get_readbuf();
            const EVSMessage& msg = i.first.get_evs_message();
            WriteBuf wb(rb ? rb->get_buf(i.first.get_payload_offset()) : 0,
                        rb ? rb->get_len(i.first.get_payload_offset()) : 0);
            wb.prepend_hdr(msg.get_hdr(), msg.get_hdrlen());
            if (send_delegate(gap.source, &wb))
                break;
            seq = seqno_add(seq, msg.get_seq_range() + 1);
        } else {
            seq = seqno_next(seq);
        }
    }
}


void EVSProto::handle_msg(const EVSMessage& msg, const UUID& source,
                          const ReadBuf* rb, const size_t roff)
{

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
        LOG_INFO(self_string() + ": state change: " + 
                 to_string(state) + " -> " + to_string(s));
    }
    switch (s) {
    case CLOSED:
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
        setall_installed(false);
        delete install_message;
        install_message = 0;
        installing = false;
        if (is_set_consensus_timer())
        {
            unset_consensus_timer();
        }
        set_consensus_timer();
        if (output.empty() && send_j == true)
        {
            send_join(false);

        }
        state = RECOVERY;
        break;
    }
    case OPERATIONAL:
        // tp->set_loopback(false);
        assert(output.empty() == true);
        assert(is_consensus() == true);
        assert(is_all_installed() == true);
        unset_consensus_timer();
        deliver();
        deliver_trans_view(false);
        deliver_trans();
        // Reset input map
        input_map.clear();
        
        previous_views.push_back(make_pair(current_view, Time::now()));
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
        cleanup_unoperational();
        cleanup_views();
        LOG_DEBUG("new view: " + current_view.to_string());
        /*
         * while (output.empty() == false)
         * if (send_user())
         * break;
         */
        start_resend_timer();
        assert(get_state() == OPERATIONAL);
        break;
    default:
        throw FatalException("Invalid state");
    }
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
    LOG_INFO(msg.to_string());
    if (msg.get_type() != EVSMessage::USER)
    {
        throw FatalException("reg validate: not user message");
    }
    if (msg.get_source_view() != current_view.get_id())
    {
        throw FatalException("reg validate: not current view");
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
        LOG_DEBUG("msg with resend flag: " + msg.to_string());
    }


    LOG_DEBUG("this: " + self_string() + " source: "
              + source.to_string() + " seq: " 
              + UInt32(msg.get_seq()).to_string());
    std::map<const UUID, EVSInstance>::iterator i = known.find(source);
    if (i == known.end()) {
        // Previously unknown instance has appeared and it seems to
        // be operational, assume that it can be trusted and start
        // merge/recovery
        LOG_DEBUG("new instance");
        std::pair<std::map<const UUID, EVSInstance>::iterator, bool> iret;
        iret = known.insert(make_pair(source,
                                      EVSInstance(source.to_string())));
        assert(iret.second == true);
        i = iret.first;
        i->second.operational = true;
        if (state == JOINING || state == RECOVERY || state == OPERATIONAL) {
            SHIFT_TO(RECOVERY);
        }
        return;
    } else if (state == JOINING || state == CLOSED) {
        // Drop message
        LOG_DEBUG("dropping");
        return;
    } else if (!(msg.get_source_view() == current_view.get_id())) {
        if (get_state() == LEAVING) {
            LOG_DEBUG("dropping");
            return;
        }

        if (msg_from_previous_view(previous_views, msg))
        {
            LOG_DEBUG("user message from previous view");
            return;
        }
        
        if (i->second.trusted == false) {
            LOG_DEBUG("untrusted source");
            // Do nothing, just discard message
            return;
        } else if (i->second.operational == false) {
            // This is probably partition merge, see if it works out
            LOG_DEBUG("unoperational source");
            i->second.operational = true;
            SHIFT_TO(RECOVERY);
            return;
        } else if (i->second.installed == false) {
            if (install_message && 
                msg.get_source_view() == install_message->get_source_view()) {
                assert(state == RECOVERY);
                LOG_DEBUG("recovery user message source");
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
                
                if (is_consensus()) {
                    SHIFT_TO(OPERATIONAL);
                } else {
                    SHIFT_TO(RECOVERY);
                    return;
                }
            } else {
                // Probably caused by network partitioning during recovery
                // state, this will most probably lead to view 
                // partition/remerge. In order to do it in organized fashion,
                // don't trust the source instance during recovery phase.
                // LOG_WARN("Setting " + source.to_string() + " status to no-trust" + " on " + self_string());
                // i->second.trusted = false;
                // Note: setting other instance to non-trust here is too harsh
                // shift_to(RECOVERY);
                return;
            }
        } else {
            // i->second.trusted = false;
            // SHIFT_TO(RECOVERY);
            LOG_WARN("me: " 
                     + self_string()
                     + " unknown message: "
                     + msg.to_string());
            return;
        }
    } else if (i->second.trusted == false) {
        LOG_DEBUG(std::string("Message from untrusted ") 
                  + msg.get_source().to_string() 
                  + " at " + self_string());
        return;
    }
    
    assert(i->second.trusted == true && 
           i->second.operational == true &&
           (i->second.installed == true || get_state() == RECOVERY) &&
           msg.get_source_view() == current_view.get_id());



    const uint32_t prev_aru = input_map.get_aru_seq();
    const uint32_t prev_safe = input_map.get_safe_seq();
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
        !(msg.get_flags() & EVSMessage::F_RESEND)) {
        LOG_DEBUG("requesting at " + self_string() + " from " + source.to_string() + " " + range.to_string() + " due to input map gap, aru " + UInt32(input_map.get_aru_seq()).to_string());

#if 0
        std::list<EVSRange> gap_list = input_map.get_gap_list(source);
        for (std::list<EVSRange>::iterator gi = gap_list.begin();
             gi != gap_list.end(); ++gi) {
            LOG_DEBUG(gi->to_string());
            send_gap(source, current_view.get_id(), *gi);
        }
#else
        send_gap(source, current_view.get_id(), range);
#endif

    }
    
    if (!(i->first == my_addr) && 
        ((output.empty() && !(msg.get_flags() & EVSMessage::F_MSG_MORE)) ||
         get_state() == RECOVERY) && 
        /* !seqno_eq(range.get_high(), SEQNO_MAX) && */
        (seqno_eq(last_sent, SEQNO_MAX) || 
         seqno_lt(last_sent, range.get_high()))) {
        // Message not originated from this instance, output queue is empty
        // and last_sent seqno should be advanced
        LOG_DEBUG("sending dummy: " + UInt32(last_sent).to_string() + " -> " 
                  + UInt32(range.get_high()).to_string());
        WriteBuf wb(0, 0);
        send_user(&wb, 0xff, DROP, send_window, range.get_high());
    } else if (((output.empty() && 
                 (seqno_eq(input_map.get_aru_seq(), SEQNO_MAX) ||
                  !seqno_eq(input_map.get_aru_seq(), prev_aru))) &&
                !(msg.get_flags() & EVSMessage::F_RESEND)) || get_state() == LEAVING) {
        // Output queue empty and aru changed, send gap to inform others
        LOG_DEBUG("sending gap");
        send_gap(source, current_view.get_id(), EVSRange(SEQNO_MAX, SEQNO_MAX));
    }
    
    deliver();
    while (output.empty() == false)
    {
        if (send_user())
            break;
    }
    if (get_state() == RECOVERY && output.empty())
    {
        send_join();
    }
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
    if (i == known.end()) {
        std::pair<std::map<const UUID, EVSInstance>::iterator, bool> iret;
        iret = known.insert(make_pair(source,
                                      EVSInstance(source.to_string())));
        assert(iret.second == true);
        i = iret.first;
        i->second.operational = true;
        if (state == JOINING || state == RECOVERY || state == OPERATIONAL) {
            SHIFT_TO(RECOVERY);
        }
        return;
    } else if (state == JOINING || state == CLOSED) {	
        // Silent drop
        return;
    } else if (state == RECOVERY && install_message && 
               install_message->get_source_view() == msg.get_source_view()) {
        i->second.installed = true;
        if (is_all_installed())
            SHIFT_TO(OPERATIONAL);
        return;
    } else if (!(msg.get_source_view() == current_view.get_id())) {
        if (msg_from_previous_view(previous_views, msg))
        {
            LOG_DEBUG("gap message from previous view");
            return;
        }
        if (i->second.trusted == false) {
            // Do nothing, just discard message
        } else if (i->second.operational == false) {
            // This is probably partition merge, see if it works out
            i->second.operational = true;
            SHIFT_TO(RECOVERY);
        } else if (i->second.installed == false) {
            // Probably caused by network partitioning during recovery
            // state, this will most probably lead to view 
            // partition/remerge. In order to do it in organized fashion,
            // don't trust the source instance during recovery phase.
            // Note: setting other instance to non-trust here is too harsh
            // LOG_WARN("Setting source status to no-trust");
            // i->second.trusted = false;
            // SHIFT_TO(RECOVERY);
        } else {
            i->second.trusted = false;
            SHIFT_TO(RECOVERY);
        }
        return;
    } else if (i->second.trusted == false) {
        LOG_DEBUG(std::string("Message from untrusted ") 
                  + msg.get_source().to_string() 
                  + " at " + self_string());
        return;
    }
    
    assert(i->second.trusted == true && 
           i->second.operational == true &&
           (i->second.installed == true || get_state() == RECOVERY) &&
           msg.get_source_view() == current_view.get_id());
    
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
        !seqno_eq(prev_safe, input_map.get_safe_seq()) &&
        output.empty() == true)
    {
        send_join();
    }
}



bool EVSProto::states_compare(const EVSMessage& msg) 
{

    const std::map<UUID, EVSMessage::Instance>* instances = msg.get_instances();
    bool send_join_p = false;
    uint32_t high_seq = SEQNO_MAX;

    for (std::map<UUID, EVSMessage::Instance>::const_iterator 
             ii = instances->begin(); ii != instances->end(); ++ii) {
        
        std::map<UUID, EVSInstance>::iterator local_ii =
            known.find(ii->second.get_pid());
        
        if (local_ii->second.operational != ii->second.get_operational()) {
            if (local_ii->second.operational == true) {
                if (local_ii->second.tstamp + inactive_timeout <
                    Time::now()) {
                    LOG_DEBUG("setting " + local_ii->first.to_string() 
                              + " as unoperational at " + self_string());
                    local_ii->second.operational = false;
                    send_join_p = true;
                }
            } else {
                send_join_p = true;
            }
        }
        
        if (local_ii->second.trusted != ii->second.get_trusted()) {
            if (local_ii->second.trusted == true) {
                LOG_DEBUG("setting " + local_ii->first.to_string() 
                          + " as utrusted at " + self_string());
                local_ii->second.trusted = false;
            }
            send_join_p = true;
        }
        
        // Coming from the same view
        if (ii->second.get_view_id() == current_view.get_id()) {
            EVSRange range(input_map.get_sa_gap(ii->second.get_pid()));
            if (!seqno_eq(range.get_low(),
                          ii->second.get_range().get_low()) ||
                !seqno_eq(range.get_high(),
                          ii->second.get_range().get_high())) {
                assert(seqno_eq(range.get_low(), SEQNO_MAX) || 
                       !seqno_eq(range.get_high(), SEQNO_MAX));
                
                if (!seqno_eq(range.get_low(), SEQNO_MAX) &&
                    (seqno_eq(ii->second.get_range().get_high(), SEQNO_MAX) ||
                     seqno_gt(range.get_high(), 
                              ii->second.get_range().get_high()))) 
                {
                    // This instance knows more
                    // TODO: Optimize to recover only required messages
                    
                    recover(EVSGap(ii->second.get_pid(), range));
                    send_join_p = true;
                } else {
                    // The other instance knows more, sending join message
                    // should generate message recovery on more knowledgeble
                    // instance
                    send_join_p = true;
                }
            }

            // Update highest known seqno
            if (!seqno_eq(ii->second.get_range().get_high(), SEQNO_MAX) &&
                (seqno_eq(high_seq, SEQNO_MAX) || 
                 seqno_gt(ii->second.get_range().get_high(), high_seq))) {
                high_seq = ii->second.get_range().get_high();
            }
        }
    }
    
    // last locally generated seqno is not equal to high seqno, generate
    // completing dummy message
    if (!seqno_eq(high_seq, SEQNO_MAX) &&
        (seqno_eq(last_sent, SEQNO_MAX) || 
         seqno_lt(last_sent, high_seq))) {
        LOG_DEBUG("completing seqno to " + UInt32(high_seq).to_string());
        WriteBuf wb(0, 0);
        send_user(&wb, 0xff, DROP, send_window, high_seq);
    }
    
    
    return send_join_p;
}



void EVSProto::handle_join(const EVSMessage& msg, const UUID& source)
{
    if (msg.get_type() != EVSMessage::JOIN)
    {
        throw FatalException("invalid input");
    }
    LOG_DEBUG(self_string() + " source: " +
              msg.get_source().to_string() + " source view: " + 
              msg.get_source_view().to_string());
    
    
    if (get_state() == LEAVING) {
        return;
    }
    
    std::map<const UUID, EVSInstance>::iterator i = known.find(source);
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
        i->second.join_message = new EVSMessage(msg);
        if (get_state() == JOINING || get_state() == RECOVERY || 
            get_state() == OPERATIONAL) {
            SHIFT_TO(RECOVERY);
        }
        return;
    } else if (i->second.trusted == false) {
        LOG_DEBUG("untrusted");
        // Silently drop
        return;
    }
    
    if (msg_from_previous_view(previous_views, msg))
    {
        LOG_DEBUG("join message from one of the previous views " + 
                  msg.get_source_view().to_string());
        return;
    }
    
    if (state == RECOVERY && install_message && is_consistent(msg)) {
        LOG_DEBUG("redundant join message");
        return;
    }
    
    i->second.tstamp = Time::now();    
    i->second.set_name(msg.get_source_name());
    
    bool send_join_p = false;
    if (get_state() == JOINING || get_state() == OPERATIONAL || 
        install_message) 
    {
        send_join_p = true;
        SHIFT_TO2(RECOVERY, false);
    }
    
    assert(i->second.trusted == true && i->second.installed == false);
    
    // Instance previously declared unoperational seems to be operational now
    if (i->second.operational == false) 
    {
        i->second.operational = true;
        LOG_DEBUG("unop -> op");
        send_join_p = true;
    } 
    
    if (msg.get_source_view() == current_view.get_id()) {
        uint32_t prev_safe = input_map.get_safe_seq();
        if (!seqno_eq(msg.get_aru_seq(), SEQNO_MAX))
            input_map.set_safe(source, msg.get_aru_seq());
        if (!seqno_eq(prev_safe, input_map.get_safe_seq())) {
            LOG_DEBUG("safe seq changed");
            send_join_p = true;
        }
        // Aru seqs are not the same
        if (!seqno_eq(msg.get_aru_seq(), input_map.get_aru_seq())) {
            LOG_DEBUG("noneq aru");
        }
        
        // Safe seqs are not the same
        if (!seqno_eq(msg.get_seq(), input_map.get_safe_seq())) {
            LOG_DEBUG("noneq seq, local " + UInt32(input_map.get_safe_seq()).to_string() + " msg " + UInt32(msg.get_seq()).to_string());
        }
    }
    
    // Store join message
    set_join(msg, source);
    
    // Converge towards consensus
    const std::map<UUID, EVSMessage::Instance>* instances = 
        msg.get_instances();
    std::map<UUID, EVSMessage::Instance>::const_iterator selfi = 
        instances->find(my_addr);
    if (selfi == instances->end()) {
        // Source instance does not know about this instance, so there 
        // is no sense to compare states yet
        LOG_DEBUG("this instance not known by source instance");
        send_join_p = true;
    } else if (selfi->second.get_trusted() == false) {
        // Source instance does not trust this instance, this feeling
        // must be mutual
        LOG_DEBUG("mutual untrust");
        i->second.trusted = false;
        send_join_p = true;
    } else if (!(current_view.get_id() == msg.get_source_view())) {
        // Not coming from same views, there's no point to compare 
        // states further
        LOG_DEBUG(std::string("different view ") +
                  msg.get_source_view().to_string());
        send_join_p = true;
    } else {
        LOG_DEBUG("states compare");
        if (states_compare(msg))
            send_join_p = true;
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
        ii->second.trusted = false;
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

    LOG_DEBUG("this: " + self_string() + " source: " +
              msg.get_source().to_string() + " source view: " + 
              msg.get_source_view().to_string());
    std::map<UUID, EVSInstance>::iterator i = known.find(source);
    
    if (i == known.end()) {
        std::pair<std::map<const UUID, EVSInstance>::iterator, bool> iret;
        iret = known.insert(make_pair(source,
                                      EVSInstance(msg.get_source_name())));
        assert(iret.second == true);
        i = iret.first;
        if (state == RECOVERY || state == OPERATIONAL) {
            SHIFT_TO(RECOVERY);
        }
        return;	
    } else if (state == JOINING || state == CLOSED) {
        LOG_DEBUG("dropping install message from " + source.to_string());
        return;
    } else if (i->second.trusted == false) {
        LOG_DEBUG("dropping install message from " + source.to_string());
        return;
    } else if (i->second.operational == false) {
        LOG_DEBUG("setting other as operational");
        i->second.operational = true;
        SHIFT_TO(RECOVERY);
        return;
    } else if (install_message || i->second.installed == true) {
        LOG_DEBUG("woot?");
        SHIFT_TO(RECOVERY);
        return;
    } else if (is_representative(source) == false) {
        LOG_DEBUG("source is not supposed to be representative");
        SHIFT_TO(RECOVERY);
        return;
    } else if (is_consensus() == false) {
        LOG_DEBUG("is not consensus");
        SHIFT_TO(RECOVERY);
        return;
    }
    else if (msg_from_previous_view(previous_views, msg))
    {
        LOG_DEBUG("install message from previous view");
        SHIFT_TO(RECOVERY);
        return;
    }
    
    assert(install_message == 0);

    i->second.tstamp = Time::now();
    i->second.set_name(msg.get_source_name());
    
    install_message = new EVSMessage(msg);
    send_gap(my_addr, install_message->get_source_view(), 
             EVSRange(SEQNO_MAX, SEQNO_MAX));
}

END_GCOMM_NAMESPACE
