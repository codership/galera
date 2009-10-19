/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * @file Unit tests for refactored EVS
 */



#include "evs_proto.hpp"
#include "evs_input_map2.hpp"
#include "evs_message2.hpp"
#include "evs_seqno.hpp"

#include "check_gcomm.hpp"
#include "check_templ.hpp"

#include <vector>
#include <set>

#include "check.h"


using namespace std;
using namespace gcomm;
using namespace gcomm::evs;


START_TEST(test_seqno)
{
    log_info << "START";
    Seqno s0(0), s1(1), 
        sk(static_cast<uint16_t>(Seqno::max().get()/2)), 
        sn(static_cast<uint16_t>(Seqno::max().get() - 1));
    
    fail_unless(s0 - 1 == sn);
    
    fail_unless(s1 == s1);
    fail_unless(s0 != s1);
    
    fail_unless(s0 < s1);
    fail_unless(s0 <= s1);
    fail_unless(s1 > s0);
    fail_unless(s1 >= s0);
    
    fail_unless(s1 >= s1);
    fail_unless(s1 <= s1);
    
    fail_unless(sn < s0);
    fail_unless(sn <= s0);
    fail_unless(s0 > sn);
    fail_unless(s0 >= sn);
    
    fail_unless(sk > s0);
    fail_unless(sk < sn);
    
    Seqno ss(0x7aba);
    check_serialization(ss, 2, Seqno());
    
}
END_TEST

START_TEST(test_range)
{
    log_info << "START";
    Range r(3, 6);

    check_serialization(r, 2*Seqno::serial_size(), Range());

}
END_TEST

START_TEST(test_message)
{
    log_info << "START";
    UUID uuid1(0, 0);
    ViewId view_id(V_TRANS, uuid1, 4567);
    Seqno seq(478), aru_seq(456), seq_range(7);
    
    UserMessage um(uuid1, view_id, seq, aru_seq, seq_range, SP_SAFE, 75433, 0xab,
                   Message::F_SOURCE);
    
    check_serialization(um, um.serial_size(), UserMessage());
    
    DelegateMessage dm(uuid1, view_id);
    dm.set_source(uuid1);
    check_serialization(dm, dm.serial_size(), DelegateMessage());
    
    MessageNodeList node_list;
    node_list.insert(make_pair(uuid1, MessageNode()));
    node_list.insert(make_pair(UUID(2), MessageNode(true, true, ViewId(V_REG), 5,
                                                    Range(7, 8))));
    JoinMessage jm(uuid1, view_id, 8, 5, 27, &node_list);
    jm.set_source(uuid1);
    check_serialization(jm, jm.serial_size(), JoinMessage());
    
    InstallMessage im(uuid1, view_id, 8, 5, 27, &node_list);
    im.set_source(uuid1);
    check_serialization(im, im.serial_size(), InstallMessage());
    
    LeaveMessage lm(uuid1, view_id, 45, 88, 3456);
    lm.set_source(uuid1);
    check_serialization(lm, lm.serial_size(), LeaveMessage());
}
END_TEST

START_TEST(test_input_map_insert)
{
    log_info << "START";
    InputMap im;
    UUID uuid1(1), uuid2(2);
    ViewId view(V_REG, uuid1, 0);

    try 
    {
        im.insert(uuid1, UserMessage(uuid1, view, 0));
        fail("");
    } 
    catch (...) { }
    
    im.insert_uuid(uuid1);
    
    im.insert(uuid1, UserMessage(uuid1, view, 0));
    
    try 
    { 
        im.insert(uuid1, 
                  UserMessage(uuid1, view, 
                              static_cast<uint16_t>(Seqno::max().get() - 1))); 
        fail("");
    }
    catch (...) { }
    
    try
    {
        im.insert_uuid(uuid2);
        fail("");
    }
    catch (...) { }

    im.clear();

    im.insert_uuid(uuid1);
    im.insert_uuid(uuid2);

    for (Seqno s = 0; s < 10; ++s)
    {
        im.insert(uuid1, UserMessage(uuid1, view, s));
        im.insert(uuid2, UserMessage(uuid1, view, s));
    }

    for (Seqno s = 0; s < 10; ++s)
    {
        InputMap::iterator i = im.find(uuid1, s);
        fail_if(i == im.end());
        fail_unless(InputMap::MsgIndex::get_value(i).get_uuid() == uuid1);
        fail_unless(InputMap::MsgIndex::get_value(i).get_msg().get_seq() == s);

        i = im.find(uuid2, s);
        fail_if(i == im.end());
        fail_unless(InputMap::MsgIndex::get_value(i).get_uuid() == uuid2);
        fail_unless(InputMap::MsgIndex::get_value(i).get_msg().get_seq() == s);
    }
    
}
END_TEST

START_TEST(test_input_map_find)
{
    log_info << "START";
    InputMap im;
    UUID uuid1(1);
    ViewId view(V_REG, uuid1, 0);
    
    im.insert_uuid(uuid1);
    
    im.insert(uuid1, UserMessage(uuid1, view, 0));
    
    fail_if(im.find(uuid1, 0) == im.end());
    

    im.insert(uuid1, UserMessage(uuid1, view, 2));
    im.insert(uuid1, UserMessage(uuid1, view, 4));
    im.insert(uuid1, UserMessage(uuid1, view, 7));

    fail_if(im.find(uuid1, 2) == im.end());
    fail_if(im.find(uuid1, 4) == im.end());
    fail_if(im.find(uuid1, 7) == im.end());

    fail_unless(im.find(uuid1, 3) == im.end());
    fail_unless(im.find(uuid1, 5) == im.end());
    fail_unless(im.find(uuid1, 6) == im.end());
    fail_unless(im.find(uuid1, 8) == im.end());
}
END_TEST

START_TEST(test_input_map_safety)
{
    log_info << "START";
    InputMap im;
    UUID uuid1(1);
    ViewId view(V_REG, uuid1, 0);
    
    im.insert_uuid(uuid1);
    
    im.insert(uuid1, UserMessage(uuid1, view, 0));
    fail_unless(im.get_aru_seq() == 0);
    im.insert(uuid1, UserMessage(uuid1, view, 1));
    fail_unless(im.get_aru_seq() == 1);
    im.insert(uuid1, UserMessage(uuid1, view, 2));
    fail_unless(im.get_aru_seq() == 2);
    im.insert(uuid1, UserMessage(uuid1, view, 3));
    fail_unless(im.get_aru_seq() == 3);
    im.insert(uuid1, UserMessage(uuid1, view, 5));
    fail_unless(im.get_aru_seq() == 3);    
    
    im.insert(uuid1, UserMessage(uuid1, view, 4));
    fail_unless(im.get_aru_seq() == 5);
    
    InputMap::iterator i = im.find(uuid1, 0);
    fail_unless(im.is_fifo(i) == true);
    fail_unless(im.is_agreed(i) == true);
    fail_if(im.is_safe(i) == true);
    im.set_safe_seq(uuid1, 0);
    fail_unless(im.is_safe(i) == true);
    
    im.set_safe_seq(uuid1, 5);
    i = im.find(uuid1, 5);
    fail_unless(im.is_safe(i) == true);
    
    im.insert(uuid1, UserMessage(uuid1, view, 7));
    im.set_safe_seq(uuid1, im.get_aru_seq());
    i = im.find(uuid1, 7);
    fail_if(im.is_safe(i) == true);

}
END_TEST

START_TEST(test_input_map_erase)
{
    log_info << "START";
    InputMap im;
    UUID uuid1(1);
    ViewId view(V_REG, uuid1, 1);
    im.insert_uuid(uuid1);

    for (Seqno s = 0; s < 10; ++s)
    {
        im.insert(uuid1, UserMessage(uuid1, view, s));
    }
    
    for (Seqno s = 0; s < 10; ++s)
    {
        InputMap::iterator i = im.find(uuid1, s);
        fail_unless(i != im.end());
        im.erase(i);
        i = im.find(uuid1, s);
        fail_unless(i == im.end());
        (void)im.recover(uuid1, s);
    }
    im.set_safe_seq(uuid1, 9);
    try
    {
        im.recover(uuid1, 9);
        fail("");
    }
    catch (...) { }
}
END_TEST

START_TEST(test_input_map_overwrap)
{
    log_info << "START";
    InputMap im;
    
    ViewId view(V_REG, UUID(1), 1);
    vector<UUID> uuids;
    for (uint32_t n = 1; n <= 5; ++n)
    {
        uuids.push_back(UUID(n));
    }

    for (vector<UUID>::const_iterator i = uuids.begin(); i != uuids.end(); ++i)
    {
        im.insert_uuid(*i);
    }
    
    Time start(Time::now());
    size_t cnt(0);
    Seqno last_safe(Seqno::max());
    for (size_t n = 0; n < Seqno::max().get()*3LU; ++n)
    {

        Seqno seq(static_cast<uint16_t>(n % Seqno::max().get()));
        for (vector<UUID>::const_iterator i = uuids.begin(); i != uuids.end();
             ++i)
        {
            UserMessage um(*i, view, seq);
            (void)im.insert(*i, um);
            if ((n + 5) % 10 == 0)
            {
                last_safe = um.get_seq() - 3;
                im.set_safe_seq(*i, last_safe);
                for (InputMap::iterator ii = im.begin(); 
                     ii != im.end() && im.is_safe(ii) == true;
                     ii = im.begin())
                {
                    im.erase(ii);
                }
            }
            cnt++;
        }
        fail_unless(im.get_aru_seq() == seq);
        fail_unless(im.get_safe_seq() == last_safe);
    }
    Time stop(Time::now());
    
    log_info << "input map msg rate " << double(cnt)/(stop - start).to_double();

}
END_TEST


class InputMapInserter
{
public:
    InputMapInserter(InputMap& im_) : im(im_) { }
    
    void operator()(const UserMessage& um) const
    {
        im.insert(um.get_source(), um);
    }
private:
    InputMap& im;
};

START_TEST(test_input_map_random_insert)
{
    log_info << "START";
    size_t n_seqnos(Seqno::max().get()/4);
    size_t n_uuids(4);
    vector<UUID> uuids(n_uuids);
    vector<UserMessage> msgs(n_uuids*n_seqnos);
    ViewId view_id(V_REG, UUID(1), 1);
    InputMap im;
    
    for (size_t i = 0; i < n_uuids; ++i)
    {
        uuids[i] = (static_cast<int32_t>(i + 1));
        im.insert_uuid(uuids[i]);
    }
    
    for (size_t j = 0; j < n_seqnos; ++j)
    {
        for (size_t i = 0; i < n_uuids; ++i)
        {
            msgs[j*n_uuids + i] = 
                UserMessage(uuids[i],
                            view_id,
                            static_cast<uint16_t>(j % Seqno::max().get()));
        }
    }
    
    vector<UserMessage> random_msgs(msgs);
    random_shuffle(random_msgs.begin(), random_msgs.end());
    for_each(random_msgs.begin(), random_msgs.end(), InputMapInserter(im));
    
    size_t n = 0;
    for (InputMap::iterator i = im.begin(); i != im.end(); ++i)
    {
        const InputMap::Msg& msg(InputMap::MsgIndex::get_value(i));
        fail_unless(msg.get_uuid() == msg.get_msg().get_source());
        fail_unless(msg.get_msg() == msgs[n]);
        fail_if(im.is_safe(i) == true);
        ++n;
    }
 
    fail_unless(im.get_aru_seq() == Seqno(static_cast<uint16_t>(n_seqnos - 1)));
    fail_unless(im.get_safe_seq() == Seqno::max());

    for (size_t i = 0; i < n_uuids; ++i)
    {
        fail_unless(im.get_range(uuids[i]) == 
                    Range(static_cast<uint16_t>(n_seqnos),
                          static_cast<uint16_t>(n_seqnos - 1)));
                                                                  
        im.set_safe_seq(uuids[i], static_cast<uint16_t>(n_seqnos - 1));
    }
    fail_unless(im.get_safe_seq() == static_cast<uint16_t>(n_seqnos - 1));
   
}
END_TEST

class Msg
{
public:
    Msg(const UUID& source_           = UUID::nil(), 
        const ViewId& source_view_id_ = ViewId(),
        const int64_t seq_            = -1) : 
        source(source_), 
        source_view_id(source_view_id_), 
        seq(seq_) 
    { }
    
    const UUID& get_source() const { return source; }

    const ViewId& get_source_view_id() const { return source_view_id; }
    
    int64_t get_seq() const { return seq; }
    
    bool operator==(const Msg& cmp) const
    {
        return (source         == cmp.source         && 
                source_view_id == cmp.source_view_id &&
                seq            == cmp.seq              );  
        
    }

private:
    UUID    source;
    ViewId  source_view_id;
    int64_t seq;
};

ostream& operator<<(ostream& os, const Msg& msg)
{
    return (os << "(" << msg.get_source() << "," << msg.get_source_view_id() << "," << msg.get_seq() << ")");
}

class ViewTrace
{
public:
    ViewTrace(const View& view_) : view(view_), msgs() { }
    
    void insert_msg(const Msg& msg)
        throw (gu::Exception)
    {
        gcomm_assert(contains(msg.get_source()) == true) 
            << "msg source " << msg.get_source() << " not int view " << view;
        gcomm_assert(view.get_id() == msg.get_source_view_id());
        msgs.push_back(msg);
    }
    
    const View& get_view() const { return view; }
    
    const deque<Msg>& get_msgs() const { return msgs; }
    
    bool operator==(const ViewTrace& cmp) const
    {
        // Note: Cannot compare joining members since seen differently
        // on different merging subsets
        return (view.get_members()     == cmp.view.get_members()     && 
                view.get_left()        == cmp.view.get_left()        &&
                view.get_partitioned() == cmp.view.get_partitioned() &&
                msgs                   == cmp.msgs                     );
    }
private:
    
    bool contains(const UUID& uuid) const
    {
        return (view.get_members().find(uuid) != view.get_members().end() ||
                view.get_left().find(uuid)    != view.get_left().end() ||
                view.get_partitioned().find(uuid) != view.get_partitioned().end());
    }
    
    View       view;
    deque<Msg> msgs;
};

ostream& operator<<(ostream& os, const ViewTrace& vtr)
{
    os << vtr.get_view() << ": ";
    copy(vtr.get_msgs().begin(), vtr.get_msgs().end(),
         ostream_iterator<const Msg>(os, " "));
    return os;
}



class Trace
{
public:
    class ViewTraceMap : public Map<ViewId, ViewTrace> { };

    Trace() : views(), current_view(views.end()) { }

    void insert_view(const View& view)
    {
        gu_trace(current_view = views.insert_checked(
                     make_pair(view.get_id(), ViewTrace(view))));
        
    }
    void insert_msg(const Msg& msg)
    {
        gcomm_assert(current_view != views.end()) << "no view set before msg delivery";
        gu_trace(ViewTraceMap::get_value(current_view).insert_msg(msg));
    }
    const ViewTraceMap& get_views() const { return views; }
private:
    ViewTraceMap views;
    ViewTraceMap::iterator current_view;
};


ostream& operator<<(ostream& os, const Trace& tr)
{
    os << "trace: \n";
    os << tr.get_views();
    return os;
}

class DummyUser : public Toplay
{
    
public:
    DummyUser(const UUID& uuid_) : uuid(uuid_), curr_seq(0), tr() { }
    ~DummyUser()
    {
        log_info << uuid << " " << tr;
    }
    void handle_up(int cid, const ReadBuf* rb, size_t offset,
                   const ProtoUpMeta& um)
    {
        log_debug << uuid << ": " << um;
        if (um.has_view() == true)
        {
            gu_trace(tr.insert_view(um.get_view()));
        }
        else
        {
            int64_t seq;
            gu_trace((void)unserialize(rb->get_buf(), rb->get_len(), 
                                       offset, &seq));
            gu_trace(tr.insert_msg(
                         Msg(um.get_source(), um.get_source_view_id(), seq)));
        }
    }

    void send()
    {
        const int64_t seq(curr_seq);
        byte_t buf[sizeof(seq)];
        size_t sz;
        gu_trace(sz = serialize(seq, buf, sizeof(buf), 0));
        WriteBuf wb(buf, sz);
        int err = pass_down(&wb, ProtoDownMeta(0));
        if (err != 0)
        {
            log_warn << "failed to send: " << strerror(err);
        }
        else
        {
            ++curr_seq;
        }
    }

    const Trace& get_trace() const { return tr; }

private:
    UUID uuid;
    int64_t curr_seq;
    Trace tr;
};


static ReadBuf* get_msg(DummyTransport* tp, Message* msg, bool release = true)
{
    ReadBuf* rb = tp->get_out();
    if (rb != 0)
    {
        gu_trace(Proto::unserialize_message(tp->get_uuid(), rb, 0, msg));
        if (release == true)
        {
            rb->release();
        }
    }
    return rb;
}

static void single_join(DummyTransport* t, Proto* p)
{
    Message jm, im, gm;
    
    // Initial state is joining
    p->shift_to(Proto::S_JOINING);
    
    // Send join must produce emitted join message
    p->send_join();
    
    ReadBuf* rb = get_msg(t, &jm);
    fail_unless(rb != 0);
    fail_unless(jm.get_type() == Message::T_JOIN);
    
    // Install message is emitted at the end of JOIN handling
    // 'cause this is the only instance and is always consistent
    // with itself
    rb = get_msg(t, &im);
    fail_unless(rb != 0);
    fail_unless(im.get_type() == Message::T_INSTALL);
    
    // Handling INSTALL message must emit gap message
    rb = get_msg(t, &gm);
    fail_unless(rb != 0);
    fail_unless(gm.get_type() == Message::T_GAP);
    
    // State must have evolved JOIN -> S_RECOVERY -> S_OPERATIONAL
    fail_unless(p->get_state() == Proto::S_OPERATIONAL);
    
    // Handle join message again, must stay in S_OPERATIONAL, must not
    // emit anything
    p->handle_msg(jm);
    rb = get_msg(t, &gm);
    fail_unless(rb == 0);
    fail_unless(p->get_state() == Proto::S_OPERATIONAL);
    
}

START_TEST(test_proto_single_join)
{
    log_info << "START";
    EventLoop el;
    UUID uuid(1);
    DummyTransport t(uuid);
    DummyUser u(uuid);
    Proto p(&el, &t, uuid, 0);
    connect(&t, &p);
    connect(&p, &u);
    single_join(&t, &p);
}
END_TEST

static void double_join(DummyTransport* t1, Proto* p1,
                        DummyTransport* t2, Proto* p2)
{

    Message jm;
    Message im;
    Message gm;
    Message gm2;
    Message msg;

    ReadBuf* rb;

    // Initial states check
    p2->shift_to(Proto::S_JOINING);
    fail_unless(p1->get_state() == Proto::S_OPERATIONAL);
    fail_unless(p2->get_state() == Proto::S_JOINING);

    // Send join message, don't self handle immediately
    // Expected output: one join message
    p2->send_join(false);
    fail_unless(p2->get_state() == Proto::S_JOINING);
    rb = get_msg(t2, &jm);
    fail_unless(rb != 0);
    fail_unless(jm.get_type() == Message::T_JOIN);
    rb = get_msg(t2, &msg);
    fail_unless(rb == 0);
    
    // Handle node 2's join on node 1
    // Expected output: shift to S_RECOVERY and one join message
    p1->handle_msg(jm);
    fail_unless(p1->get_state() == Proto::S_RECOVERY);
    rb = get_msg(t1, &jm);
    fail_unless(rb != 0);
    fail_unless(jm.get_type() == Message::T_JOIN);
    rb = get_msg(t1, &msg);
    fail_unless(rb == 0);
    
    // Handle node 1's join on node 2
    // Expected output: shift to S_RECOVERY and one join message
    p2->handle_msg(jm);
    fail_unless(p2->get_state() == Proto::S_RECOVERY);
    rb = get_msg(t2, &jm);
    fail_unless(rb != 0);
    fail_unless(jm.get_type() == Message::T_JOIN);
    rb = get_msg(t2, &msg);
    fail_unless(rb == 0);
    
    // Handle node 2's join on node 1
    // Expected output: Install and gap messages, state stays in S_RECOVERY
    p1->handle_msg(jm);
    fail_unless(p1->get_state() == Proto::S_RECOVERY);
    rb = get_msg(t1, &im);
    fail_unless(rb != 0);
    fail_unless(im.get_type() == Message::T_INSTALL);
    rb = get_msg(t1, &gm);
    fail_unless(rb != 0);
    fail_unless(gm.get_type() == Message::T_GAP);
    rb = get_msg(t1, &msg);
    fail_unless(rb == 0);
    
    // Handle install message on node 2
    // Expected output: Gap message and state stays in S_RECOVERY
    p2->handle_msg(im);
    fail_unless(p2->get_state() == Proto::S_RECOVERY);
    rb = get_msg(t2, &gm2);
    fail_unless(rb != 0);
    fail_unless(gm2.get_type() == Message::T_GAP);
    rb = get_msg(t2, &msg);
    fail_unless(rb == 0);
    
    // Handle gap messages
    // Expected output: Both nodes shift to S_OPERATIONAL, no messages
    // sent
    p1->handle_msg(gm2);
    fail_unless(p1->get_state() == Proto::S_OPERATIONAL);
    rb = get_msg(t1, &msg);
    fail_unless(rb == 0);
    p2->handle_msg(gm);
    fail_unless(p2->get_state() == Proto::S_OPERATIONAL);
    rb = get_msg(t2, &msg);
    fail_unless(rb == 0);
}


START_TEST(test_proto_double_join)
{
    log_info << "START";
    EventLoop el;
    UUID uuid1(1), uuid2(2);
    DummyTransport t1(uuid1), t2(uuid2);
    DummyUser u1(uuid1), u2(uuid2);
    Proto p1(&el, &t1, uuid1, 0), p2(&el, &t2, uuid2, 0);

    connect(&t1, &p1);
    connect(&p1, &u1);

    connect(&t2, &p2);
    connect(&p2, &u2);

    single_join(&t1, &p1);
    double_join(&t1, &p1, &t2, &p2);

}
END_TEST

class DummyNode
{
public:
    DummyNode(size_t idx, EventLoop* el) : 
        index (idx),
        uuid  (UUID(static_cast<int32_t>(idx))),
        u     (uuid), 
        t     (uuid),
        p     (el, &t, uuid, 0),
        cvi   ()
    { 
        connect(&t, &p);
        connect(&p, &u);
    }
    
    ~DummyNode()
    {
        disconnect(&t, &p);
        disconnect(&p, &u);
    }
    
    DummyTransport* get_tp() { return &t; }

    const UUID& get_uuid() const { return uuid; }

    size_t get_index() const { return index; }

    void join(bool first)
    {
        gu_trace(p.shift_to(Proto::S_JOINING));
        gu_trace(p.send_join(first));
    }
    
    void leave()
    {
        gu_trace(p.shift_to(Proto::S_LEAVING));
        gu_trace(p.send_leave());
    }

    void send()
    {
        gu_trace(u.send());
    }

    const Trace& get_trace() const { return u.get_trace(); }
    
    void set_cvi(const ViewId& vi) { cvi = vi; }

    bool in_cvi() const 
    { 
        return (u.get_trace().get_views().empty() == false &&
                u.get_trace().get_views().find(cvi) != 
                u.get_trace().get_views().end()); 
    }

    void expire_timers()
    {
        p.handle_send_join_timer();
    }
    
private:
    size_t index;
    UUID uuid;
    DummyUser u;
    DummyTransport t;
    Proto p;
    ViewId cvi;
};


class ChannelMsg
{
public:
    ChannelMsg(const ReadBuf* rb_, const UUID& source_) :
        rb(rb_ != 0 ? rb_->copy() : 0),
        source(source_)
    { }
    ReadBuf* get_rb() { return rb; }
    const UUID& get_source() { return source; }
private:
    ReadBuf* rb;
    UUID source;
};

class Channel
{
public:
    Channel(const size_t ttl_ = 1, 
            const size_t latency_ = 1, 
            const double loss_ = 1.) :
        ttl(ttl_),
        latency(latency_),
        loss(loss_),
        queue()
    { }

    void put(const ReadBuf* rb, const UUID& source) 
    { 
        queue.push_back(make_pair(latency, ChannelMsg(rb, source)));
    }

    ChannelMsg get()
    {
        while (queue.empty() == false)
        {
            pair<size_t, ChannelMsg>& p(queue.front());
            if (p.first == 0)
            {
                // todo: packet loss goes here
                if (get_loss() < 1.)
                {
                    double rnd(double(rand())/double(RAND_MAX));
                    if (get_loss() < rnd)
                    {
                        p.second.get_rb()->release();
                        queue.pop_front();
                        return ChannelMsg(0, UUID::nil());
                    }
                }
                ChannelMsg ret(p.second);
                queue.pop_front();
                return ret;
            }
            else
            {
                --p.first;
                return ChannelMsg(0, UUID::nil());
            }
        }
        return ChannelMsg(0, UUID::nil());
    }
    
    void set_ttl(const size_t t) { ttl = t; }
    size_t get_ttl() const { return ttl; }
    void set_latency(const size_t l) 
    { 
        gcomm_assert(l > 0);
        latency = l; 
    }
    size_t get_latency() const { return latency; }
    void set_loss(const double l) { loss = l; }
    double get_loss() const { return loss; }
    size_t get_n_msgs() const
    {
        return queue.size();
    }
private:
    size_t ttl;
    size_t latency;
    double loss;
    deque<pair<size_t, ChannelMsg> > queue;
};

ostream& operator<<(ostream& os, const Channel& ch)
{
    return os;
}

class MatrixElem
{
public:
    MatrixElem(const size_t ii_, const size_t jj_) : ii(ii_), jj(jj_) { }
    size_t get_ii() const { return ii; }
    size_t get_jj() const { return jj; }
    bool operator<(const MatrixElem& cmp) const
    {
        return (ii < cmp.ii || (ii == cmp.ii && jj < cmp.jj));
    }
private:
    size_t ii;
    size_t jj;
};


ostream& operator<<(ostream& os, const MatrixElem& me)
{
    return (os << "(" << me.get_ii() << "," << me.get_jj() << ")");
}

class LinkOp
{
public:
    LinkOp(const size_t idx_, map<MatrixElem, Channel>& prop_) : 
        idx(idx_), prop(prop_) { }
    void operator()(const map<size_t, DummyNode*>::value_type& l) const
    {
        if (l.first != idx)
        {
            gcomm_assert(
                prop.insert(
                    make_pair(MatrixElem(idx, l.first), Channel())).second == true);
            gcomm_assert(
                prop.insert(
                    make_pair(MatrixElem(l.first, idx), Channel())).second == true);
        }
    }
private:
    size_t idx;
    map<MatrixElem, Channel>& prop;
};

class ReadTpOp
{
public:
    ReadTpOp(map<MatrixElem, Channel>& prop_) : prop(prop_) { }

    void operator()(const map<size_t, DummyNode*>::value_type& vt)
    {
        ReadBuf* rb = vt.second->get_tp()->get_out();
        if (rb != 0)
        {
            for (map<MatrixElem, Channel>::iterator i = prop.begin();
                 i != prop.end(); ++i)
            {
                if (i->first.get_ii() == vt.first)
                {
                    i->second.put(rb, vt.second->get_uuid());
                }
            }
            rb->release();
        }
    }
private:
    map<MatrixElem, Channel>& prop;
};


class PropagateOp
{
public:
    PropagateOp(map<size_t, DummyNode*>& tp_) : tp(tp_) { }
    
    void operator()(map<MatrixElem, Channel>::value_type& vt)
    {
        ChannelMsg cmsg(vt.second.get());
        if (cmsg.get_rb() != 0)
        {
            map<size_t, DummyNode*>::iterator i(tp.find(vt.first.get_jj()));
            gcomm_assert(i != tp.end());
            gu_trace(i->second->get_tp()->handle_up(-1, cmsg.get_rb(), 0, 
                                                    ProtoUpMeta(cmsg.get_source())));
            cmsg.get_rb()->release();
        }
    }
    
private:
    map<size_t, DummyNode*>& tp;
};

class ExpireTimersOp
{
public:
    ExpireTimersOp() { }
    void operator()(map<size_t, DummyNode*>::value_type& vt)
    {
        vt.second->expire_timers();
    }
};


class PropagationMatrix
{
public:
    PropagationMatrix() : tp(), prop() { }
    

    
    void insert_tp(const size_t idx, DummyNode* t)
    {
        gcomm_assert(tp.insert(make_pair(idx, t)).second == true);
        for_each(tp.begin(), tp.end(), LinkOp(idx, prop));
    }

    void set_latency(const size_t ii, const size_t jj, const size_t lat)
    {
        map<MatrixElem, Channel>::iterator i = prop.find(MatrixElem(ii, jj));
        gcomm_assert(i != prop.end());
        i->second.set_latency(lat);
    }

    void set_loss(const size_t ii, const size_t jj, const double loss)
    {
        map<MatrixElem, Channel>::iterator i = prop.find(MatrixElem(ii, jj));
        gcomm_assert(i != prop.end());
        i->second.set_loss(loss);
    }

    void propagate_n(size_t n)
    {
        while (n-- > 0)
        {
            for_each(tp.begin(), tp.end(), ReadTpOp(prop));
            for_each(prop.begin(), prop.end(), PropagateOp(tp));
        }
    }

    void propagate_until_empty()
    {
        do
        {
            for_each(prop.begin(), prop.end(), PropagateOp(tp));
            for_each(tp.begin(), tp.end(), ReadTpOp(prop));
        }
        while (count_channel_msgs() > 0);
    }

    void propagate_until_cvi()
    {
        bool all_in = false;
        do
        {
            propagate_until_empty();
            all_in = all_in_cvi();
            if (all_in == false)
            {
                for_each(tp.begin(), tp.end(), ExpireTimersOp());
            }
        }
        while (all_in == false);
        
    }

    friend ostream& operator<<(ostream&, const PropagationMatrix&);

private:

    size_t count_channel_msgs() const
    {
        size_t ret = 0;
        for (map<MatrixElem, Channel>::const_iterator i = prop.begin(); 
             i != prop.end(); ++i)
        {
            ret += i->second.get_n_msgs();
        }
        return ret;
    }

    bool all_in_cvi() const
    {
        for (map<size_t, DummyNode*>::const_iterator i = tp.begin(); 
             i != tp.end(); ++i)
        {
            if (i->second->in_cvi() == false)
            {
                return false;
            }
        }
        return true;
    }

    map<size_t, DummyNode*> tp;
    map<MatrixElem, Channel> prop;
};

ostream& operator<<(ostream& os, const map<MatrixElem, Channel>::value_type& v)
{
    return (os << "(" << v.first.get_ii() << "," << v.first.get_jj() << ")");
}

ostream& operator<<(ostream& os, const PropagationMatrix& prop)
{
    os << "(";
    copy(prop.prop.begin(), prop.prop.end(), 
         ostream_iterator<const map<MatrixElem, Channel>::value_type>(os, ","));
    os << ")";
    return os;
}


void check_traces(const Trace& t1, const Trace& t2)
{
    for (Trace::ViewTraceMap::const_iterator 
             i = t1.get_views().begin(); i != t1.get_views().end();
         ++i)
    {
        Trace::ViewTraceMap::const_iterator i_next(i);
        ++i_next;
        if (i_next != t1.get_views().end())
        {
            const Trace::ViewTraceMap::const_iterator 
                j(t2.get_views().find(Trace::ViewTraceMap::get_key(i)));
            Trace::ViewTraceMap::const_iterator j_next(j);
            ++j_next;
            // Note: Comparision is meaningful if also next view is the 
            // same
            if (j             != t2.get_views().end() && 
                j_next        != t2.get_views().end() &&
                i_next->first == j_next->first          )
            {
                gcomm_assert(*i == *j) << 
                    "traces differ: " << *i << " != " << *j;
            }
        }
    }
}

class CheckTraceOp
{
public:
    CheckTraceOp(const vector<DummyNode*>& nvec_) : nvec(nvec_) { }
    
    void operator()(const DummyNode* n) const
    {
        for (vector<DummyNode*>::const_iterator i = nvec.begin(); 
             i != nvec.end();
             ++i)
        {
            if ((*i)->get_index() != n->get_index())
            {
                gu_trace(check_traces((*i)->get_trace(), n->get_trace()));
            }
        }
    }

private:
    const vector<DummyNode*>& nvec;
};

static void check_trace(const vector<DummyNode*>& nvec)
{
    for_each(nvec.begin(), nvec.end(), CheckTraceOp(nvec));
}



static void join_node(PropagationMatrix* p, 
                      DummyNode* n, bool first = false)
{
    gu_trace(p->insert_tp(n->get_index(), n));
    gu_trace(n->join(first));
}

static void send_n(DummyNode* node, const size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        gu_trace(node->send());
    }
}

START_TEST(test_proto_join_n)
{
    log_info << "START";
    const size_t n_nodes(4);
    EventLoop el;
    PropagationMatrix prop;
    vector<DummyNode*> dn;

    for (size_t i = 1; i <= n_nodes; ++i)
    {
        dn.push_back(new DummyNode(i, &el));
    }
    
    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        gu_trace(prop.propagate_until_empty());
    }
    gu_trace(check_trace(dn));
    for_each(dn.begin(), dn.end(), delete_object());
}
END_TEST

START_TEST(test_proto_join_n_w_user_msg)
{
    log_info << "START";
    const size_t n_nodes(4);
    EventLoop el;
    PropagationMatrix prop;
    vector<DummyNode*> dn;

    for (size_t i = 1; i <= n_nodes; ++i)
    {
        dn.push_back(new DummyNode(i, &el));
    }
    
    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        gu_trace(prop.propagate_until_empty());
        for (size_t j = 0; j < i; ++j)
        {
            gu_trace(send_n(dn[j], 8));
        }
    }
    gu_trace(check_trace(dn));
    for_each(dn.begin(), dn.end(), delete_object());
}
END_TEST


START_TEST(test_proto_join_n_lossy)
{
    log_info << "START";
    const size_t n_nodes(8);
    EventLoop el;
    PropagationMatrix prop;
    vector<DummyNode*> dn;
    
    for (size_t i = 1; i <= n_nodes; ++i)
    {
        dn.push_back(new DummyNode(i, &el));
    }

    gu_log_max_level = GU_LOG_DEBUG;
    for (size_t i = 0; i < n_nodes; ++i)
    {
        for (size_t j = 0; j <= i; ++j)
        {
            dn[j]->set_cvi(ViewId(V_REG, dn[0]->get_uuid(), 
                                  static_cast<uint32_t>(i + 1)));
        }
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        for (size_t j = 1; j < i + 1; ++j)
        {
            prop.set_loss(i + 1, j, 0.9);
            prop.set_loss(j, i + 1, 0.9);

        }
        gu_trace(prop.propagate_until_cvi());
    }
    gu_trace(check_trace(dn));
    for_each(dn.begin(), dn.end(), delete_object());
}
END_TEST


START_TEST(test_proto_join_n_lossy_w_user_msg)
{
    log_info << "START";
    const size_t n_nodes(8);
    EventLoop el;
    PropagationMatrix prop;
    vector<DummyNode*> dn;
    
    for (size_t i = 1; i <= n_nodes; ++i)
    {
        dn.push_back(new DummyNode(i, &el));
    }

    gu_log_max_level = GU_LOG_DEBUG;
    for (size_t i = 0; i < n_nodes; ++i)
    {
        for (size_t j = 0; j <= i; ++j)
        {
            dn[j]->set_cvi(ViewId(V_REG, dn[0]->get_uuid(), 
                                  static_cast<uint32_t>(i + 1)));
        }
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        for (size_t j = 1; j < i + 1; ++j)
        {
            prop.set_loss(i + 1, j, 0.9);
            prop.set_loss(j, i + 1, 0.9);
            
        }
        gu_trace(prop.propagate_until_cvi());
        for (size_t j = 0; j < i; ++j)
        {
            gu_trace(send_n(dn[j], 8));
        }
    }
    gu_trace(check_trace(dn));
    for_each(dn.begin(), dn.end(), delete_object());
}
END_TEST

Suite* evs2_suite()
{
    Suite* s = suite_create("gcomm::evs");
    TCase* tc;
    
    tc = tcase_create("test_seqno");
    tcase_add_test(tc, test_seqno);
    suite_add_tcase(s, tc);
    
    tc = tcase_create("test_range");
    tcase_add_test(tc, test_range);
    suite_add_tcase(s, tc);
    
    tc = tcase_create("test_message");
    tcase_add_test(tc, test_message);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_input_map_insert");
    tcase_add_test(tc, test_input_map_insert);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_input_map_find");
    tcase_add_test(tc, test_input_map_find);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_input_map_safety");
    tcase_add_test(tc, test_input_map_safety);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_input_map_erase");
    tcase_add_test(tc, test_input_map_erase);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_input_map_overwrap");
    tcase_add_test(tc, test_input_map_overwrap);
    tcase_set_timeout(tc, 15);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_input_map_random_insert");
    tcase_add_test(tc, test_input_map_random_insert);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_proto_single_join");
    tcase_add_test(tc, test_proto_single_join);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_proto_double_join");
    tcase_add_test(tc, test_proto_double_join);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_proto_join_n");
    tcase_add_test(tc, test_proto_join_n);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_proto_join_n_w_user_msg");
    tcase_add_test(tc, test_proto_join_n_w_user_msg);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_proto_join_n_lossy");
    tcase_add_test(tc, test_proto_join_n_lossy);
    suite_add_tcase(s, tc);


    tc = tcase_create("test_proto_join_n_lossy_w_user_msg");
    tcase_add_test(tc, test_proto_join_n_lossy_w_user_msg);
    suite_add_tcase(s, tc);
    
    return s;
}
