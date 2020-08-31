/*
 * Copyright (C) 2009-2020 Codership Oy <info@codership.com>
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
#include "check_trace.hpp"

#include "gcomm/conf.hpp"

#include "gu_asio.hpp" // gu::ssl_register_params()

#include <stdexcept>
#include <vector>
#include <set>

#include "check.h"

//
// set GALERA_TEST_DETERMINISTIC env
// variable before running pc test suite.
//
static class deterministic_tests
{
public:
    deterministic_tests()
        : deterministic_tests_()
    {
        if (::getenv("GALERA_TEST_DETERMINISTIC"))
        {
            deterministic_tests_ = true;
        }
        else
        {
            deterministic_tests_ = false;
        }
    }

    bool operator()() const { return deterministic_tests_; }

private:
    bool deterministic_tests_;
} deterministic_tests;

using namespace std;
using namespace std::rel_ops;
using namespace gu::datetime;
using namespace gcomm;
using namespace gcomm::evs;
using gu::DeleteObject;

void init_rand()
{
    unsigned int seed(static_cast<unsigned int>(time(0)));
    log_info << "rand seed " << seed;
    srand(seed);
}

void init_rand(unsigned int seed)
{
    log_info << "rand seed " << seed;
    srand(seed);
}


START_TEST(test_range)
{
    log_info << "START";
    Range r(3, 6);

    check_serialization(r, 2 * sizeof(seqno_t), Range());

}
END_TEST

START_TEST(test_message)
{
    log_info << "START";
    UUID uuid1(0, 0);
    ViewId view_id(V_TRANS, uuid1, 4567);
    seqno_t seq(478), aru_seq(456), seq_range(7);

    UserMessage um(0, uuid1, view_id, seq, aru_seq, seq_range, O_SAFE, 75433, 0xab,
                   Message::F_SOURCE);
    ck_assert(um.serial_size() % 4 == 0);
    check_serialization(um, um.serial_size(), UserMessage());

    AggregateMessage am(0xab, 17457, 0x79);
    check_serialization(am, 4, AggregateMessage());

    DelegateMessage dm(0, uuid1, view_id);
    dm.set_source(uuid1);
    check_serialization(dm, dm.serial_size(), DelegateMessage());

    MessageNodeList node_list;
    node_list.insert(make_pair(uuid1, MessageNode()));
    node_list.insert(make_pair(UUID(2), MessageNode(true, false, 254, true, 1,
                                                    ViewId(V_REG), 5,
                                                    Range(7, 8))));
    JoinMessage jm(0, uuid1, view_id, 8, 5, 27, node_list);
    jm.set_source(uuid1);
    check_serialization(jm, jm.serial_size(), JoinMessage());

    InstallMessage im(0, uuid1, view_id, ViewId(V_REG, view_id.uuid(),
                                                view_id.seq()), 8, 5, 27, node_list);
    im.set_source(uuid1);
    check_serialization(im, im.serial_size(), InstallMessage());

    LeaveMessage lm(0, uuid1, view_id, 45, 88, 3456);
    lm.set_source(uuid1);
    check_serialization(lm, lm.serial_size(), LeaveMessage());


    DelayedListMessage dlm(0, uuid1, view_id, 4576);
    dlm.add(UUID(2), 23);
    dlm.add(UUID(3), 45);
    dlm.add(UUID(5), 255);
    check_serialization(dlm, dlm.serial_size(), DelayedListMessage());
}
END_TEST

START_TEST(test_input_map_insert)
{
    log_info << "START";
    UUID uuid1(1), uuid2(2);
    InputMap im;
    ViewId view(V_REG, uuid1, 0);

    try
    {
        im.insert(0, UserMessage(0, uuid1, view, 0));
        ck_abort_msg("Exception not thrown, input map has not been "
                     "reset/initialized yet");
    }
    catch (...)
    {  }

    im.reset(1);

    im.insert(0, UserMessage(0, uuid1, view, 0));


    im.clear();
    im.reset(2);

    for (seqno_t s = 0; s < 10; ++s)
    {
        im.insert(0, UserMessage(0, uuid1, view, s));
        im.insert(1, UserMessage(0, uuid2, view, s));
    }

    for (seqno_t s = 0; s < 10; ++s)
    {
        InputMap::iterator i = im.find(0, s);
        ck_assert(i != im.end());
        ck_assert(InputMapMsgIndex::value(i).msg().source() == uuid1);
        ck_assert(InputMapMsgIndex::value(i).msg().seq() == s);

        i = im.find(1, s);
        ck_assert(i != im.end());
        ck_assert(InputMapMsgIndex::value(i).msg().source() == uuid2);
        ck_assert(InputMapMsgIndex::value(i).msg().seq() == s);
    }

}
END_TEST

START_TEST(test_input_map_find)
{
    log_info << "START";
    InputMap im;
    UUID uuid1(1);
    ViewId view(V_REG, uuid1, 0);

    im.reset(1);

    im.insert(0, UserMessage(0, uuid1, view, 0));

    ck_assert(im.find(0, 0) != im.end());


    im.insert(0, UserMessage(0, uuid1, view, 2));
    im.insert(0, UserMessage(0, uuid1, view, 4));
    im.insert(0, UserMessage(0, uuid1, view, 7));

    ck_assert(im.find(0, 2) != im.end());
    ck_assert(im.find(0, 4) != im.end());
    ck_assert(im.find(0, 7) != im.end());

    ck_assert(im.find(0, 3) == im.end());
    ck_assert(im.find(0, 5) == im.end());
    ck_assert(im.find(0, 6) == im.end());
    ck_assert(im.find(0, 8) == im.end());
}
END_TEST

START_TEST(test_input_map_safety)
{
    log_info << "START";
    InputMap im;
    UUID uuid1(1);
    size_t index1(0);
    ViewId view(V_REG, uuid1, 0);

    im.reset(1);

    im.insert(index1, UserMessage(0, uuid1, view, 0));
    ck_assert(im.aru_seq() == 0);
    im.insert(index1, UserMessage(0, uuid1, view, 1));
    ck_assert(im.aru_seq() == 1);
    im.insert(index1, UserMessage(0, uuid1, view, 2));
    ck_assert(im.aru_seq() == 2);
    im.insert(index1, UserMessage(0, uuid1, view, 3));
    ck_assert(im.aru_seq() == 3);
    im.insert(index1, UserMessage(0, uuid1, view, 5));
    ck_assert(im.aru_seq() == 3);

    im.insert(index1, UserMessage(0, uuid1, view, 4));
    ck_assert(im.aru_seq() == 5);

    InputMap::iterator i = im.find(index1, 0);
    ck_assert(im.is_fifo(i) == true);
    ck_assert(im.is_agreed(i) == true);
    ck_assert(im.is_safe(i) == false);
    im.set_safe_seq(index1, 0);
    ck_assert(im.is_safe(i) == true);

    im.set_safe_seq(index1, 5);
    i = im.find(index1, 5);
    ck_assert(im.is_safe(i) == true);

    im.insert(index1, UserMessage(0, uuid1, view, 7));
    im.set_safe_seq(index1, im.aru_seq());
    i = im.find(index1, 7);
    ck_assert(im.is_safe(i) == false);

}
END_TEST

START_TEST(test_input_map_erase)
{
    log_info << "START";
    InputMap im;
    size_t index1(0);
    UUID uuid1(1);
    ViewId view(V_REG, uuid1, 1);

    im.reset(1);

    for (seqno_t s = 0; s < 10; ++s)
    {
        im.insert(index1, UserMessage(0, uuid1, view, s));
    }

    for (seqno_t s = 0; s < 10; ++s)
    {
        InputMap::iterator i = im.find(index1, s);
        ck_assert(i != im.end());
        im.erase(i);
        i = im.find(index1, s);
        ck_assert(i == im.end());
        (void)im.recover(index1, s);
    }
    im.set_safe_seq(index1, 9);
    try
    {
        im.recover(index1, 9);
        ck_abort_msg("Exception not thrown, "
                     "setting safe seq should purge index");
    }
    catch (...) { }
}
END_TEST

START_TEST(test_input_map_overwrap)
{
    log_info << "START";
    InputMap im;
    const size_t n_nodes(5);
    ViewId view(V_REG, UUID(1), 1);
    vector<UUID> uuids;
    for (size_t n = 0; n < n_nodes; ++n)
    {
        uuids.push_back(UUID(static_cast<int32_t>(n + 1)));
    }

    im.reset(n_nodes);


    Date start(Date::monotonic());
    size_t cnt(0);
    seqno_t last_safe(-1);
    for (seqno_t seq = 0; seq < 100000; ++seq)
    {
        for (size_t i = 0; i < n_nodes; ++i)
        {
            UserMessage um(0, uuids[i], view, seq);
            (void)im.insert(i, um);
            if ((seq + 5) % 10 == 0)
            {
                last_safe = um.seq() - 3;
                im.set_safe_seq(i, last_safe);
                for (InputMap::iterator ii = im.begin();
                     ii != im.end() && im.is_safe(ii) == true;
                     ii = im.begin())
                {
                    im.erase(ii);
                }
            }
            cnt++;
        }
        gcomm_assert(im.aru_seq() == seq);
        gcomm_assert(im.safe_seq() == last_safe);
    }
    Date stop(Date::monotonic());

    double div(double(stop.get_utc() - start.get_utc())/gu::datetime::Sec);
    log_info << "input map msg rate " << double(cnt)/div;
}
END_TEST


class InputMapInserter
{
public:
    InputMapInserter(InputMap& im) : im_(im) { }

    void operator()(const pair<size_t, UserMessage>& p) const
    {
        im_.insert(p.first, p.second);
    }
private:
    InputMap& im_;
};

START_TEST(test_input_map_random_insert)
{
    log_info << "START";
    init_rand();
    seqno_t n_seqnos(1024);
    size_t n_uuids(4);
    vector<UUID> uuids(n_uuids);
    vector<pair<size_t, UserMessage> > msgs(static_cast<size_t>(n_uuids*n_seqnos));
    ViewId view_id(V_REG, UUID(1), 1);
    InputMap im;

    for (size_t i = 0; i < n_uuids; ++i)
    {
        uuids[i] = (static_cast<int32_t>(i + 1));
    }

    im.reset(n_uuids);

    for (seqno_t j = 0; j < n_seqnos; ++j)
    {
        for (size_t i = 0; i < n_uuids; ++i)
        {
            msgs[static_cast<size_t>(j*n_uuids) + i] =
                make_pair(i, UserMessage(0, uuids[i], view_id, j));
        }
    }

    vector<pair<size_t, UserMessage> > random_msgs(msgs);
    random_shuffle(random_msgs.begin(), random_msgs.end());
    for_each(random_msgs.begin(), random_msgs.end(), InputMapInserter(im));

    size_t n = 0;
    for (InputMap::iterator i = im.begin(); i != im.end(); ++i)
    {
        const InputMapMsg& msg(InputMapMsgIndex::value(i));
        ck_assert(msg.msg() == msgs[n].second);
        ck_assert(im.is_safe(i) == false);
        ++n;
    }

    ck_assert(im.aru_seq() == n_seqnos - 1);
    ck_assert(im.safe_seq() == -1);

    for (size_t i = 0; i < n_uuids; ++i)
    {
        ck_assert(im.range(i) ==
                    Range(n_seqnos,
                          n_seqnos - 1));

        im.set_safe_seq(i, n_seqnos - 1);
    }
    ck_assert(im.safe_seq() == n_seqnos - 1);

}
END_TEST

START_TEST(test_input_map_gap_range_list)
{
    gcomm::evs::InputMap im;
    im.reset(1);
    gcomm::UUID uuid(1);
    gcomm::ViewId view_id(gcomm::V_REG, uuid, 1);
    im.insert(0, gcomm::evs::UserMessage(0, uuid, view_id, 0, 0));
    im.insert(0, gcomm::evs::UserMessage(0, uuid, view_id, 2, 0));

    std::vector<gcomm::evs::Range> gap_range(
        im.gap_range_list(0, gcomm::evs::Range(0, 2)));
    ck_assert(gap_range.size() == 1);
    ck_assert(gap_range.begin()->lu() == 1);
    ck_assert(gap_range.begin()->hs() == 1);

    im.insert(0, gcomm::evs::UserMessage(0, uuid, view_id, 4, 0));
    gap_range = im.gap_range_list(0, gcomm::evs::Range(0, 4));
    ck_assert(gap_range.size() == 2);
    ck_assert(gap_range.begin()->lu() == 1);
    ck_assert(gap_range.begin()->hs() == 1);
    ck_assert(gap_range.rbegin()->lu() == 3);
    ck_assert(gap_range.rbegin()->hs() == 3);

    // Although there are two messages missing, limiting the range to 0,2
    // should return only the first one.
    gap_range = im.gap_range_list(0, gcomm::evs::Range(0, 2));
    ck_assert(gap_range.size() == 1);
    ck_assert(gap_range.begin()->lu() == 1);
    ck_assert(gap_range.begin()->hs() == 1);

    im.insert(0, gcomm::evs::UserMessage(0, uuid, view_id, 8, 0));
    gap_range = im.gap_range_list(0, gcomm::evs::Range(0, 8));
    ck_assert(gap_range.size() == 3);
    ck_assert(gap_range.begin()->lu() == 1);
    ck_assert(gap_range.begin()->hs() == 1);
    ck_assert(gap_range.rbegin()->lu() == 5);
    ck_assert(gap_range.rbegin()->hs() == 7);

    im.insert(0, gcomm::evs::UserMessage(0, uuid, view_id, 3, 0));
    gap_range = im.gap_range_list(0, gcomm::evs::Range(0, 8));
    ck_assert(gap_range.size() == 2);
    ck_assert(gap_range.begin()->lu() == 1);
    ck_assert(gap_range.begin()->hs() == 1);
    ck_assert(gap_range.rbegin()->lu() == 5);
    ck_assert(gap_range.rbegin()->hs() == 7);

    im.insert(0, gcomm::evs::UserMessage(0, uuid, view_id, 1, 0));
    im.insert(0, gcomm::evs::UserMessage(0, uuid, view_id, 5, 0));
    im.insert(0, gcomm::evs::UserMessage(0, uuid, view_id, 6, 0));
    im.insert(0, gcomm::evs::UserMessage(0, uuid, view_id, 7, 0));
    gap_range = im.gap_range_list(0, gcomm::evs::Range(0, 8));
    ck_assert(gap_range.empty());
}
END_TEST

static Datagram* get_msg(DummyTransport* tp, Message* msg, bool release = true)
{
    Datagram* rb = tp->out();
    if (rb != 0)
    {
        gu_trace(Proto::unserialize_message(tp->uuid(), *rb, msg));
        if (release == true)
        {
            delete rb;
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

    Datagram* rb = get_msg(t, &jm);
    ck_assert(rb != 0);
    ck_assert(jm.type() == Message::EVS_T_JOIN);

    // Install message is emitted at the end of JOIN handling
    // 'cause this is the only instance and is always consistent
    // with itself
    rb = get_msg(t, &im);
    ck_assert(rb != 0);
    ck_assert(im.type() == Message::EVS_T_INSTALL);

    // Handling INSTALL message emits three gap messages,
    // one for receiving install message (commit gap), one for
    // shift to install and one for shift to operational
    rb = get_msg(t, &gm);
    ck_assert(rb != 0);
    ck_assert(gm.type() == Message::EVS_T_GAP);
    ck_assert((gm.flags() & Message::F_COMMIT) != 0);

    rb = get_msg(t, &gm);
    ck_assert(rb != 0);
    ck_assert(gm.type() == Message::EVS_T_GAP);
    ck_assert((gm.flags() & Message::F_COMMIT) == 0);

    rb = get_msg(t, &gm);
    ck_assert(rb != 0);
    ck_assert(gm.type() == Message::EVS_T_GAP);
    ck_assert((gm.flags() & Message::F_COMMIT) == 0);

    // State must have evolved JOIN -> S_GATHER -> S_INSTALL -> S_OPERATIONAL
    ck_assert(p->state() == Proto::S_OPERATIONAL);

    // Handle join message again, must stay in S_OPERATIONAL, must not
    // emit anything
    p->handle_msg(jm);
    rb = get_msg(t, &gm);
    ck_assert(rb == 0);
    ck_assert(p->state() == Proto::S_OPERATIONAL);

}

class DummyUser : public Toplay
{
public:
    DummyUser(gu::Config& conf) : Toplay(conf) { }
    void handle_up(const void*, const Datagram&, const ProtoUpMeta&) { }
private:
};


START_TEST(test_proto_single_join)
{
    log_info << "START";
    gu::Config conf;
    mark_point();
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    UUID uuid(1);
    DummyTransport t(uuid);
    mark_point();
    DummyUser u(conf);
    mark_point();
    Proto p(conf, uuid, 0);
    mark_point();
    gcomm::connect(&t, &p);
    gcomm::connect(&p, &u);
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

    Datagram* rb;

    // Initial states check
    p2->shift_to(Proto::S_JOINING);
    ck_assert(p1->state() == Proto::S_OPERATIONAL);
    ck_assert(p2->state() == Proto::S_JOINING);

    // Send join message, don't self handle immediately
    // Expected output: one join message
    p2->send_join(false);
    ck_assert(p2->state() == Proto::S_JOINING);
    rb = get_msg(t2, &jm);
    ck_assert(rb != 0);
    ck_assert(jm.type() == Message::EVS_T_JOIN);
    rb = get_msg(t2, &msg);
    ck_assert(rb == 0);

    // Handle node 2's join on node 1
    // Expected output: shift to S_GATHER and one join message
    p1->handle_msg(jm);
    ck_assert(p1->state() == Proto::S_GATHER);
    rb = get_msg(t1, &jm);
    ck_assert(rb != 0);
    ck_assert(jm.type() == Message::EVS_T_JOIN);
    rb = get_msg(t1, &msg);
    ck_assert(rb == 0);

    // Handle node 1's join on node 2
    // Expected output: shift to S_GATHER and one join message
    p2->handle_msg(jm);
    ck_assert(p2->state() == Proto::S_GATHER);
    rb = get_msg(t2, &jm);
    ck_assert(rb != 0);
    ck_assert(jm.type() == Message::EVS_T_JOIN);
    rb = get_msg(t2, &msg);
    ck_assert(rb == 0);

    // Handle node 2's join on node 1
    // Expected output: Install and commit gap messages, state stays in S_GATHER
    p1->handle_msg(jm);
    ck_assert(p1->state() == Proto::S_GATHER);
    rb = get_msg(t1, &im);
    ck_assert(rb != 0);
    ck_assert(im.type() == Message::EVS_T_INSTALL);
    rb = get_msg(t1, &gm);
    ck_assert(rb != 0);
    ck_assert(gm.type() == Message::EVS_T_GAP);
    ck_assert((gm.flags() & Message::F_COMMIT) != 0);
    rb = get_msg(t1, &msg);
    ck_assert(rb == 0);

    // Handle install message on node 2
    // Expected output: commit gap message and state stays in S_RECOVERY
    p2->handle_msg(im);
    ck_assert(p2->state() == Proto::S_GATHER);
    rb = get_msg(t2, &gm2);
    ck_assert(rb != 0);
    ck_assert(gm2.type() == Message::EVS_T_GAP);
    ck_assert((gm2.flags() & Message::F_COMMIT) != 0);
    rb = get_msg(t2, &msg);
    ck_assert(rb == 0);

    // Handle gap messages
    // Expected output: Both nodes shift to S_INSTALL,
    // both send gap messages
    p1->handle_msg(gm2);
    ck_assert(p1->state() == Proto::S_INSTALL);
    Message gm12;
    rb = get_msg(t1, &gm12);
    ck_assert(rb != 0);
    ck_assert(gm12.type() == Message::EVS_T_GAP);
    ck_assert((gm12.flags() & Message::F_COMMIT) == 0);
    rb = get_msg(t1, &msg);
    ck_assert(rb == 0);

    p2->handle_msg(gm);
    ck_assert(p2->state() == Proto::S_INSTALL);
    Message gm22;
    rb = get_msg(t2, &gm22);
    ck_assert(rb != 0);
    ck_assert(gm22.type() == Message::EVS_T_GAP);
    ck_assert((gm22.flags() & Message::F_COMMIT) == 0);
    rb = get_msg(t2, &msg);
    ck_assert(rb == 0);

    // Handle final gap messages, expected output shift to operational
    // and gap message

    p1->handle_msg(gm22);
    ck_assert(p1->state() == Proto::S_OPERATIONAL);
    rb = get_msg(t1, &msg);
    ck_assert(rb != 0);
    ck_assert(msg.type() == Message::EVS_T_GAP);
    ck_assert((msg.flags() & Message::F_COMMIT) == 0);
    rb = get_msg(t1, &msg);
    ck_assert(rb == 0);

    p2->handle_msg(gm12);
    ck_assert(p2->state() == Proto::S_OPERATIONAL);
    rb = get_msg(t2, &msg);
    ck_assert(rb != 0);
    ck_assert(msg.type() == Message::EVS_T_GAP);
    ck_assert((msg.flags() & Message::F_COMMIT) == 0);
    rb = get_msg(t2, &msg);
    ck_assert(rb == 0);

}


START_TEST(test_proto_double_join)
{
    log_info << "START";
    gu::Config conf;
    mark_point();
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    UUID uuid1(1), uuid2(2);
    DummyTransport t1(uuid1), t2(uuid2);
    mark_point();
    DummyUser u1(conf), u2(conf);
    mark_point();
    Proto p1(conf, uuid1, 0), p2(conf, uuid2, 0);

    gcomm::connect(&t1, &p1);
    gcomm::connect(&p1, &u1);

    gcomm::connect(&t2, &p2);
    gcomm::connect(&p2, &u2);

    single_join(&t1, &p1);
    double_join(&t1, &p1, &t2, &p2);

}
END_TEST

static gu::Config gu_conf;

static DummyNode* create_dummy_node(size_t idx,
                                    int version,
                                    const string& suspect_timeout = "PT1H",
                                    const string& inactive_timeout = "PT1H",
                                    const string& retrans_period = "PT10M")
{
    // reset conf to avoid stale config in case of nofork
    gu_conf = gu::Config();
    gu::ssl_register_params(gu_conf);
    gcomm::Conf::register_params(gu_conf);
    string conf = "evs://?" + Conf::EvsViewForgetTimeout + "=PT1H&"
        + Conf::EvsInactiveCheckPeriod + "=" + to_string(Period(suspect_timeout)/3) + "&"
        + Conf::EvsSuspectTimeout + "=" + suspect_timeout + "&"
        + Conf::EvsInactiveTimeout + "=" + inactive_timeout + "&"

        + Conf::EvsKeepalivePeriod + "=" + retrans_period + "&"
        + Conf::EvsJoinRetransPeriod + "=" + retrans_period + "&"
        + Conf::EvsInfoLogMask + "=0x7" + "&"
        + Conf::EvsDebugLogMask + "=0xfff" + "&"
        + Conf::EvsVersion + "=" + gu::to_string<int>(version);
    if (::getenv("EVS_DEBUG_MASK") != 0)
    {
        conf += "&" + Conf::EvsDebugLogMask + "="
            + ::getenv("EVS_DEBUG_MASK");
    }
    list<Protolay*> protos;
    UUID uuid(static_cast<int32_t>(idx));
    protos.push_back(new DummyTransport(uuid, false));
    protos.push_back(new Proto(gu_conf, uuid, 0, conf));
    return new DummyNode(gu_conf, idx, protos);
}

namespace
{
    gcomm::evs::Proto* evs_from_dummy(DummyNode* dn)
    {
        return static_cast<Proto*>(dn->protos().back());
    }

    DummyTransport* transport_from_dummy(DummyNode* dn)
    {
        return static_cast<DummyTransport*>(dn->protos().front());
    }
}


static void join_node(PropagationMatrix* p,
                      DummyNode* n, bool first = false)
{
    gu_trace(p->insert_tp(n));
    gu_trace(n->connect(first));
}


static void send_n(DummyNode* node, const size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        gu_trace(node->send());
    }
}

static void set_cvi(vector<DummyNode*>& nvec, size_t i_begin, size_t i_end,
                    size_t seq)
{
    for (size_t i = i_begin; i <= i_end; ++i)
    {
        nvec[i]->set_cvi(ViewId(V_REG, nvec[i_begin]->uuid(),
                                static_cast<uint32_t>(seq)));
    }
}

template <class C>
class ViewSeq
{
public:
    ViewSeq() { }
    bool operator()(const C& a, const C& b) const
    {
        return (a->trace().current_view_trace().view().id().seq() < b->trace().current_view_trace().view().id().seq());
    }
};

static uint32_t get_max_view_seq(const std::vector<DummyNode*>& dnv,
                                 size_t i, size_t j)
{
    if (i == dnv.size()) return static_cast<uint32_t>(-1);
    return (*std::max_element(dnv.begin() + i,
                              dnv.begin() + j,
                              ViewSeq<const DummyNode*>()))->trace().current_view_trace().view().id().seq();
}



START_TEST(test_proto_join_n)
{
    log_info << "START (join_n)";
    init_rand();

    const size_t n_nodes(4);
    PropagationMatrix prop;
    vector<DummyNode*> dn;

    for (size_t i = 1; i <= n_nodes; ++i)
    {
        gu_trace(dn.push_back(create_dummy_node(i, 0)));
    }

    uint32_t max_view_seq(0);
    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        set_cvi(dn, 0, i, max_view_seq + 1);
        gu_trace(prop.propagate_until_cvi(false));
        max_view_seq = get_max_view_seq(dn, 0, i);
    }
    gu_trace(check_trace(dn));
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST


START_TEST(test_proto_join_n_w_user_msg)
{
    gu_conf_self_tstamp_on();
    log_info << "START (join_n_w_user_msg)";
    init_rand();

    const size_t n_nodes(4);
    PropagationMatrix prop;
    vector<DummyNode*> dn;
    // @todo This test should terminate without these timeouts
    const string suspect_timeout("PT1H");
    const string inactive_timeout("PT1H");
    const string retrans_period("PT0.1S");

    for (size_t i = 1; i <= n_nodes; ++i)
    {
        gu_trace(dn.push_back(
                     create_dummy_node(i, 0, suspect_timeout,
                                       inactive_timeout, retrans_period)));
    }

    uint32_t max_view_seq(0);
    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        set_cvi(dn, 0, i, max_view_seq + 1);
        gu_trace(prop.propagate_until_cvi(true));
        for (size_t j = 0; j <= i; ++j)
        {
            gu_trace(send_n(dn[j], 5 + ::rand() % 4));
        }
        gu_trace(prop.propagate_until_empty());
        for (size_t j = 0; j <= i; ++j)
        {
            gu_trace(send_n(dn[j], 5 + ::rand() % 4));
        }
        max_view_seq = get_max_view_seq(dn, 0, i);
    }

    gu_trace(check_trace(dn));
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST


START_TEST(test_proto_join_n_lossy)
{
    gu_conf_self_tstamp_on();
    log_info << "START (join_n_lossy)";
    init_rand();

    const size_t n_nodes(4);
    PropagationMatrix prop;
    vector<DummyNode*> dn;
    const string suspect_timeout("PT1H");
    const string inactive_timeout("PT1H");
    const string retrans_period("PT0.1S");


    for (size_t i = 1; i <= n_nodes; ++i)
    {
        gu_trace(dn.push_back(
                     create_dummy_node(i, 0, suspect_timeout,
                                       inactive_timeout, retrans_period)));
    }

    uint32_t max_view_seq(0);
    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        set_cvi(dn, 0, i, max_view_seq + 1);
        for (size_t j = 1; j < i + 1; ++j)
        {
            prop.set_loss(i + 1, j, 0.9);
            prop.set_loss(j, i + 1, 0.9);
        }
        gu_trace(prop.propagate_until_cvi(true));
        max_view_seq = get_max_view_seq(dn, 0, i);
    }
    gu_trace(check_trace(dn));
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST


START_TEST(test_proto_join_n_lossy_w_user_msg)
{
    gu_conf_self_tstamp_on();
    log_info << "START (join_n_lossy_w_user_msg)";
    init_rand();

    const size_t n_nodes(4);
    PropagationMatrix prop;
    vector<DummyNode*> dn;
    const string suspect_timeout("PT1H");
    const string inactive_timeout("PT1H");
    const string retrans_period("PT0.1S");

    for (size_t i = 1; i <= n_nodes; ++i)
    {
        gu_trace(dn.push_back(
                     create_dummy_node(i, 0, suspect_timeout,
                                       inactive_timeout, retrans_period)));
    }

    uint32_t max_view_seq(0);
    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        set_cvi(dn, 0, i, max_view_seq + 1);
        for (size_t j = 1; j < i + 1; ++j)
        {
            prop.set_loss(i + 1, j, 0.9);
            prop.set_loss(j, i + 1, 0.9);

        }
        gu_trace(prop.propagate_until_cvi(true));
        for (size_t j = 0; j < i; ++j)
        {
            gu_trace(send_n(dn[j], 5 + ::rand() % 4));
        }
        max_view_seq = get_max_view_seq(dn, 0, i);
    }
    gu_trace(check_trace(dn));
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST

START_TEST(test_proto_leave_n)
{
    gu_conf_self_tstamp_on();
    log_info << "START (leave_n)";
    init_rand();

    const size_t n_nodes(4);
    PropagationMatrix prop;
    vector<DummyNode*> dn;

    for (size_t i = 1; i <= n_nodes; ++i)
    {
        gu_trace(dn.push_back(create_dummy_node(i, 0)));
    }

    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        set_cvi(dn, 0, i, i + 1);
        gu_trace(prop.propagate_until_cvi(true));
    }

    uint32_t max_view_seq(get_max_view_seq(dn, 0, n_nodes));

    for (size_t i = 0; i < n_nodes; ++i)
    {
        dn[i]->close();
        dn[i]->set_cvi(V_REG);
        set_cvi(dn, i + 1, n_nodes - 1, max_view_seq + 1);
        gu_trace(prop.propagate_until_cvi(true));
        max_view_seq = get_max_view_seq(dn, i + 1, n_nodes);
    }

    gu_trace(check_trace(dn));
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST

START_TEST(test_proto_leave_n_w_user_msg)
{
    gu_conf_self_tstamp_on();
    log_info << "START (leave_n_w_user_msg)";
    init_rand();

    const size_t n_nodes(4);
    PropagationMatrix prop;
    vector<DummyNode*> dn;
    const string suspect_timeout("PT1H");
    const string inactive_timeout("PT1H");
    const string retrans_period("PT0.1S");

    for (size_t i = 1; i <= n_nodes; ++i)
    {
        gu_trace(dn.push_back(
                     create_dummy_node(i, 0, suspect_timeout,
                                       inactive_timeout, retrans_period)));
    }

    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        set_cvi(dn, 0, i, i + 1);
        gu_trace(prop.propagate_until_cvi(false));
    }

    uint32_t max_view_seq(get_max_view_seq(dn, 0, n_nodes));

    for (size_t i = 0; i < n_nodes; ++i)
    {
        for (size_t j = i; j < n_nodes; ++j)
        {
            gu_trace(send_n(dn[j], 5 + ::rand() % 4));
        }
        dn[i]->close();
        dn[i]->set_cvi(V_REG);
        set_cvi(dn, i + 1, n_nodes - 1, max_view_seq + 1);
        gu_trace(prop.propagate_until_cvi(true));
        max_view_seq = get_max_view_seq(dn, i + 1, n_nodes);
    }

    gu_trace(check_trace(dn));
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST


START_TEST(test_proto_leave_n_lossy)
{
    if (deterministic_tests()) return;

    gu_conf_self_tstamp_on();
    log_info << "START (leave_n_lossy)";
    init_rand();
    const size_t n_nodes(4);
    PropagationMatrix prop;
    vector<DummyNode*> dn;
    const string suspect_timeout("PT15S");
    const string inactive_timeout("PT30S");
    const string retrans_period("PT1S");

    for (size_t i = 1; i <= n_nodes; ++i)
    {
        gu_trace(dn.push_back(
                     create_dummy_node(i, 0, suspect_timeout,
                                       inactive_timeout, retrans_period)));
    }

    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        set_cvi(dn, 0, i, i + 1);
        gu_trace(prop.propagate_until_cvi(false));
    }

    uint32_t max_view_seq(get_max_view_seq(dn, 0, n_nodes));

    for (size_t i = 0; i < n_nodes; ++i)
    {
        for (size_t j = 1; j < i + 1; ++j)
        {
            prop.set_loss(i + 1, j, 0.9);
            prop.set_loss(j, i + 1, 0.9);
        }
    }

    for (size_t i = 0; i < n_nodes; ++i)
    {
        dn[i]->set_cvi(V_REG);
        set_cvi(dn, i + 1, n_nodes - 1, max_view_seq + 1);
        dn[i]->close();
        gu_trace(prop.propagate_until_cvi(true));
        max_view_seq = get_max_view_seq(dn, i + 1, n_nodes);
    }

    gu_trace(check_trace(dn));
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST



START_TEST(test_proto_leave_n_lossy_w_user_msg)
{
    if (deterministic_tests()) return;

    gu_conf_self_tstamp_on();
    log_info << "START (leave_n_lossy_w_user_msg)";
    init_rand();

    const size_t n_nodes(4);
    PropagationMatrix prop;
    vector<DummyNode*> dn;

    const string suspect_timeout("PT15S");
    const string inactive_timeout("PT30S");
    const string retrans_period("PT1S");

    for (size_t i = 1; i <= n_nodes; ++i)
    {
        gu_trace(dn.push_back(
                     create_dummy_node(i, 0, suspect_timeout,
                                       inactive_timeout, retrans_period)));
    }

    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        set_cvi(dn, 0, i, i + 1);
        gu_trace(prop.propagate_until_cvi(false));
    }


    for (size_t i = 0; i < n_nodes; ++i)
    {
        for (size_t j = 1; j < i + 1; ++j)
        {
            prop.set_loss(i + 1, j, 0.9);
            prop.set_loss(j, i + 1, 0.9);
        }
    }

    uint32_t max_view_seq(get_max_view_seq(dn, 0, n_nodes));

    for (size_t i = 0; i < n_nodes; ++i)
    {
        for (size_t j = i; j < n_nodes; ++j)
        {
            gu_trace(send_n(dn[j], 5 + ::rand() % 4));
        }
        dn[i]->set_cvi(V_REG);
        set_cvi(dn, i + 1, n_nodes - 1, max_view_seq + 1);
        dn[i]->close();
        gu_trace(prop.propagate_until_cvi(true));
        max_view_seq = get_max_view_seq(dn, i + 1, n_nodes);
    }

    gu_trace(check_trace(dn));
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST


// Generic test code for split/merge cases
static void test_proto_split_merge_gen(const size_t n_nodes,
                                       const bool send_msgs,
                                       const double loss)
{
    PropagationMatrix prop;
    vector<DummyNode*> dn;
    const string suspect_timeout("PT15S");
    const string inactive_timeout("PT30S");
    const string retrans_period("PT1S");

    for (size_t i = 1; i <= n_nodes; ++i)
    {
        gu_trace(dn.push_back(
                     create_dummy_node(i, 0, suspect_timeout,
                                       inactive_timeout, retrans_period)));
    }

    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        set_cvi(dn, 0, i, i + 1);
        gu_trace(prop.propagate_until_cvi(false));
    }

    for (size_t i = 0; i < n_nodes; ++i)
    {
        for (size_t j = 1; j < i + 1; ++j)
        {
            prop.set_loss(i + 1, j, loss);
            prop.set_loss(j, i + 1, loss);
        }
    }

    vector<int32_t> split;
    for (size_t i = 0; i < n_nodes; ++i)
    {
        split.push_back(static_cast<int32_t>(i + 1));
    }

    uint32_t max_view_seq(get_max_view_seq(dn, 0, n_nodes));

    for (size_t i = 1; i < n_nodes; ++i)
    {
        if (send_msgs == true)
        {
            for (size_t k = 0; k < 5; ++k)
            {
                for (size_t j = 0; j < n_nodes; ++j)
                {
                    gu_trace(send_n(dn[j], 1 + j));
                }
                gu_trace(prop.propagate_n(7));
            }
        }

        log_info << "split " << i;
        for (size_t j = 0; j < i; ++j)
        {
            for (size_t k = i; k < n_nodes; ++k)
            {
                gu_trace(prop.set_loss(split[j], split[k], 0.));
                gu_trace(prop.set_loss(split[k], split[j], 0.));
            }
        }

        set_cvi(dn, 0, i - 1, max_view_seq + 1);
        set_cvi(dn, i, n_nodes - 1, max_view_seq + 1);

        if (send_msgs == true)
        {
            for (size_t j = 0; j < n_nodes; ++j)
            {
                gu_trace(send_n(dn[j], 5 + rand() % 4));
            }
        }

        gu_trace(prop.propagate_until_cvi(true));
        max_view_seq = get_max_view_seq(dn, 0, n_nodes);
        log_info << "merge " << i;
        for (size_t j = 0; j < i; ++j)
        {
            for (size_t k = i; k < n_nodes; ++k)
            {
                gu_trace(prop.set_loss(split[j], split[k], loss));
                gu_trace(prop.set_loss(split[k], split[j], loss));
            }
        }

        set_cvi(dn, 0, n_nodes - 1, max_view_seq + 1);

        if (send_msgs == true)
        {
            for (size_t j = 0; j < n_nodes; ++j)
            {
                gu_trace(send_n(dn[j], 5 + rand() % 4));
            }
        }
        gu_trace(prop.propagate_until_cvi(true));
        max_view_seq = get_max_view_seq(dn, 0, n_nodes);
    }

    gu_trace(prop.propagate_until_empty());

    gu_trace(check_trace(dn));
    for_each(dn.begin(), dn.end(), DeleteObject());
}



START_TEST(test_proto_split_merge)
{
    gu_conf_self_tstamp_on();
    log_info << "START (split_merge)";
    init_rand();

    test_proto_split_merge_gen(4, false, 1.);
}
END_TEST


START_TEST(test_proto_split_merge_lossy)
{
    if (deterministic_tests()) return;

    gu_conf_self_tstamp_on();
    log_info << "START (split_merge_lossy)";
    init_rand();

    test_proto_split_merge_gen(4, false, .9);
}
END_TEST



START_TEST(test_proto_split_merge_w_user_msg)
{
    gu_conf_self_tstamp_on();
    log_info << "START (split_merge_w_user_msg)";
    init_rand();

    test_proto_split_merge_gen(4, true, 1.);

}
END_TEST


START_TEST(test_proto_split_merge_lossy_w_user_msg)
{
    if (deterministic_tests()) return;

    gu_conf_self_tstamp_on();
    log_info << "START (split_merge_lossy_w_user_msg)";
    init_rand();

    test_proto_split_merge_gen(4, true, .9);
}
END_TEST

START_TEST(test_proto_stop_cont)
{
    log_info << "START";
    init_rand();

    const size_t n_nodes(4);
    PropagationMatrix prop;
    vector<DummyNode*> dn;
    const string suspect_timeout("PT0.31S");
    const string inactive_timeout("PT0.31S");
    const string retrans_period("PT0.1S");

    for (size_t i = 1; i <= n_nodes; ++i)
    {
        gu_trace(dn.push_back(
                     create_dummy_node(i, 0, suspect_timeout,
                                       inactive_timeout, retrans_period)));
    }

    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        set_cvi(dn, 0, i, i + 1);
        gu_trace(prop.propagate_until_cvi(false));
    }
    uint32_t view_seq = n_nodes + 1;

    for (size_t i = 0; i < n_nodes; ++i)
    {
        for (size_t j = 0; j < n_nodes; ++j)
        {
            if (j != i)
            {
                dn[j]->close(dn[i]->uuid());
            }
        }
        set_cvi(dn, 0, n_nodes - 1, view_seq + 1);
        gu_trace(prop.propagate_until_cvi(true));
        view_seq += 2;

    }
    gu_trace(check_trace(dn));
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST


START_TEST(test_proto_arbitrate)
{
    log_info << "START";
    const size_t n_nodes(3);
    PropagationMatrix prop;
    vector<DummyNode*> dn;
    const string suspect_timeout("PT0.5S");
    const string inactive_timeout("PT0.5S");
    const string retrans_period("PT0.1S");

    for (size_t i = 1; i <= n_nodes; ++i)
    {
        gu_trace(dn.push_back(
                     create_dummy_node(i, 0,
                                       suspect_timeout,
                                       inactive_timeout, retrans_period)));
    }

    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        set_cvi(dn, 0, i, i + 1);
        gu_trace(prop.propagate_until_cvi(false));
    }
    uint32_t view_seq = n_nodes + 1;

    dn[0]->close(dn[1]->uuid());
    dn[1]->close(dn[0]->uuid());
    dn[0]->set_cvi(ViewId(V_REG, dn[0]->uuid(), view_seq));
    dn[2]->set_cvi(ViewId(V_REG, dn[0]->uuid(), view_seq));
    dn[1]->set_cvi(ViewId(V_REG, dn[1]->uuid(), view_seq));
    gu_trace(prop.propagate_until_cvi(true));

    dn[0]->set_cvi(ViewId(V_REG, dn[0]->uuid(), view_seq + 1));
    dn[1]->set_cvi(ViewId(V_REG, dn[0]->uuid(), view_seq + 1));
    dn[2]->set_cvi(ViewId(V_REG, dn[0]->uuid(), view_seq + 1));
    gu_trace(prop.propagate_until_cvi(true));

    gu_trace(check_trace(dn));

    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST


START_TEST(test_proto_split_two)
{
    log_info << "START";
    const size_t n_nodes(2);
    PropagationMatrix prop;
    vector<DummyNode*> dn;
    const string suspect_timeout("PT0.31S");
    const string inactive_timeout("PT0.31S");
    const string retrans_period("PT0.1S");

    for (size_t i = 1; i <= n_nodes; ++i)
    {
        gu_trace(dn.push_back(
                     create_dummy_node(i, 0, suspect_timeout,
                                       inactive_timeout, retrans_period)));
    }

    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        set_cvi(dn, 0, i, i + 1);
        gu_trace(prop.propagate_until_cvi(false));
    }
    uint32_t view_seq = n_nodes + 1;

    dn[0]->close(dn[1]->uuid());
    dn[1]->close(dn[0]->uuid());
    dn[0]->set_cvi(ViewId(V_REG, dn[0]->uuid(), view_seq));
    dn[1]->set_cvi(ViewId(V_REG, dn[1]->uuid(), view_seq));

    gu_trace(prop.propagate_until_cvi(true));

    dn[0]->set_cvi(ViewId(V_REG, dn[0]->uuid(), view_seq + 1));
    dn[1]->set_cvi(ViewId(V_REG, dn[0]->uuid(), view_seq + 1));
    gu_trace(prop.propagate_until_cvi(true));

    gu_trace(check_trace(dn));

    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST

START_TEST(test_aggreg)
{
    log_info << "START";
    const size_t n_nodes(2);
    PropagationMatrix prop;
    vector<DummyNode*> dn;
    const string suspect_timeout("PT0.31S");
    const string inactive_timeout("PT0.31S");
    const string retrans_period("PT0.1S");

    for (size_t i = 1; i <= n_nodes; ++i)
    {
        gu_trace(dn.push_back(
                     create_dummy_node(i, 0, suspect_timeout,
                                       inactive_timeout, retrans_period)));
    }

    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        set_cvi(dn, 0, i, i + 1);
        gu_trace(prop.propagate_until_cvi(false));
    }

    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(send_n(dn[i], 8));
    }

    gu_trace(prop.propagate_until_empty());
    gu_trace(check_trace(dn));

    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST

START_TEST(test_trac_538)
{
    gu_conf_self_tstamp_on();
    log_info << "START (test_trac_538)";
    init_rand();
    const size_t n_nodes(5);
    PropagationMatrix prop;
    vector<DummyNode*> dn;
    const string suspect_timeout("PT0.5S");
    const string inactive_timeout("PT2S");
    const string retrans_period("PT0.1S");

    for (size_t i = 1; i <= n_nodes; ++i)
    {
        gu_trace(dn.push_back(
                     create_dummy_node(i, 0, suspect_timeout,
                                       inactive_timeout,
                                       retrans_period)));
    }

    for (size_t i = 0; i < n_nodes - 1; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        set_cvi(dn, 0, i, i + 1);
        gu_trace(prop.propagate_until_cvi(false));
    }

    uint32_t max_view_seq(get_max_view_seq(dn, 0, n_nodes - 1));

    gu_trace(join_node(&prop, dn[n_nodes - 1], false));
    for (size_t i = 1; i <= n_nodes; ++i)
    {
        if (i != n_nodes - 1)
        {
            prop.set_loss(i, n_nodes - 1, 0);
            prop.set_loss(n_nodes - 1, i, 0);
        }
    }
    set_cvi(dn, 0, n_nodes - 1, max_view_seq + 1);
    dn[n_nodes - 2]->set_cvi(ViewId(V_REG, n_nodes - 1, max_view_seq + 1));
    gu_trace(prop.propagate_until_cvi(true));
    gu_trace(check_trace(dn));
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST


START_TEST(test_trac_552)
{
    log_info << "START (trac_552)";
    init_rand();

    const size_t n_nodes(3);
    PropagationMatrix prop;
    vector<DummyNode*> dn;

    const string suspect_timeout("PT15S");
    const string inactive_timeout("PT30S");
    const string retrans_period("PT1S");

    for (size_t i = 1; i <= n_nodes; ++i)
    {
        gu_trace(dn.push_back(
                     create_dummy_node(i, 0, suspect_timeout,
                                       inactive_timeout, retrans_period)));
    }

    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        set_cvi(dn, 0, i, i + 1);
        gu_trace(prop.propagate_until_cvi(false));
    }


    for (size_t i = 0; i < n_nodes; ++i)
    {
        for (size_t j = 1; j < i + 1; ++j)
        {
            prop.set_loss(i + 1, j, 0.9);
            prop.set_loss(j, i + 1, 0.9);
        }
    }

    uint32_t max_view_seq(get_max_view_seq(dn, 0, n_nodes));

    for (size_t j = 0; j < n_nodes; ++j)
    {
        gu_trace(send_n(dn[j], 5 + ::rand() % 4));
    }
    dn[0]->set_cvi(V_REG);
    dn[1]->set_cvi(V_REG);
    set_cvi(dn, 2, n_nodes - 1, max_view_seq + 1);
    dn[0]->close();
    dn[1]->close();
    gu_trace(prop.propagate_until_cvi(true));

    gu_trace(check_trace(dn));
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST


START_TEST(test_trac_607)
{
    gu_conf_self_tstamp_on();
    log_info << "START (trac_607)";

    const size_t n_nodes(3);
    PropagationMatrix prop;
    vector<DummyNode*> dn;

    const string suspect_timeout("PT0.5S");
    const string inactive_timeout("PT1S");
    const string retrans_period("PT0.1S");

    for (size_t i = 1; i <= n_nodes; ++i)
    {
        gu_trace(dn.push_back(
                     create_dummy_node(i, 0, suspect_timeout,
                                       inactive_timeout, retrans_period)));
    }

    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        set_cvi(dn, 0, i, i + 1);
        gu_trace(prop.propagate_until_cvi(false));
    }

    uint32_t max_view_seq(get_max_view_seq(dn, 0, n_nodes));
    dn[0]->set_cvi(V_REG);
    dn[0]->close();

    while (evs_from_dummy(dn[1])->state() != Proto::S_INSTALL)
    {
        prop.propagate_n(1);
    }

    // this used to cause exception:
    // Forbidden state transition: INSTALL -> LEAVING (FATAL)
    dn[1]->close();

    // expected behavior:
    // dn[1], dn[2] reach S_OPERATIONAL and then dn[1] leaves gracefully
    set_cvi(dn, 1, n_nodes - 1, max_view_seq + 1);

    gu_trace(prop.propagate_until_cvi(true));
    max_view_seq = get_max_view_seq(dn, 0, n_nodes);
    dn[1]->set_cvi(V_REG);
    set_cvi(dn, 2, 2, max_view_seq + 1);

    gu_trace(prop.propagate_until_cvi(true));

    gu_trace(check_trace(dn));
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST


START_TEST(test_trac_724)
{
    gu_conf_self_tstamp_on();
    log_info << "START (trac_724)";
    init_rand();

    const size_t n_nodes(2);
    PropagationMatrix prop;
    vector<DummyNode*> dn;
    Protolay::sync_param_cb_t sync_param_cb;

    const string suspect_timeout("PT0.5S");
    const string inactive_timeout("PT1S");
    const string retrans_period("PT0.1S");

    for (size_t i = 1; i <= n_nodes; ++i)
    {
        gu_trace(dn.push_back(
                     create_dummy_node(i, 0, suspect_timeout,
                                       inactive_timeout, retrans_period)));
    }

    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        set_cvi(dn, 0, i, i + 1);
        gu_trace(prop.propagate_until_cvi(false));
    }

    // Slightly asymmetric settings and evs.use_aggregate=false to
    // allow completion window to grow over 0xff.
    Proto* evs0(evs_from_dummy(dn[0]));

    bool ret(evs0->set_param("evs.use_aggregate", "false", sync_param_cb));
    ck_assert(ret == true);
    ret = evs0->set_param("evs.send_window", "1024", sync_param_cb);
    ck_assert(ret == true);
    ret = evs0->set_param("evs.user_send_window", "515", sync_param_cb);
    Proto* evs1(evs_from_dummy(dn[1]));
    ret = evs1->set_param("evs.use_aggregate", "false", sync_param_cb);
    ck_assert(ret == true);
    ret = evs1->set_param("evs.send_window", "1024", sync_param_cb);
    ck_assert(ret == true);
    ret = evs1->set_param("evs.user_send_window", "512", sync_param_cb);

    prop.set_loss(1, 2, 0.);

    for (size_t i(0); i < 256; ++i)
    {
        dn[0]->send();
        dn[0]->send();
        dn[1]->send();
        gu_trace(prop.propagate_until_empty());
    }
    dn[0]->send();
    prop.set_loss(1, 2, 1.);

    dn[0]->send();
    gu_trace(prop.propagate_until_empty());

    gu_trace(check_trace(dn));
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST


START_TEST(test_trac_760)
{
    gu_conf_self_tstamp_on();
    log_info << "START (trac_760)";
    init_rand();

    const size_t n_nodes(3);
    PropagationMatrix prop;
    vector<DummyNode*> dn;

    const string suspect_timeout("PT0.5S");
    const string inactive_timeout("PT1S");
    const string retrans_period("PT0.1S");

    for (size_t i = 1; i <= n_nodes; ++i)
    {
        gu_trace(dn.push_back(
                     create_dummy_node(i, 0, suspect_timeout,
                                       inactive_timeout, retrans_period)));
    }

    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        set_cvi(dn, 0, i, i + 1);
        gu_trace(prop.propagate_until_cvi(false));
    }

    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(send_n(dn[i], 2));

    }
    gu_trace(prop.propagate_until_empty());


    uint32_t max_view_seq(get_max_view_seq(dn, 0, n_nodes));
    gu_trace(send_n(dn[0], 1));
    gu_trace(send_n(dn[1], 1));
    // gu_trace(send_n(dn[2], 1));

    set_cvi(dn, 0, 1, max_view_seq + 1);
    dn[2]->set_cvi(V_REG);
    dn[2]->close();

    Proto* evs0(evs_from_dummy(dn[0]));
    Proto* evs1(evs_from_dummy(dn[1]));
    while (evs1->state() != Proto::S_GATHER && evs0->state() != Proto::S_GATHER)
    {
        gu_trace(prop.propagate_n(1));
    }
    dn[1]->close();

    gu_trace(prop.propagate_until_cvi(true));

    gu_trace(check_trace(dn));
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST

START_TEST(test_gh_41)
{
    gu_conf_self_tstamp_on();
    log_info << "START (gh_41)";

    const size_t n_nodes(3);
    PropagationMatrix prop;
    vector<DummyNode*> dn;

    const string suspect_timeout("PT0.5S");
    const string inactive_timeout("PT1S");
    const string retrans_period("PT0.1S");

    for (size_t i = 1; i <= n_nodes; ++i)
    {
        gu_trace(dn.push_back(
                     create_dummy_node(i, 0, suspect_timeout,
                                       inactive_timeout, retrans_period)));
    }

    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        set_cvi(dn, 0, i, i + 1);
        gu_trace(prop.propagate_until_cvi(false));
    }

    // Generate partitioning so that the node with smallest UUID
    // creates singleton view
    log_info << "partition";
    prop.set_loss(1, 2, 0.);
    prop.set_loss(2, 1, 0.);
    prop.set_loss(1, 3, 0.);
    prop.set_loss(3, 1, 0.);
    uint32_t max_view_seq(get_max_view_seq(dn, 0, n_nodes));

    dn[0]->set_cvi(ViewId(V_REG, dn[0]->uuid(), max_view_seq + 1));
    dn[1]->set_cvi(ViewId(V_REG, dn[1]->uuid(), max_view_seq + 1));
    dn[2]->set_cvi(ViewId(V_REG, dn[1]->uuid(), max_view_seq + 1));

    prop.propagate_until_cvi(true);

    // Merge groups and make node 1 leave so that nodes 2 and 3 see
    // leave message from unknown origin
    log_info << "merge";
    prop.set_loss(1, 2, 1.);
    prop.set_loss(2, 1, 1.);
    prop.set_loss(1, 3, 1.);
    prop.set_loss(3, 1, 1.);

    // Send message so that nodes 2 and 3 shift to GATHER. This must be done
    // because LEAVE message is ignored in handle_foreign()
    dn[0]->send();
    dn[0]->close();

    dn[0]->set_cvi(V_REG);
    dn[1]->set_cvi(ViewId(V_REG, dn[1]->uuid(), max_view_seq + 2));
    dn[2]->set_cvi(ViewId(V_REG, dn[1]->uuid(), max_view_seq + 2));

    prop.propagate_until_cvi(true);
    check_trace(dn);
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST

START_TEST(test_gh_37)
{
    gu_conf_self_tstamp_on();
    log_info << "START (gh_37)";

    const size_t n_nodes(3);
    PropagationMatrix prop;
    vector<DummyNode*> dn;

    const string suspect_timeout("PT0.5S");
    const string inactive_timeout("PT1S");
    const string retrans_period("PT0.1S");

    for (size_t i = 1; i <= n_nodes; ++i)
    {
        gu_trace(dn.push_back(
                     create_dummy_node(i, 0, suspect_timeout,
                                       inactive_timeout, retrans_period)));
    }

    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        set_cvi(dn, 0, i, i + 1);
        gu_trace(prop.propagate_until_cvi(false));
    }

    uint32_t max_view_seq(get_max_view_seq(dn, 0, n_nodes));
    // node 0 is gonna to leave
    for(size_t i = 2; i <= n_nodes; i++)
    {
        // leaving node(LN) is able to send messages to remaining nodes.
        // prop.set_loss(1, i, 0.);
        // but remaining nodes(RNS) won't be able to ack these messages.
        prop.set_loss(i, 1, 0.);
        // so RNS aru_seq are the same and higher than LN aru_seq.
    }
    // LN  ss=-1, ir=[2,1]
    // RNS ss=1,  ir=[2,1]
    dn[0]->send();
    dn[0]->send();
    dn[0]->close();

    dn[0]->set_cvi(V_REG);
    dn[1]->set_cvi(ViewId(V_REG, dn[1]->uuid(), max_view_seq + 1));
    dn[2]->set_cvi(ViewId(V_REG, dn[1]->uuid(), max_view_seq + 1));

    prop.propagate_until_cvi(true);
    check_trace(dn);
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST

START_TEST(test_gh_40)
{
    gu_conf_self_tstamp_on();
    log_info << "START (gh_40)";

    const size_t n_nodes(3);
    PropagationMatrix prop;
    vector<DummyNode*> dn;

    const string suspect_timeout("PT0.5S");
    const string inactive_timeout("PT1S");
    const string retrans_period("PT0.1S");

    for (size_t i = 1; i <= n_nodes; ++i)
    {
        gu_trace(dn.push_back(
                     create_dummy_node(i, 0, suspect_timeout,
                                       inactive_timeout, retrans_period)));
    }

    for (size_t i = 0; i < n_nodes; ++i)
    {
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        set_cvi(dn, 0, i, i + 1);
        gu_trace(prop.propagate_until_cvi(false));
    }
    uint32_t max_view_seq(get_max_view_seq(dn, 0, n_nodes));

    // ss=0, ir=[1,0];
    dn[0]->send();
    gu_trace(prop.propagate_until_empty());
    log_info << "gh_40 all got operational state";

    // cut dn[0] from dn[1] and dn[2].
    for (size_t i = 2; i <= n_nodes; ++i)
    {
        prop.set_loss(1, i, 0.);
        prop.set_loss(i, 1, 0.);
    }

    // ss=0, ir=[2,1];
    // dn[1] send msg(seq=1)
    dn[1]->send();

    Proto* evs1 = evs_from_dummy(dn[1]);
    Proto* evs2 = evs_from_dummy(dn[2]);
    ck_assert(evs1->state() == Proto::S_OPERATIONAL);
    ck_assert(evs2->state() == Proto::S_OPERATIONAL);
    evs1->set_inactive(dn[0]->uuid());
    evs2->set_inactive(dn[0]->uuid());
    evs1->check_inactive();
    evs2->check_inactive();
    ck_assert(evs1->state() == Proto::S_GATHER);
    ck_assert(evs2->state() == Proto::S_GATHER);

    // Advance clock to get over join message rate limiting.
    gu::datetime::SimClock::inc_time(100*gu::datetime::MSec);
    while(!(evs1->state() == Proto::S_GATHER &&
            evs1->is_install_message()))
    {
        gu_trace(prop.propagate_n(1));
    }

    // dn[0] comes back.
    // here we have to set message F_RETRANS
    // otherwise handle_msg ignores this msg.
    // @todo:why?

    // dn[0] ack dn[1] msg(seq=1) with flags F_RETRANS.
    Datagram dg1 = dn[0]->create_datagram();
    UserMessage msg1(0,
                     dn[0]->uuid(),
                     ViewId(V_REG, dn[0]->uuid(), max_view_seq),
                     1, 0, 0, O_DROP, 1, 0xff,
                     Message::F_RETRANS);
    // dn[0] msg(seq=2) leak into dn[1] input_map.
    Datagram dg2 = dn[0]->create_datagram();
    UserMessage msg2(0,
                     dn[0]->uuid(),
                     ViewId(V_REG, dn[0]->uuid(), max_view_seq),
                     2, 0, 0, O_SAFE, 2, 0xff,
                     Message::F_RETRANS);
    // so for dn[1]
    // input_map:       ss=0, ir=[3,2]
    // install message: ss=0, ir=[2,1]
    // seq 1 = O_SAFE message.(initiated by self)
    // seq 2 = O_DROP message.(complete_user)
    push_header(msg1, dg1);
    evs1->handle_up(0, dg1, ProtoUpMeta(dn[0]->uuid()));
    push_header(msg2, dg2);
    log_info << "evs1 handle msg " << msg2;
    log_info << "before handle msg: " << *evs1;
    evs1->handle_up(0, dg2, ProtoUpMeta(dn[0]->uuid()));
    log_info << "after handle msg: " << *evs1;

    dn[0]->set_cvi(ViewId(V_REG, dn[0]->uuid(), max_view_seq + 1));
    dn[1]->set_cvi(ViewId(V_REG, dn[1]->uuid(), max_view_seq + 1));
    dn[2]->set_cvi(ViewId(V_REG, dn[1]->uuid(), max_view_seq + 1));
    prop.propagate_until_cvi(true);
    check_trace(dn);
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST


START_TEST(test_gh_100)
{
    log_info << "START (test_gh_100)";
    gu::Config conf;
    mark_point();
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    conf.set("evs.info_log_mask", "0x3");
    conf.set("evs.debug_log_mask", "0xa0");
    UUID uuid1(1), uuid2(2);
    DummyTransport t1(uuid1), t2(uuid2);
    mark_point();
    DummyUser u1(conf), u2(conf);
    mark_point();
    Proto p1(conf, uuid1, 0, gu::URI("evs://"), 10000, 0);
    // Start p2 view seqno from higher value than p1
    View p2_rst_view(0, ViewId(V_REG, uuid2, 3));
    Proto p2(conf, uuid2, 0, gu::URI("evs://"), 10000, &p2_rst_view);

    gcomm::connect(&t1, &p1);
    gcomm::connect(&p1, &u1);

    gcomm::connect(&t2, &p2);
    gcomm::connect(&p2, &u2);

    single_join(&t1, &p1);


    // The following is from double_join(). Process messages until
    // install message is generated. After that handle install timer
    // on p1 and verify that the newly generated install message has
    // greater install view id seqno than the first one.
    Message jm;
    Message im;
    Message im2;
    Message gm;
    Message gm2;
    Message msg;

    Datagram* rb;

    // Initial states check
    p2.shift_to(Proto::S_JOINING);
    ck_assert(p1.state() == Proto::S_OPERATIONAL);
    ck_assert(p2.state() == Proto::S_JOINING);

    // Send join message, don't self handle immediately
    // Expected output: one join message
    p2.send_join(false);
    ck_assert(p2.state() == Proto::S_JOINING);
    rb = get_msg(&t2, &jm);
    ck_assert(rb != 0);
    ck_assert(jm.type() == Message::EVS_T_JOIN);
    rb = get_msg(&t2, &msg);
    ck_assert(rb == 0);

    // Handle node 2's join on node 1
    // Expected output: shift to S_GATHER and one join message
    p1.handle_msg(jm);
    ck_assert(p1.state() == Proto::S_GATHER);
    rb = get_msg(&t1, &jm);
    ck_assert(rb != 0);
    ck_assert(jm.type() == Message::EVS_T_JOIN);
    rb = get_msg(&t1, &msg);
    ck_assert(rb == 0);

    // Handle node 1's join on node 2
    // Expected output: shift to S_GATHER and one join message
    p2.handle_msg(jm);
    ck_assert(p2.state() == Proto::S_GATHER);
    rb = get_msg(&t2, &jm);
    ck_assert(rb != 0);
    ck_assert(jm.type() == Message::EVS_T_JOIN);
    rb = get_msg(&t2, &msg);
    ck_assert(rb == 0);

    // Handle node 2's join on node 1
    // Expected output: Install and commit gap messages, state stays in S_GATHER
    p1.handle_msg(jm);
    ck_assert(p1.state() == Proto::S_GATHER);
    rb = get_msg(&t1, &im);
    ck_assert(rb != 0);
    ck_assert(im.type() == Message::EVS_T_INSTALL);
    rb = get_msg(&t1, &gm);
    ck_assert(rb != 0);
    ck_assert(gm.type() == Message::EVS_T_GAP);
    ck_assert((gm.flags() & Message::F_COMMIT) != 0);
    rb = get_msg(&t1, &msg);
    ck_assert(rb == 0);

    // Handle timers to  to generate shift to GATHER
    p1.handle_inactivity_timer();
    p1.handle_install_timer();
    rb = get_msg(&t1, &jm);
    ck_assert(rb != 0);
    ck_assert(jm.type() == Message::EVS_T_JOIN);
    rb = get_msg(&t1, &im2);
    ck_assert(rb != 0);
    ck_assert(im2.type() == Message::EVS_T_INSTALL);
    ck_assert(im2.install_view_id().seq() > im.install_view_id().seq());

    gcomm::Datagram* tmp;
    while ((tmp = t1.out())) delete tmp;
    while ((tmp = t2.out())) delete tmp;
}
END_TEST

START_TEST(test_evs_protocol_upgrade)
{
    log_info << "START (test_evs_protocol_upgrade)";
    PropagationMatrix prop;
    vector<DummyNode*> dn;

    uint32_t view_seq(0);
    for (int i(0); i <= GCOMM_PROTOCOL_MAX_VERSION; ++i)
    {
        gu_trace(dn.push_back(create_dummy_node(i + 1, i)));
        gu_trace(join_node(&prop, dn[i], i == 0 ? true : false));
        set_cvi(dn, 0, i, view_seq + 1);
        gu_trace(prop.propagate_until_cvi(false));
        ++view_seq;
        for (int j(0); j <= i; ++j)
        {
            ck_assert(evs_from_dummy(dn[j])->current_view().version() == 0);
            gu_trace(send_n(dn[j], 5 + ::rand() % 4));
        }
    }

    for (int i(0); i < GCOMM_PROTOCOL_MAX_VERSION; ++i)
    {
        for (int j(i); j <= GCOMM_PROTOCOL_MAX_VERSION; ++j)
        {
            gu_trace(send_n(dn[j], 5 + ::rand() % 4));
        }
        dn[i]->close();
        dn[i]->set_cvi(V_REG);
        set_cvi(dn, i + 1, GCOMM_PROTOCOL_MAX_VERSION, view_seq);
        gu_trace(prop.propagate_until_cvi(true));
        ++view_seq;
        for (int j(i + 1); j <= GCOMM_PROTOCOL_MAX_VERSION; ++j)
        {
            gu_trace(send_n(dn[j], 5 + ::rand() % 4));
        }
        gu_trace(prop.propagate_until_empty());
    }
    ck_assert(evs_from_dummy(dn[GCOMM_PROTOCOL_MAX_VERSION])->current_view().version() == GCOMM_PROTOCOL_MAX_VERSION);
    check_trace(dn);
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST


START_TEST(test_gal_521)
{
    // Test the case where two nodes exhaust their user send windows
    // simultaneously.
    log_info << "Start test_gal_521";

    std::vector<DummyNode*> dn;
    Protolay::sync_param_cb_t sync_param_cb;

    dn.push_back(create_dummy_node(1, 0));
    dn.push_back(create_dummy_node(2, 0));


    gcomm::evs::Proto *evs1(evs_from_dummy(dn[0]));
    DummyTransport* t1(transport_from_dummy(dn[0]));
    t1->set_queueing(true);

    gcomm::evs::Proto *evs2(evs_from_dummy(dn[1]));
    DummyTransport* t2(transport_from_dummy(dn[1]));
    t2->set_queueing(true);

    single_join(t1, evs1);
    double_join(t1, evs1, t2, evs2);

    ck_assert(t1->empty() == true);
    ck_assert(t2->empty() == true);

    // Adjust send windows to allow sending only single user generated
    // message at the time
    evs1->set_param(gcomm::Conf::EvsUserSendWindow, "1", sync_param_cb);
    evs1->set_param(gcomm::Conf::EvsSendWindow, "1", sync_param_cb);

    evs2->set_param(gcomm::Conf::EvsUserSendWindow, "1", sync_param_cb);
    evs2->set_param(gcomm::Conf::EvsSendWindow, "1", sync_param_cb);

    // Make both sides send two messages without communicating with
    // each other. This will place one user message into transport
    // queue and one into send queue for both nodes.
    send_n(dn[0], 2);
    ck_assert(t1->empty() == false);
    send_n(dn[1], 2);
    ck_assert(t2->empty() == false);

    Datagram *d1;
    Message um1;
    ck_assert((d1 = get_msg(t1, &um1, false)) != 0);
    ck_assert(um1.type() == Message::EVS_T_USER);
    ck_assert(t1->empty() == true);
    Datagram *d2;
    Message um2;
    ck_assert((d2 = get_msg(t2, &um2, false)) != 0);
    ck_assert(um2.type() == Message::EVS_T_USER);
    ck_assert(t2->empty() == true);

    // Both of the nodes handle each other's messages. Now due to
    // send_window == 1 they are not allowed to send the second
    // message since safe_seq has not been incremented. Instead, they
    // must emit gap messages to make safe_seq to progress.
    evs1->handle_up(0, *d2, ProtoUpMeta(dn[1]->uuid()));
    delete d2;
    Message gm1;
    ck_assert(get_msg(t1, &gm1) != 0);
    ck_assert(gm1.type() == Message::EVS_T_GAP);
    ck_assert(t1->empty() == true);

    evs2->handle_up(0, *d1, ProtoUpMeta(dn[0]->uuid()));
    delete d1;
    Message gm2;
    ck_assert(get_msg(t2, &gm2) != 0);
    ck_assert(gm2.type() == Message::EVS_T_GAP);
    ck_assert(t2->empty() == true);

    // Handle gap messages. The safe_seq is now incremented so the
    // second user messages are now sent from output queue.
    evs1->handle_msg(gm2);
    ck_assert((d1 = get_msg(t1, &um1, false)) != 0);
    ck_assert(um1.type() == Message::EVS_T_USER);
    ck_assert(t1->empty() == true);

    evs2->handle_msg(gm1);
    ck_assert((d2 = get_msg(t2, &um2, false)) != 0);
    ck_assert(um2.type() == Message::EVS_T_USER);
    ck_assert(t2->empty() == true);

    // Handle user messages. Each node should now emit gap
    // because the output queue is empty.
    evs1->handle_up(0, *d2, ProtoUpMeta(dn[1]->uuid()));
    delete d2;
    ck_assert(get_msg(t1, &gm1) != 0);
    ck_assert(gm1.type() == Message::EVS_T_GAP);
    ck_assert(t1->empty() == true);

    evs2->handle_up(0, *d1, ProtoUpMeta(dn[0]->uuid()));
    delete d1;
    ck_assert(get_msg(t2, &gm2) != 0);
    ck_assert(gm2.type() == Message::EVS_T_GAP);
    ck_assert(t2->empty() == true);

    // Handle gap messages. No further messages should be emitted
    // since both user messages have been delivered, there are
    // no pending user messages in the output queue and no timers
    // have been expired.
    evs1->handle_msg(gm2);
    ck_assert((d1 = get_msg(t1, &um1, false)) == 0);

    evs2->handle_msg(gm1);
    ck_assert((d2 = get_msg(t2, &um2, false)) == 0);


    std::for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST

struct TwoNodeFixture
{
    struct Configs
    {
        Configs()
            : conf1()
            , conf2()
        {
            gu::ssl_register_params(conf1);
            gcomm::Conf::register_params(conf1);
            gcomm::Conf::register_params(conf2);
        }
        gu::Config conf1;  // Config for node1
        gu::Config conf2;  // Config for node2
    };
    TwoNodeFixture()
        : conf()
        , uuid1(1)
        , uuid2(2)
        , tr1(uuid1)
        , tr2(uuid2)
        , evs1(conf.conf1, uuid1, 0)
        , evs2(conf.conf2, uuid2, 0)
        , top1(conf.conf1)
        , top2(conf.conf2)
    {
        gcomm::connect(&tr1, &evs1);
        gcomm::connect(&evs1, &top1);
        gcomm::connect(&tr2, &evs2);
        gcomm::connect(&evs2, &top2);
        single_join(&tr1, &evs1);
        double_join(&tr1, &evs1, &tr2, &evs2);
    }
    Configs conf;
    const gcomm::UUID uuid1; // UUID of node1
    const gcomm::UUID uuid2; // UUID if node2
    DummyTransport tr1; // Transport for node1
    DummyTransport tr2; // Transport for node2
    gcomm::evs::Proto evs1; // Proto for node1
    gcomm::evs::Proto evs2; // Proto for node2
    DummyUser top1;      // Top level layer for node1
    DummyUser top2;      // Top level layer for node2
};

// Verify that gap messages are rate limited when a node receives
// several out of order messages.
START_TEST(test_gap_rate_limit)
{
    log_info << "START test_gap_rate_limit";
    // Start time from 1 sec to avoid hitting gap rate limit for the first
    // gap message.
    gu::datetime::SimClock::init(gu::datetime::Sec);
    gu_log_max_level = GU_LOG_DEBUG;
    TwoNodeFixture f;
    gcomm::Protolay::sync_param_cb_t spcb;

    // Increase evs1 send windows to allow generating out of order messages.
    f.evs1.set_param("evs.send_window", "4", spcb);
    f.evs1.set_param("evs.user_send_window", "4", spcb);
    // Print all debug logging on node2 for test troubleshooting.
    f.evs2.set_param("evs.debug_log_mask", "0xffff", spcb);
    f.evs2.set_param("evs.info_log_mask", "0xff", spcb);
    char data[1] = { 0 };
    gcomm::Datagram dg(gu::SharedBuffer(new gu::Buffer(data, data + 1)));
    // Generate four messages from node1. The first one is ignored,
    // the rest are handled by node2 for generating gap messages.
    f.evs1.handle_down(dg, ProtoDownMeta(O_SAFE));
    gcomm::Datagram* read_dg;
    gcomm::evs::Message um1;
    read_dg = get_msg(&f.tr1, &um1);
    ck_assert(read_dg != 0);
    f.evs1.handle_down(dg, ProtoDownMeta(O_SAFE));
    gcomm::evs::Message um2;
    read_dg = get_msg(&f.tr1, &um2);
    ck_assert(read_dg != 0);
    f.evs1.handle_down(dg, ProtoDownMeta(O_SAFE));
    gcomm::evs::Message um3;
    read_dg = get_msg(&f.tr1, &um3);
    ck_assert(read_dg != 0);
    f.evs1.handle_down(dg, ProtoDownMeta(O_SAFE));
    gcomm::evs::Message um4;
    read_dg = get_msg(&f.tr1, &um4);
    ck_assert(read_dg != 0);

    // Make node2 handle an out of order message and verify that gap is emitted
    f.evs2.handle_msg(um2);
    gcomm::evs::Message gm1;
    read_dg = get_msg(&f.tr2, &gm1);
    ck_assert(read_dg != 0);
    ck_assert(gm1.type() == gcomm::evs::Message::EVS_T_GAP);
    ck_assert(gm1.range_uuid() == f.uuid1);
    ck_assert(gm1.range().lu() == 0);
    ck_assert(gm1.range().hs() == 0);
    // The node2 will also send an user message to complete the sequence
    // number. Consume it.
    gcomm::evs::Message comp_um1;
    read_dg = get_msg(&f.tr2, &comp_um1);
    ck_assert(read_dg != 0);
    ck_assert(comp_um1.type() == gcomm::evs::Message::EVS_T_USER);
    ck_assert(comp_um1.seq() + comp_um1.seq_range() == 1);
    // No further messages should be emitted
    read_dg = get_msg(&f.tr2, &comp_um1);
    ck_assert(read_dg == 0);

    // Handle the second out of order message, gap should not be emitted.
    // There will be a next user message which completes the um3.
    f.evs2.handle_msg(um3);
    gcomm::evs::Message comp_um2;
    read_dg = get_msg(&f.tr2, &comp_um2);
    ck_assert(read_dg != 0);
    ck_assert(comp_um2.type() == gcomm::evs::Message::EVS_T_USER);
    ck_assert(comp_um2.seq() + comp_um2.seq_range() == 2);

    // There should not be any more gap messages.
    read_dg = get_msg(&f.tr2, &gm1);
    ck_assert(read_dg == 0);

    // Move the clock forwards and handle the fourth message, gap should
    // now emitted.
    gu::datetime::SimClock::inc_time(100*gu::datetime::MSec);
    gcomm::evs::Message gm2;
    f.evs2.handle_msg(um4);
    read_dg = get_msg(&f.tr2, &gm2);
    ck_assert(read_dg != 0);
    ck_assert(gm2.type() == gcomm::evs::Message::EVS_T_GAP);
    ck_assert(gm2.range().lu() == 0);
    ck_assert(gm2.range().hs() == 0);

    gcomm::evs::Message comp_u4;
    read_dg = get_msg(&f.tr2, &comp_u4);
    ck_assert(read_dg != 0);
    ck_assert(comp_u4.type() == gcomm::evs::Message::EVS_T_USER);
    log_info << "END test_gap_rate_limit";
}
END_TEST

// Verify that gap messages are rate limited when the liveness check finds
// delayed node.
START_TEST(test_gap_rate_limit_delayed)
{
    log_info << "START test_gap_rate_limit_delayed";
    // Start time from 1 sec to avoid hitting gap rate limit for the first
    // gap message.
    gu::datetime::SimClock::init(gu::datetime::Sec);
    gu_log_max_level = GU_LOG_DEBUG;
    TwoNodeFixture f;
    gcomm::Protolay::sync_param_cb_t spcb;

    // Increase evs1 send windows to allow generating out of order messages.
    f.evs1.set_param("evs.send_window", "4", spcb);
    f.evs1.set_param("evs.user_send_window", "4", spcb);
    // Print all debug logging on node2 for test troubleshooting.
    f.evs2.set_param("evs.debug_log_mask", "0xffff", spcb);
    f.evs2.set_param("evs.info_log_mask", "0xff", spcb);
    // The retransmission request is done for delayed only if
    // auto evict is on.
    f.evs2.set_param("evs.auto_evict", "1", spcb);
    const char data[1] = { 0 };
    gcomm::Datagram dg(gu::SharedBuffer(new gu::Buffer(data, data + 1)));
    // Generate four messages from node1. The first one is ignored,
    // the rest are handled by node2 for generating gap messages.
    f.evs1.handle_down(dg, ProtoDownMeta(O_SAFE));
    gcomm::Datagram* read_dg;
    gcomm::evs::Message um1;
    read_dg = get_msg(&f.tr1, &um1);
    ck_assert(read_dg != 0);
    f.evs1.handle_down(dg, ProtoDownMeta(O_SAFE));
    gcomm::evs::Message um2;
    read_dg = get_msg(&f.tr1, &um2);
    ck_assert(read_dg != 0);
    f.evs1.handle_down(dg, ProtoDownMeta(O_SAFE));
    gcomm::evs::Message um3;
    read_dg = get_msg(&f.tr1, &um3);
    ck_assert(read_dg != 0);
    f.evs1.handle_down(dg, ProtoDownMeta(O_SAFE));
    gcomm::evs::Message um4;
    read_dg = get_msg(&f.tr1, &um4);
    ck_assert(read_dg != 0);

    // Make node2 handle an out of order message and verify that gap is emitted
    f.evs2.handle_msg(um2);
    gcomm::evs::Message gm1;
    read_dg = get_msg(&f.tr2, &gm1);
    ck_assert(read_dg != 0);
    ck_assert(gm1.type() == gcomm::evs::Message::EVS_T_GAP);
    ck_assert(gm1.range_uuid() == f.uuid1);
    ck_assert(gm1.range().lu() == 0);
    ck_assert(gm1.range().hs() == 0);
    // The node2 will also send an user message to complete the sequence
    // number. Consume it.
    gcomm::evs::Message comp_um1;
    read_dg = get_msg(&f.tr2, &comp_um1);
    ck_assert(read_dg != 0);
    ck_assert(comp_um1.type() == gcomm::evs::Message::EVS_T_USER);
    ck_assert(comp_um1.seq() + comp_um1.seq_range() == 1);
    // No further messages should be emitted
    read_dg = get_msg(&f.tr2, &comp_um1);
    ck_assert(read_dg == 0);

    // Move time forwards in 1 sec interval and make inactivity check
    // in between. No gap messages should be emitted.
    gu::datetime::SimClock::inc_time(gu::datetime::Sec);
    f.evs2.handle_inactivity_timer();
    gcomm::evs::Message gm_discard;
    read_dg = get_msg(&f.tr2, &gm_discard);
    ck_assert(read_dg == 0);
    // The clock is now advanced over retrans_period + delay margin. Next
    // call to handle_inactivity_timer() should fire the check. Gap message
    // is emitted.
    gu::datetime::SimClock::inc_time(gu::datetime::Sec);
    f.evs2.handle_inactivity_timer();
    read_dg = get_msg(&f.tr2, &gm1);
    ck_assert(read_dg != 0);
    ck_assert(gm1.type() == gcomm::evs::Message::EVS_T_GAP);
    // Now call handle_inactivity_timer() again, gap message should not
    // be emitted due to rate limit.
    f.evs2.handle_inactivity_timer();
    read_dg = get_msg(&f.tr2, &gm_discard);
    ck_assert(read_dg == 0);
    // Move clock forward 100msec, new gap should be now emitted.
    gu::datetime::SimClock::inc_time(100*gu::datetime::MSec);
    f.evs2.handle_inactivity_timer();
    gcomm::evs::Message gm2;
    read_dg = get_msg(&f.tr2, &gm2);
    ck_assert(read_dg != 0);
    ck_assert(gm2.type() == gcomm::evs::Message::EVS_T_GAP);
    log_info << "END test_gap_rate_limit_delayed";

    gcomm::Datagram* tmp;
    while ((tmp = f.tr1.out())) delete tmp;
    while ((tmp = f.tr2.out())) delete tmp;
}
END_TEST

START_TEST(test_out_queue_limit)
{
    TwoNodeFixture f;

    std::vector<char> data(1 << 15);
    gcomm::Datagram dg(gu::SharedBuffer(
                           new gu::Buffer(data.begin(), data.end())));
    // Default user send window is 2 and out queue limit is 1M,
    // so we can write 2 + 32 messages without blocking.
    for (size_t i(0); i < 34; ++i)
    {
        ck_assert(f.evs1.handle_down(dg, ProtoDownMeta(O_SAFE)) == 0);
    }
    // The next write should fill the out_queue and return EAGAIN
    const char small_data[1] = { 0 };
    dg = gu::SharedBuffer(new gu::Buffer(small_data, small_data + 1));
    ck_assert(f.evs1.handle_down(dg, ProtoDownMeta(O_SAFE)) == EAGAIN);

    gcomm::Datagram* tmp;
    while ((tmp = f.tr1.out())) delete tmp;
}
END_TEST

Suite* evs2_suite()
{
    Suite* s = suite_create("gcomm::evs");
    TCase* tc;

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
    tcase_set_timeout(tc, 60);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_input_map_random_insert");
    tcase_add_test(tc, test_input_map_random_insert);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_input_map_gap_range_list");
    tcase_add_test(tc, test_input_map_gap_range_list);
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

    tc = tcase_create("test_proto_leave_n");
    tcase_add_test(tc, test_proto_leave_n);
    tcase_set_timeout(tc, 20);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_proto_leave_n_w_user_msg");
    tcase_add_test(tc, test_proto_leave_n_w_user_msg);
    tcase_set_timeout(tc, 20);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_proto_leave_n_lossy");
    tcase_add_test(tc, test_proto_leave_n_lossy);
    tcase_set_timeout(tc, 25);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_proto_leave_n_lossy_w_user_msg");
    tcase_add_test(tc, test_proto_leave_n_lossy_w_user_msg);
    tcase_set_timeout(tc, 25);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_proto_split_merge");
    tcase_add_test(tc, test_proto_split_merge);
    tcase_set_timeout(tc, 20);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_proto_split_merge_lossy");
    tcase_add_test(tc, test_proto_split_merge_lossy);
    tcase_set_timeout(tc, 20);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_proto_split_merge_w_user_msg");
    tcase_add_test(tc, test_proto_split_merge_w_user_msg);
    tcase_set_timeout(tc, 60);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_proto_split_merge_lossy_w_user_msg");
    tcase_add_test(tc, test_proto_split_merge_lossy_w_user_msg);
    tcase_set_timeout(tc, 60);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_proto_stop_cont");
    tcase_add_test(tc, test_proto_stop_cont);
    tcase_set_timeout(tc, 10);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_proto_split_two");
    tcase_add_test(tc, test_proto_split_two);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_aggreg");
    tcase_add_test(tc, test_aggreg);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_proto_arbitrate");
    tcase_add_test(tc, test_proto_arbitrate);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_trac_538");
    tcase_add_test(tc, test_trac_538);
    tcase_set_timeout(tc, 15);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_trac_552");
    tcase_add_test(tc, test_trac_552);
    tcase_set_timeout(tc, 15);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_trac_607");
    tcase_add_test(tc, test_trac_607);
    tcase_set_timeout(tc, 15);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_trac_724");
    tcase_add_test(tc, test_trac_724);
    tcase_set_timeout(tc, 15);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_trac_760");
    tcase_add_test(tc, test_trac_760);
    tcase_set_timeout(tc, 15);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_gh_41");
    tcase_add_test(tc, test_gh_41);
    tcase_set_timeout(tc, 15);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_gh_37");
    tcase_add_test(tc, test_gh_37);
    tcase_set_timeout(tc, 15);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_gh_40");
    tcase_add_test(tc, test_gh_40);
    tcase_set_timeout(tc, 5);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_gh_100");
    tcase_add_test(tc, test_gh_100);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_evs_protocol_upgrade");
    tcase_add_test(tc, test_evs_protocol_upgrade);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_gal_521");
    tcase_add_test(tc, test_gal_521);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_gap_rate_limit");
    tcase_add_test(tc, test_gap_rate_limit);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_gap_rate_limit_delayed");
    tcase_add_test(tc, test_gap_rate_limit_delayed);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_out_queue_limit");
    tcase_add_test(tc, test_out_queue_limit);
    suite_add_tcase(s, tc);

    return s;
}
