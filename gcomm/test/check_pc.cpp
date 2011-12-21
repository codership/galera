/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */


#include "check_gcomm.hpp"

#include "pc_message.hpp"
#include "pc_proto.hpp"
#include "evs_proto.hpp"

#include "check_templ.hpp"
#include "check_trace.hpp"
#include "gcomm/conf.hpp"

#include <check.h>

#include <list>
#include <cstdlib>
#include <vector>

using namespace std;
using namespace std::rel_ops;
using namespace gu;
using namespace gu::net;
using namespace gu::datetime;
using namespace gcomm;
using namespace gcomm::pc;


START_TEST(test_pc_messages)
{
    StateMessage pcs(0);
    pc::NodeMap& sim(pcs.get_node_map());

    sim.insert(std::make_pair(UUID(0,0),
                              pc::Node(true, 6,
                                     ViewId(V_PRIM,
                                            UUID(0, 0), 9),
                                     42)));
    sim.insert(std::make_pair(UUID(0,0),
                              pc::Node(false, 88, ViewId(V_PRIM,
                                                       UUID(0, 0), 3),
                                     472)));
    sim.insert(std::make_pair(UUID(0,0),
                              pc::Node(true, 78, ViewId(V_PRIM,
                                                      UUID(0, 0), 87),
                                     52)));

    size_t expt_size = 4 // hdr
        + 4              // seq
        + 4 + 3*(UUID::serial_size() + sizeof(uint32_t) + 4 + 20 + 8); // NodeMap
    check_serialization(pcs, expt_size, StateMessage(-1));

    InstallMessage pci(0);
    pc::NodeMap& iim = pci.get_node_map();

    iim.insert(std::make_pair(UUID(0,0),
                              pc::Node(true, 6, ViewId(V_PRIM,
                                                       UUID(0, 0), 9), 42)));
    iim.insert(std::make_pair(UUID(0,0),
                              pc::Node(false, 88, ViewId(V_NON_PRIM,
                                                         UUID(0, 0), 3), 472)));
    iim.insert(std::make_pair(UUID(0,0),
                              pc::Node(true, 78, ViewId(V_PRIM,
                                                        UUID(0, 0), 87), 52)));
    iim.insert(std::make_pair(UUID(0,0),
                              pc::Node(false, 457, ViewId(V_NON_PRIM,
                                                          UUID(0, 0), 37), 56)));

    expt_size = 4 // hdr
        + 4              // seq
        + 4 + 4*(UUID::serial_size() + sizeof(uint32_t) + 4 + 20 + 8); // NodeMap
    check_serialization(pci, expt_size, InstallMessage(-1));

    UserMessage pcu(0, 7);
    pcu.checksum(0xfefe, true);

    expt_size = 4 + 4;
    check_serialization(pcu, expt_size, UserMessage(-1, -1U));
    fail_unless(pcu.serial_size() % 4 == 0);
}
END_TEST

class PCUser : public Toplay
{
    list<View> views;
    PCUser(const PCUser&);
    void operator=(const PCUser&);
public:
    UUID uuid;
    DummyTransport* tp;
    Proto* pc;
    PCUser(gu::Config& conf, const UUID& uuid_,
           DummyTransport *tp_, Proto* pc_) :
        Toplay(conf),
        views(),
        uuid(uuid_),
        tp(tp_),
        pc(pc_)
    {
        gcomm::connect(tp, pc);
        gcomm::connect(pc, this);
    }

    void handle_up(const void* cid, const Datagram& rb,
                   const ProtoUpMeta& um)
    {
        if (um.has_view() == true)
        {
            const View& view(um.get_view());
            log_info << view;
            fail_unless(view.get_type() == V_PRIM ||
                        view.get_type() == V_NON_PRIM);
            views.push_back(View(view));
        }
    }

    void send()
    {
        byte_t pl[4] = {1, 2, 3, 4};
        Buffer buf(pl, pl + sizeof(pl));
        Datagram dg(buf);
        fail_unless(send_down(dg, ProtoDownMeta()) == 0);
    }

};

void get_msg(Datagram* rb, Message* msg, bool release = true)
{
    assert(msg != 0);
    if (rb == 0)
    {
        log_info << "get_msg: (null)";
    }
    else
    {
        // assert(rb->get_header().size() == 0 && rb->get_offset() == 0);
        const byte_t* begin(get_begin(*rb));
        const size_t available(get_available(*rb));
        fail_unless(msg->unserialize(begin,
                                     available, 0) != 0);
        log_info << "get_msg: " << msg->to_string();
        if (release)
            delete rb;
    }

}

void single_boot(PCUser* pu1)
{

    ProtoUpMeta sum1(pu1->uuid);

    View vt0(ViewId(V_TRANS, pu1->uuid, 0));
    vt0.add_member(pu1->uuid, "n1");
    ProtoUpMeta um1(UUID::nil(), ViewId(), &vt0);
    pu1->pc->connect(true);
    // pu1->pc->shift_to(Proto::S_JOINING);
    pu1->pc->handle_up(0, Datagram(), um1);
    fail_unless(pu1->pc->get_state() == Proto::S_TRANS);

    View vr1(ViewId(V_REG, pu1->uuid, 1));
    vr1.add_member(pu1->uuid, "n1");
    ProtoUpMeta um2(UUID::nil(), ViewId(), &vr1);
    pu1->pc->handle_up(0, Datagram(), um2);
    fail_unless(pu1->pc->get_state() == Proto::S_STATES_EXCH);

    Datagram* rb = pu1->tp->get_out();
    fail_unless(rb != 0);
    Message sm1;
    get_msg(rb, &sm1);
    fail_unless(sm1.get_type() == Message::T_STATE);
    fail_unless(sm1.get_node_map().size() == 1);
    {
        const pc::Node& pi1 = pc::NodeMap::get_value(sm1.get_node_map().begin());
        fail_unless(pi1.get_prim() == true);
        fail_unless(pi1.get_last_prim() == ViewId(V_PRIM, pu1->uuid, 0));
    }
    pu1->pc->handle_msg(sm1, Datagram(), sum1);
    fail_unless(pu1->pc->get_state() == Proto::S_INSTALL);

    rb = pu1->tp->get_out();
    fail_unless(rb != 0);
    Message im1;
    get_msg(rb, &im1);
    fail_unless(im1.get_type() == Message::T_INSTALL);
    fail_unless(im1.get_node_map().size() == 1);
    {
        const pc::Node& pi1 = pc::NodeMap::get_value(im1.get_node_map().begin());
        fail_unless(pi1.get_prim() == true);
        fail_unless(pi1.get_last_prim() == ViewId(V_PRIM, pu1->uuid, 0));
    }
    pu1->pc->handle_msg(im1, Datagram(), sum1);
    fail_unless(pu1->pc->get_state() == Proto::S_PRIM);
}

START_TEST(test_pc_view_changes_single)
{
    log_info << "START (test_pc_view_changes_single)";
    gu::Config conf;
    UUID uuid1(0, 0);
    Proto pc1(conf, uuid1);
    DummyTransport tp1;
    PCUser pu1(conf, uuid1, &tp1, &pc1);
    single_boot(&pu1);

}
END_TEST


static void double_boot(PCUser* pu1, PCUser* pu2)
{
    ProtoUpMeta pum1(pu1->uuid);
    ProtoUpMeta pum2(pu2->uuid);

    View t11(ViewId(V_TRANS, pu1->pc->get_current_view().get_id()));
    t11.add_member(pu1->uuid, "n1");
    pu1->pc->handle_view(t11);
    fail_unless(pu1->pc->get_state() == Proto::S_TRANS);

    View t12(ViewId(V_TRANS, pu2->uuid, 0));
    t12.add_member(pu2->uuid, "n2");
    // pu2->pc->shift_to(Proto::S_JOINING);
    pu2->pc->connect(false);
    pu2->pc->handle_view(t12);
    fail_unless(pu2->pc->get_state() == Proto::S_TRANS);

    View r1(ViewId(V_REG,
                   pu1->uuid,
                   pu1->pc->get_current_view().get_id().get_seq() + 1));
    r1.add_member(pu1->uuid, "n1");
    r1.add_member(pu2->uuid, "n2");
    pu1->pc->handle_view(r1);
    fail_unless(pu1->pc->get_state() == Proto::S_STATES_EXCH);

    pu2->pc->handle_view(r1);
    fail_unless(pu2->pc->get_state() == Proto::S_STATES_EXCH);

    Datagram* rb = pu1->tp->get_out();
    fail_unless(rb != 0);
    Message sm1;
    get_msg(rb, &sm1);
    fail_unless(sm1.get_type() == Message::T_STATE);

    rb = pu2->tp->get_out();
    fail_unless(rb != 0);
    Message sm2;
    get_msg(rb, &sm2);
    fail_unless(sm2.get_type() == Message::T_STATE);

    rb = pu1->tp->get_out();
    fail_unless(rb == 0);
    rb = pu2->tp->get_out();
    fail_unless(rb == 0);

    pu1->pc->handle_msg(sm1, Datagram(), pum1);
    rb = pu1->tp->get_out();
    fail_unless(rb == 0);
    fail_unless(pu1->pc->get_state() == Proto::S_STATES_EXCH);
    pu1->pc->handle_msg(sm2, Datagram(), pum2);
    fail_unless(pu1->pc->get_state() == Proto::S_INSTALL);

    pu2->pc->handle_msg(sm1, Datagram(), pum1);
    rb = pu2->tp->get_out();
    fail_unless(rb == 0);
    fail_unless(pu2->pc->get_state() == Proto::S_STATES_EXCH);
    pu2->pc->handle_msg(sm2, Datagram(), pum2);
    fail_unless(pu2->pc->get_state() == Proto::S_INSTALL);

    Message im1;
    UUID imsrc;
    if (pu1->uuid < pu2->uuid)
    {
        rb = pu1->tp->get_out();
        imsrc = pu1->uuid;
    }
    else
    {
        rb = pu2->tp->get_out();
        imsrc = pu2->uuid;
    }

    fail_unless(rb != 0);
    get_msg(rb, &im1);
    fail_unless(im1.get_type() == Message::T_INSTALL);

    fail_unless(pu1->tp->get_out() == 0);
    fail_unless(pu2->tp->get_out() == 0);

    ProtoUpMeta ipum(imsrc);
    pu1->pc->handle_msg(im1, Datagram(), ipum);
    fail_unless(pu1->pc->get_state() == Proto::S_PRIM);

    pu2->pc->handle_msg(im1, Datagram(), ipum);
    fail_unless(pu2->pc->get_state() == Proto::S_PRIM);
}

START_TEST(test_pc_view_changes_double)
{
    log_info << "START (test_pc_view_changes_double)";
    gu::Config conf;
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf, uuid1);
    DummyTransport tp1;
    PCUser pu1(conf, uuid1, &tp1, &pc1);
    single_boot(&pu1);

    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf, uuid2);
    DummyTransport tp2;
    PCUser pu2(conf, uuid2, &tp2, &pc2);

    double_boot(&pu1, &pu2);

    Datagram* rb;

    View tnp(ViewId(V_TRANS, pu1.pc->get_current_view().get_id()));
    tnp.add_member(uuid1, "n1");
    pu1.pc->handle_view(tnp);
    fail_unless(pu1.pc->get_state() == Proto::S_TRANS);
    View reg(ViewId(V_REG, uuid1,
                    pu1.pc->get_current_view().get_id().get_seq() + 1));
    reg.add_member(uuid1, "n1");
    pu1.pc->handle_view(reg);
    fail_unless(pu1.pc->get_state() == Proto::S_STATES_EXCH);
    rb = pu1.tp->get_out();
    fail_unless(rb != 0);
    pu1.pc->handle_up(0, *rb, ProtoUpMeta(uuid1));
    fail_unless(pu1.pc->get_state() == Proto::S_NON_PRIM);
    delete rb;

    View tpv2(ViewId(V_TRANS, pu2.pc->get_current_view().get_id()));
    tpv2.add_member(uuid2, "n2");
    tpv2.add_left(uuid1, "n1");
    pu2.pc->handle_view(tpv2);
    fail_unless(pu2.pc->get_state() == Proto::S_TRANS);
    fail_unless(pu2.tp->get_out() == 0);

    View rp2(ViewId(V_REG, uuid2,
                                 pu1.pc->get_current_view().get_id().get_seq() + 1));
    rp2.add_member(uuid2, "n2");
    rp2.add_left(uuid1, "n1");
    pu2.pc->handle_view(rp2);
    fail_unless(pu2.pc->get_state() == Proto::S_STATES_EXCH);
    rb = pu2.tp->get_out();
    fail_unless(rb != 0);
    Message sm2;
    get_msg(rb, &sm2);
    fail_unless(sm2.get_type() == Message::T_STATE);
    fail_unless(pu2.tp->get_out() == 0);
    pu2.pc->handle_msg(sm2, Datagram(), pum2);
    fail_unless(pu2.pc->get_state() == Proto::S_INSTALL);
    rb = pu2.tp->get_out();
    fail_unless(rb != 0);
    Message im2;
    get_msg(rb, &im2);
    fail_unless(im2.get_type() == Message::T_INSTALL);
    pu2.pc->handle_msg(im2, Datagram(), pum2);
    fail_unless(pu2.pc->get_state() == Proto::S_PRIM);

}
END_TEST

/* Test that UUID ordering does not matter when starting nodes */
START_TEST(test_pc_view_changes_reverse)
{
    log_info << "START (test_pc_view_changes_reverse)";
    gu::Config conf;
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf, uuid1);
    DummyTransport tp1;
    PCUser pu1(conf, uuid1, &tp1, &pc1);


    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf, uuid2);
    DummyTransport tp2;
    PCUser pu2(conf, uuid2, &tp2, &pc2);

    single_boot(&pu2);
    double_boot(&pu2, &pu1);
}
END_TEST



START_TEST(test_pc_state1)
{
    log_info << "START (test_pc_state1)";
    gu::Config conf;
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf, uuid1);
    DummyTransport tp1;
    PCUser pu1(conf, uuid1, &tp1, &pc1);
    single_boot(&pu1);

    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf, uuid2);
    DummyTransport tp2;
    PCUser pu2(conf, uuid2, &tp2, &pc2);

    // n1: PRIM -> TRANS -> STATES_EXCH -> RTR -> PRIM
    // n2: JOINING -> STATES_EXCH -> RTR -> PRIM
    double_boot(&pu1, &pu2);

    fail_unless(pu1.pc->get_state() == Proto::S_PRIM);
    fail_unless(pu2.pc->get_state() == Proto::S_PRIM);

    // PRIM -> TRANS -> STATES_EXCH -> RTR -> TRANS -> STATES_EXCH -> RTR -> PRIM
    View tr1(ViewId(V_TRANS, pu1.pc->get_current_view().get_id()));
    tr1.add_member(uuid1, "n1");
    tr1.add_member(uuid2, "n2");
    pu1.pc->handle_view(tr1);
    pu2.pc->handle_view(tr1);

    fail_unless(pu1.pc->get_state() == Proto::S_TRANS);
    fail_unless(pu2.pc->get_state() == Proto::S_TRANS);

    fail_unless(pu1.tp->get_out() == 0);
    fail_unless(pu2.tp->get_out() == 0);

    View reg2(ViewId(V_REG, uuid1,
                     pu1.pc->get_current_view().get_id().get_seq() + 1));
    reg2.add_member(uuid1, "n1");
    reg2.add_member(uuid2, "n2");
    pu1.pc->handle_view(reg2);
    pu2.pc->handle_view(reg2);

    fail_unless(pu1.pc->get_state() == Proto::S_STATES_EXCH);
    fail_unless(pu2.pc->get_state() == Proto::S_STATES_EXCH);

    Message msg;
    get_msg(pu1.tp->get_out(), &msg);
    pu1.pc->handle_msg(msg, Datagram(), pum1);
    pu2.pc->handle_msg(msg, Datagram(), pum1);

    fail_unless(pu1.pc->get_state() == Proto::S_STATES_EXCH);
    fail_unless(pu2.pc->get_state() == Proto::S_STATES_EXCH);

    get_msg(pu2.tp->get_out(), &msg);
    pu1.pc->handle_msg(msg, Datagram(), pum2);
    pu2.pc->handle_msg(msg, Datagram(), pum2);

    fail_unless(pu1.pc->get_state() == Proto::S_INSTALL);
    fail_unless(pu2.pc->get_state() == Proto::S_INSTALL);

    View tr2(ViewId(V_TRANS, pu1.pc->get_current_view().get_id()));
    tr2.add_member(uuid1, "n1");
    tr2.add_member(uuid2, "n2");

    pu1.pc->handle_view(tr2);
    pu2.pc->handle_view(tr2);


    fail_unless(pu1.pc->get_state() == Proto::S_TRANS);
    fail_unless(pu2.pc->get_state() == Proto::S_TRANS);

    Message im;

    if (uuid1 < uuid2)
    {
        get_msg(pu1.tp->get_out(), &im);
        pu1.pc->handle_msg(im, Datagram(), pum1);
        pu2.pc->handle_msg(im, Datagram(), pum1);
    }
    else
    {
        get_msg(pu2.tp->get_out(), &im);
        pu1.pc->handle_msg(im, Datagram(), pum2);
        pu2.pc->handle_msg(im, Datagram(), pum2);
    }


    fail_unless(pu1.pc->get_state() == Proto::S_TRANS);
    fail_unless(pu2.pc->get_state() == Proto::S_TRANS);


    View reg3(ViewId(V_REG, uuid1,
                     pu1.pc->get_current_view().get_id().get_seq() + 1));

    reg3.add_member(uuid1, "n1");
    reg3.add_member(uuid2, "n2");

    pu1.pc->handle_view(reg3);
    pu2.pc->handle_view(reg3);

    fail_unless(pu1.pc->get_state() == Proto::S_STATES_EXCH);
    fail_unless(pu2.pc->get_state() == Proto::S_STATES_EXCH);

    get_msg(pu1.tp->get_out(), &msg);
    pu1.pc->handle_msg(msg, Datagram(), pum1);
    pu2.pc->handle_msg(msg, Datagram(), pum1);

    fail_unless(pu1.pc->get_state() == Proto::S_STATES_EXCH);
    fail_unless(pu2.pc->get_state() == Proto::S_STATES_EXCH);

    get_msg(pu2.tp->get_out(), &msg);
    pu1.pc->handle_msg(msg, Datagram(), pum2);
    pu2.pc->handle_msg(msg, Datagram(), pum2);

    fail_unless(pu1.pc->get_state() == Proto::S_INSTALL);
    fail_unless(pu2.pc->get_state() == Proto::S_INSTALL);

    if (uuid1 < uuid2)
    {
        get_msg(pu1.tp->get_out(), &im);
        pu1.pc->handle_msg(im, Datagram(), pum1);
        pu2.pc->handle_msg(im, Datagram(), pum1);
    }
    else
    {
        get_msg(pu2.tp->get_out(), &im);
        pu1.pc->handle_msg(im, Datagram(), pum2);
        pu2.pc->handle_msg(im, Datagram(), pum2);
    }

    fail_unless(pu1.pc->get_state() == Proto::S_PRIM);
    fail_unless(pu2.pc->get_state() == Proto::S_PRIM);

}
END_TEST

START_TEST(test_pc_state2)
{
    log_info << "START (test_pc_state2)";
    gu::Config conf;
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf, uuid1);
    DummyTransport tp1;
    PCUser pu1(conf, uuid1, &tp1, &pc1);
    single_boot(&pu1);

    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf, uuid2);
    DummyTransport tp2;
    PCUser pu2(conf, uuid2, &tp2, &pc2);

    // n1: PRIM -> TRANS -> STATES_EXCH -> RTR -> PRIM
    // n2: JOINING -> STATES_EXCH -> RTR -> PRIM
    double_boot(&pu1, &pu2);

    fail_unless(pu1.pc->get_state() == Proto::S_PRIM);
    fail_unless(pu2.pc->get_state() == Proto::S_PRIM);

    // PRIM -> TRANS -> STATES_EXCH -> TRANS -> STATES_EXCH -> RTR -> PRIM
    View tr1(ViewId(V_TRANS, pu1.pc->get_current_view().get_id()));
    tr1.add_member(uuid1, "n1");
    tr1.add_member(uuid2, "n2");
    pu1.pc->handle_view(tr1);
    pu2.pc->handle_view(tr1);

    fail_unless(pu1.pc->get_state() == Proto::S_TRANS);
    fail_unless(pu2.pc->get_state() == Proto::S_TRANS);

    fail_unless(pu1.tp->get_out() == 0);
    fail_unless(pu2.tp->get_out() == 0);

    View reg2(ViewId(V_REG, uuid1,
                     pu1.pc->get_current_view().get_id().get_seq() + 1));
    reg2.add_member(uuid1, "n1");
    reg2.add_member(uuid2, "n2");
    pu1.pc->handle_view(reg2);
    pu2.pc->handle_view(reg2);

    fail_unless(pu1.pc->get_state() == Proto::S_STATES_EXCH);
    fail_unless(pu2.pc->get_state() == Proto::S_STATES_EXCH);



    View tr2(ViewId(V_TRANS, pu1.pc->get_current_view().get_id()));
    tr2.add_member(uuid1, "n1");
    tr2.add_member(uuid2, "n2");

    pu1.pc->handle_view(tr2);
    pu2.pc->handle_view(tr2);


    fail_unless(pu1.pc->get_state() == Proto::S_TRANS);
    fail_unless(pu2.pc->get_state() == Proto::S_TRANS);

    Message msg;
    get_msg(pu1.tp->get_out(), &msg);
    pu1.pc->handle_msg(msg, Datagram(), pum1);
    pu2.pc->handle_msg(msg, Datagram(), pum1);

    fail_unless(pu1.pc->get_state() == Proto::S_TRANS);
    fail_unless(pu2.pc->get_state() == Proto::S_TRANS);

    get_msg(pu2.tp->get_out(), &msg);
    pu1.pc->handle_msg(msg, Datagram(), pum2);
    pu2.pc->handle_msg(msg, Datagram(), pum2);

    fail_unless(pu1.pc->get_state() == Proto::S_TRANS);
    fail_unless(pu2.pc->get_state() == Proto::S_TRANS);


    View reg3(ViewId(V_REG, uuid1,
                     pu1.pc->get_current_view().get_id().get_seq() + 1));

    reg3.add_member(uuid1, "n1");
    reg3.add_member(uuid2, "n2");

    pu1.pc->handle_view(reg3);
    pu2.pc->handle_view(reg3);

    fail_unless(pu1.pc->get_state() == Proto::S_STATES_EXCH);
    fail_unless(pu2.pc->get_state() == Proto::S_STATES_EXCH);

    get_msg(pu1.tp->get_out(), &msg);
    pu1.pc->handle_msg(msg, Datagram(), pum1);
    pu2.pc->handle_msg(msg, Datagram(), pum1);

    fail_unless(pu1.pc->get_state() == Proto::S_STATES_EXCH);
    fail_unless(pu2.pc->get_state() == Proto::S_STATES_EXCH);

    get_msg(pu2.tp->get_out(), &msg);
    pu1.pc->handle_msg(msg, Datagram(), pum2);
    pu2.pc->handle_msg(msg, Datagram(), pum2);

    fail_unless(pu1.pc->get_state() == Proto::S_INSTALL);
    fail_unless(pu2.pc->get_state() == Proto::S_INSTALL);

    Message im;
    if (uuid1 < uuid2)
    {
        get_msg(pu1.tp->get_out(), &im);
        pu1.pc->handle_msg(im, Datagram(), pum1);
        pu2.pc->handle_msg(im, Datagram(), pum1);
    }
    else
    {
        get_msg(pu2.tp->get_out(), &im);
        pu1.pc->handle_msg(im, Datagram(), pum2);
        pu2.pc->handle_msg(im, Datagram(), pum2);
    }

    fail_unless(pu1.pc->get_state() == Proto::S_PRIM);
    fail_unless(pu2.pc->get_state() == Proto::S_PRIM);

}
END_TEST

START_TEST(test_pc_state3)
{
    log_info << "START (test_pc_state3)";
    gu::Config conf;
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf, uuid1);
    DummyTransport tp1;
    PCUser pu1(conf, uuid1, &tp1, &pc1);
    single_boot(&pu1);

    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf, uuid2);
    DummyTransport tp2;
    PCUser pu2(conf, uuid2, &tp2, &pc2);

    // n1: PRIM -> TRANS -> STATES_EXCH -> RTR -> PRIM
    // n2: JOINING -> STATES_EXCH -> RTR -> PRIM
    double_boot(&pu1, &pu2);

    fail_unless(pu1.pc->get_state() == Proto::S_PRIM);
    fail_unless(pu2.pc->get_state() == Proto::S_PRIM);

    // PRIM -> NON_PRIM -> STATES_EXCH -> RTR -> NON_PRIM -> STATES_EXCH -> ...
    //      -> NON_PRIM -> STATES_EXCH -> RTR -> NON_PRIM
    View tr11(ViewId(V_TRANS, pu1.pc->get_current_view().get_id()));
    tr11.add_member(uuid1, "n1");
    pu1.pc->handle_view(tr11);

    View tr12(ViewId(V_TRANS, pu1.pc->get_current_view().get_id()));
    tr12.add_member(uuid2, "n2");
    pu2.pc->handle_view(tr12);

    fail_unless(pu1.pc->get_state() == Proto::S_TRANS);
    fail_unless(pu2.pc->get_state() == Proto::S_TRANS);

    fail_unless(pu1.tp->get_out() == 0);
    fail_unless(pu2.tp->get_out() == 0);

    View reg21(ViewId(V_REG, uuid1,
                      pu1.pc->get_current_view().get_id().get_seq() + 1));
    reg21.add_member(uuid1, "n1");
    pu1.pc->handle_view(reg21);
    fail_unless(pu1.pc->get_state() == Proto::S_STATES_EXCH);

    View reg22(ViewId(V_REG, uuid2,
                      pu2.pc->get_current_view().get_id().get_seq() + 1));
    reg22.add_member(uuid2, "n2");
    pu2.pc->handle_view(reg22);
    fail_unless(pu2.pc->get_state() == Proto::S_STATES_EXCH);


    Message msg;
    get_msg(pu1.tp->get_out(), &msg);
    pu1.pc->handle_msg(msg, Datagram(), pum1);

    get_msg(pu2.tp->get_out(), &msg);
    pu2.pc->handle_msg(msg, Datagram(), pum2);

    fail_unless(pu1.pc->get_state() == Proto::S_NON_PRIM);
    fail_unless(pu2.pc->get_state() == Proto::S_NON_PRIM);



    View tr21(ViewId(V_TRANS, pu1.pc->get_current_view().get_id()));
    tr21.add_member(uuid1, "n1");
    pu1.pc->handle_view(tr21);

    View tr22(ViewId(V_TRANS, pu2.pc->get_current_view().get_id()));
    tr22.add_member(uuid2, "n2");
    pu2.pc->handle_view(tr22);

    fail_unless(pu1.pc->get_state() == Proto::S_TRANS);
    fail_unless(pu2.pc->get_state() == Proto::S_TRANS);

    fail_unless(pu1.tp->get_out() == 0);
    fail_unless(pu2.tp->get_out() == 0);

    View reg3(ViewId(V_REG, uuid1,
                     pu1.pc->get_current_view().get_id().get_seq() + 1));
    reg3.add_member(uuid1, "n1");
    reg3.add_member(uuid2, "n2");

    pu1.pc->handle_view(reg3);
    pu2.pc->handle_view(reg3);

    fail_unless(pu1.pc->get_state() == Proto::S_STATES_EXCH);
    fail_unless(pu2.pc->get_state() == Proto::S_STATES_EXCH);

    get_msg(pu1.tp->get_out(), &msg);
    pu1.pc->handle_msg(msg, Datagram(), pum1);
    pu2.pc->handle_msg(msg, Datagram(), pum1);

    fail_unless(pu1.pc->get_state() == Proto::S_STATES_EXCH);
    fail_unless(pu2.pc->get_state() == Proto::S_STATES_EXCH);

    get_msg(pu2.tp->get_out(), &msg);
    pu1.pc->handle_msg(msg, Datagram(), pum2);
    pu2.pc->handle_msg(msg, Datagram(), pum2);

    fail_unless(pu1.pc->get_state() == Proto::S_INSTALL);
    fail_unless(pu2.pc->get_state() == Proto::S_INSTALL);

    Message im;
    if (uuid1 < uuid2)
    {
        get_msg(pu1.tp->get_out(), &im);
        pu1.pc->handle_msg(im, Datagram(), pum1);
        pu2.pc->handle_msg(im, Datagram(), pum1);
    }
    else
    {
        get_msg(pu2.tp->get_out(), &im);
        pu1.pc->handle_msg(im, Datagram(), pum2);
        pu2.pc->handle_msg(im, Datagram(), pum2);
    }

    fail_unless(pu1.pc->get_state() == Proto::S_PRIM);
    fail_unless(pu2.pc->get_state() == Proto::S_PRIM);

}
END_TEST

START_TEST(test_pc_conflicting_prims)
{
    log_info << "START (test_pc_conflicting_prims)";
    gu::Config conf;
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf, uuid1);
    DummyTransport tp1;
    PCUser pu1(conf, uuid1, &tp1, &pc1);
    single_boot(&pu1);

    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf, uuid2);
    DummyTransport tp2;
    PCUser pu2(conf, uuid2, &tp2, &pc2);
    single_boot(&pu2);

    View tr1(ViewId(V_TRANS, pu1.pc->get_current_view().get_id()));
    tr1.add_member(uuid1);
    pu1.pc->handle_view(tr1);
    View tr2(ViewId(V_TRANS, pu2.pc->get_current_view().get_id()));
    tr2.add_member(uuid2);
    pu2.pc->handle_view(tr2);

    View reg(ViewId(V_REG, uuid1, tr1.get_id().get_seq() + 1));
    reg.add_member(uuid1);
    reg.add_member(uuid2);
    pu1.pc->handle_view(reg);
    pu2.pc->handle_view(reg);

    Message msg1, msg2;

    /* First node must discard msg2 and stay in states exch waiting for
     * trans view */
    get_msg(pu1.tp->get_out(), &msg1);
    get_msg(pu2.tp->get_out(), &msg2);
    fail_unless(pu1.pc->get_state() == Proto::S_STATES_EXCH);

    pu1.pc->handle_msg(msg1, Datagram(), pum1);
    pu1.pc->handle_msg(msg2, Datagram(), pum2);

    /* Second node must abort */
    try
    {
        pu2.pc->handle_msg(msg1, Datagram(), pum1);
        fail("not aborted");
    }
    catch (Exception& e)
    {
        log_info << e.what();
    }

    fail_unless(pu1.tp->get_out() == 0);

    View tr3(ViewId(V_TRANS, reg.get_id()));
    tr3.add_member(uuid1);
    pu1.pc->handle_view(tr3);
    View reg3(ViewId(V_REG, uuid1, tr3.get_id().get_seq() + 1));
    reg3.add_member(uuid1);
    pu1.pc->handle_view(reg3);

    get_msg(pu1.tp->get_out(), &msg1);
    pu1.pc->handle_msg(msg1, Datagram(), pum1);

    get_msg(pu1.tp->get_out(), &msg1);
    pu1.pc->handle_msg(msg1, Datagram(), pum1);

    fail_unless(pu1.pc->get_state() == Proto::S_PRIM);

}
END_TEST

START_TEST(test_pc_conflicting_prims_npvo)
{
    log_info << "START (test_pc_conflicting_npvo)";
    gu::Config conf;
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf, uuid1, URI("pc://?pc.npvo=true"));
    DummyTransport tp1;
    PCUser pu1(conf, uuid1, &tp1, &pc1);
    single_boot(&pu1);

    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf, uuid2, URI("pc://?pc.npvo=true"));
    DummyTransport tp2;
    PCUser pu2(conf, uuid2, &tp2, &pc2);
    single_boot(&pu2);

    View tr1(ViewId(V_TRANS, pu1.pc->get_current_view().get_id()));
    tr1.add_member(uuid1);
    pu1.pc->handle_view(tr1);
    View tr2(ViewId(V_TRANS, pu2.pc->get_current_view().get_id()));
    tr2.add_member(uuid2);
    pu2.pc->handle_view(tr2);

    View reg(ViewId(V_REG, uuid1, tr1.get_id().get_seq() + 1));
    reg.add_member(uuid1);
    reg.add_member(uuid2);
    pu1.pc->handle_view(reg);
    pu2.pc->handle_view(reg);

    Message msg1, msg2;

    /* First node must discard msg2 and stay in states exch waiting for
     * trans view */
    get_msg(pu1.tp->get_out(), &msg1);
    get_msg(pu2.tp->get_out(), &msg2);
    fail_unless(pu1.pc->get_state() == Proto::S_STATES_EXCH);

    pu1.pc->handle_msg(msg1, Datagram(), pum1);
    pu2.pc->handle_msg(msg1, Datagram(), pum1);

    /* First node must abort */
    try
    {
        pu1.pc->handle_msg(msg2, Datagram(), pum2);
        fail("not aborted");
    }
    catch (Exception& e)
    {
        log_info << e.what();
    }

    fail_unless(pu2.tp->get_out() == 0);

    View tr3(ViewId(V_TRANS, reg.get_id()));
    tr3.add_member(uuid2);
    pu2.pc->handle_view(tr3);
    View reg3(ViewId(V_REG, uuid2, tr3.get_id().get_seq() + 1));
    reg3.add_member(uuid2);
    pu2.pc->handle_view(reg3);

    get_msg(pu2.tp->get_out(), &msg2);
    pu2.pc->handle_msg(msg2, Datagram(), pum2);

    get_msg(pu2.tp->get_out(), &msg2);
    pu2.pc->handle_msg(msg2, Datagram(), pum2);

    fail_unless(pu2.pc->get_state() == Proto::S_PRIM);

}
END_TEST


static void join_node(PropagationMatrix* p,
                      DummyNode* n, bool first)
{
    log_info << first;
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
                    size_t seq, ViewType type)
{
    for (size_t i = i_begin; i <= i_end; ++i)
    {
        nvec[i]->set_cvi(ViewId(type,
                                type == V_NON_PRIM ?
                                nvec[0]->get_uuid() :
                                nvec[i_begin]->get_uuid(),
                                static_cast<uint32_t>(type == V_NON_PRIM ? seq - 1 : seq)));
    }
}

static gu::Config gu_conf;

static DummyNode* create_dummy_node(size_t idx,
                                    const string& inactive_timeout = "PT1H",
                                    const string& retrans_period = "PT1H")
{
    const string conf = "evs://?" + Conf::EvsViewForgetTimeout + "=PT1H&"
        + Conf::EvsInactiveCheckPeriod + "=" + to_string(Period(inactive_timeout)/3) + "&"
        + Conf::EvsInactiveTimeout + "=" + inactive_timeout + "&"

        + Conf::EvsKeepalivePeriod + "=" + retrans_period + "&"
        + Conf::EvsJoinRetransPeriod + "=" + retrans_period + "&"
        + Conf::EvsInstallTimeout + "=" + inactive_timeout;;
    list<Protolay*> protos;
    try
    {
        UUID uuid(static_cast<int32_t>(idx));
        protos.push_back(new DummyTransport(uuid, false));
        protos.push_back(new evs::Proto(gu_conf, uuid, conf));
        protos.push_back(new Proto(gu_conf, uuid));
        return new DummyNode(gu_conf, idx, protos);
    }
    catch (...)
    {
        for_each(protos.begin(), protos.end(), DeleteObject());
        throw;
    }
}

static ViewType view_type(const size_t i_begin, const size_t i_end,
                          const size_t n_nodes)
{

    return (((i_end - i_begin + 1)*2 > n_nodes) ? V_PRIM : V_NON_PRIM);
}

START_TEST(test_pc_split_merge)
{
    log_info << "START (test_pc_split_merge)";
    size_t n_nodes(5);
    vector<DummyNode*> dn;
    PropagationMatrix prop;
    const string inactive_timeout("PT0.7S");
    const string retrans_period("PT0.1S");
    uint32_t view_seq = 0;

    for (size_t i = 0; i < n_nodes; ++i)
    {
        dn.push_back(create_dummy_node(i + 1, inactive_timeout, retrans_period));
        gu_trace(join_node(&prop, dn[i], i == 0));
        set_cvi(dn, 0, i, ++view_seq, V_PRIM);
        gu_trace(prop.propagate_until_cvi(false));
    }

    for (size_t i = 1; i < n_nodes; ++i)
    {

        for (size_t j = 0; j < i; ++j)
        {
            for (size_t k = i; k < n_nodes; ++k)
            {
                prop.split(j + 1, k + 1);
            }
        }

        ++view_seq;
        log_info << "split " << i << " view seq " << view_seq;
        set_cvi(dn, 0, i - 1, view_seq, view_type(0, i - 1, n_nodes));
        set_cvi(dn, i, n_nodes - 1, view_seq, view_type(i, n_nodes - 1, n_nodes));
        gu_trace(prop.propagate_until_cvi(true));

        for (size_t j = 0; j < i; ++j)
        {
            for (size_t k = i; k < n_nodes; ++k)
            {
                prop.merge(j + 1, k + 1);
            }
        }
        ++view_seq;
        log_info << "merge " << i << " view seq " << view_seq;
        set_cvi(dn, 0, n_nodes - 1, view_seq, V_PRIM);
        gu_trace(prop.propagate_until_cvi(true));
    }
    check_trace(dn);
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST



START_TEST(test_pc_split_merge_w_user_msg)
{
    log_info << "START (test_pc_split_merge_w_user_msg)";
    size_t n_nodes(5);
    vector<DummyNode*> dn;
    PropagationMatrix prop;
    const string inactive_timeout("PT0.7S");
    const string retrans_period("PT0.1S");
    uint32_t view_seq = 0;

    for (size_t i = 0; i < n_nodes; ++i)
    {
        dn.push_back(create_dummy_node(i + 1, inactive_timeout, retrans_period));
        gu_trace(join_node(&prop, dn[i], i == 0));
        set_cvi(dn, 0, i, ++view_seq, V_PRIM);
        gu_trace(prop.propagate_until_cvi(false));
    }

    for (size_t i = 1; i < n_nodes; ++i)
    {
        for (size_t j = 0; j < n_nodes; ++j)
        {
            send_n(dn[j], ::rand() % 5);
        }
        for (size_t j = 0; j < i; ++j)
        {
            for (size_t k = i; k < n_nodes; ++k)
            {
                prop.split(j + 1, k + 1);
            }
        }
        ++view_seq;
        log_info << "split " << i << " view seq " << view_seq;
        set_cvi(dn, 0, i - 1, view_seq, view_type(0, i - 1, n_nodes));
        set_cvi(dn, i, n_nodes - 1, view_seq, view_type(i, n_nodes - 1, n_nodes));
        gu_trace(prop.propagate_until_cvi(true));

        for (size_t j = 0; j < n_nodes; ++j)
        {
            send_n(dn[j], ::rand() % 5);
        }
        for (size_t j = 0; j < i; ++j)
        {
            for (size_t k = i; k < n_nodes; ++k)
            {
                prop.merge(j + 1, k + 1);
            }
        }
        ++view_seq;
        log_info << "merge " << i << " view seq " << view_seq;
        set_cvi(dn, 0, n_nodes - 1, view_seq, V_PRIM);
        gu_trace(prop.propagate_until_cvi(true));
    }
    check_trace(dn);
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST


START_TEST(test_pc_complete_split_merge)
{
    log_info << "START (test_pc_complete_split_merge)";
    size_t n_nodes(5);
    vector<DummyNode*> dn;
    PropagationMatrix prop;
    const string inactive_timeout("PT0.31S");
    const string retrans_period("PT0.1S");
    uint32_t view_seq = 0;

    for (size_t i = 0; i < n_nodes; ++i)
    {
        dn.push_back(create_dummy_node(i + 1, inactive_timeout, retrans_period));
        log_info << "i " << i;
        gu_trace(join_node(&prop, dn[i], i == 0));
        set_cvi(dn, 0, i, ++view_seq, V_PRIM);
        gu_trace(prop.propagate_until_cvi(false));
    }

    for (size_t i = 0; i < 5; ++i)
    {

        for (size_t j = 0; j < n_nodes; ++j)
        {
            send_n(dn[j], ::rand() % 5);
        }


        prop.propagate_n(9 + ::rand() % 5);

        for (size_t j = 0; j < n_nodes; ++j)
        {
            for (size_t k = 0; k < n_nodes; ++k)
            {
                if (j != k)
                {
                    prop.split(j + 1, k + 1);
                }
            }
        }

        ++view_seq;
        log_info << "split " << i << " view seq " << view_seq;
        set_cvi(dn, 0, n_nodes - 1, view_seq, V_NON_PRIM);
        gu_trace(prop.propagate_until_cvi(true));

        for (size_t j = 0; j < n_nodes; ++j)
        {
            for (size_t k = 0; k < n_nodes; ++k)
            {
                if (j != k)
                {
                    prop.merge(j + 1, k + 1);
                }
            }
        }
        ++view_seq;
        log_info << "merge " << i << " view seq " << view_seq;
        set_cvi(dn, 0, n_nodes - 1, view_seq, V_PRIM);
        gu_trace(prop.propagate_until_cvi(true));
    }
    check_trace(dn);
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST


class PCUser2 : public Toplay
{
    Transport* tp;
    bool sending;
    uint8_t my_type;
    bool send;
    Period send_period;
    Date next_send;
    PCUser2(const PCUser2&);
    void operator=(const PCUser2);
public:
    PCUser2(Protonet& net, const string& uri, const bool send_ = true) :
        Toplay(net.conf()),
        tp(Transport::create(net, uri)),
        sending(false),
        my_type(static_cast<uint8_t>(1 + ::rand()%4)),
        send(send_),
        send_period("PT0.05S"),
        next_send(Date::max())
    { }

    ~PCUser2()
    {
        delete tp;
    }

    void start()
    {
        gcomm::connect(tp, this);
        tp->connect();
        gcomm::disconnect(tp, this);
        tp->get_pstack().push_proto(this);
    }

    void stop()
    {
        sending = false;
        tp->get_pstack().pop_proto(this);
        gcomm::connect(tp, this);
        tp->close();
        gcomm::disconnect(tp, this);
    }

    void handle_up(const void* cid, const Datagram& rb, const ProtoUpMeta& um)
    {

        if (um.has_view())
        {
            const View& view(um.get_view());
            log_info << view;
            if (view.get_type() == V_PRIM && send == true)
            {
                sending = true;
                next_send = Date::now() + send_period;
            }
        }
        else
        {
            // log_debug << "received message: " << um.get_to_seq();
            fail_unless(rb.get_len() - rb.get_offset() == 16);
            if (um.get_source() == tp->get_uuid())
            {
                fail_unless(um.get_user_type() == my_type);
            }
        }
    }

    Protostack& get_pstack() { return tp->get_pstack(); }

    Date handle_timers()
    {
        Date now(Date::now());
        if (now >= next_send)
        {
            byte_t buf[16];
            memset(buf, 0xa, sizeof(buf));
            Datagram dg(Buffer(buf, buf + sizeof(buf)));
            // dg.get_header().resize(128);
            // dg.set_header_offset(128);
            int ret = send_down(dg, ProtoDownMeta(my_type, rand() % 10 == 0 ? O_SAFE : O_LOCAL_CAUSAL));
            if (ret != 0)
            {
                // log_debug << "send down " << ret;
            }
            next_send = next_send + send_period;
        }
        return next_send;
    }

};

START_TEST(test_pc_transport)
{
    log_info << "START (test_pc_transport)";
    gu::Config conf;
    auto_ptr<Protonet> net(Protonet::create(conf));
    PCUser2 pu1(*net,
                "pc://?"
                "evs.info_log_mask=0xff&"
                "gmcast.listen_addr=tcp://127.0.0.1:10001&"
                "gmcast.group=pc&"
                "gmcast.time_wait=PT0.5S&"
                "node.name=n1");
    PCUser2 pu2(*net,
                "pc://127.0.0.1:10001?"
                "evs.info_log_mask=0xff&"
                "gmcast.group=pc&"
                "gmcast.time_wait=PT0.5S&"
                "gmcast.listen_addr=tcp://127.0.0.1:10002&"
                "node.name=n2");
    PCUser2 pu3(*net,
                "pc://127.0.0.1:10001?"
                "evs.info_log_mask=0xff&"
                "gmcast.group=pc&"
                "gmcast.time_wait=PT0.5S&"
                "gmcast.listen_addr=tcp://127.0.0.1:10003&"
                "node.name=n3");

    gu_conf_self_tstamp_on();

    pu1.start();
    net->event_loop(5*Sec);

    pu2.start();
    net->event_loop(5*Sec);

    pu3.start();
    net->event_loop(5*Sec);

    pu3.stop();
    net->event_loop(5*Sec);

    pu2.stop();
    net->event_loop(5*Sec);

    pu1.stop();
    log_info << "cleanup";
    net->event_loop(0);
    log_info << "finished";

}
END_TEST


START_TEST(test_trac_191)
{
    log_info << "START (test_trac_191)";
    gu::Config conf;
    UUID uuid1(1), uuid2(2), uuid3(3), uuid4(4);
    Proto p(conf, uuid4);
    DummyTransport tp(uuid4, true);
    // gcomm::connect(&tp, &p);
    PCUser pu(conf, uuid4, &tp, &p);

    p.shift_to(Proto::S_NON_PRIM);
    View t0(ViewId(V_TRANS, uuid4, 0));
    t0.add_member(uuid4);
    p.handle_view(t0);

    View r5(ViewId(V_REG, uuid2, 5));
    r5.add_member(uuid3);
    r5.add_member(uuid4);

    p.handle_view(r5);

    Datagram* dg = tp.get_out();
    fail_unless(dg != 0);
    Message sm4;
    get_msg(dg, &sm4);
    fail_unless(sm4.get_type() == Message::T_STATE);

    // Handle first sm from uuid3

    StateMessage sm3(0);
    pc::NodeMap& im3(sm3.get_node_map());
    im3.insert_unique(make_pair(uuid1,
                                pc::Node(true, 254, ViewId(V_PRIM, uuid1, 3), 20)));
    im3.insert_unique(make_pair(uuid2,
                                pc::Node(true, 254, ViewId(V_PRIM, uuid1, 3), 20)));
    im3.insert_unique(make_pair(uuid3,
                                pc::Node(false, 254, ViewId(V_PRIM, uuid1, 3), 25)));
    p.handle_msg(sm3, Datagram(), ProtoUpMeta(uuid3));
    p.handle_msg(sm4, Datagram(), ProtoUpMeta(uuid4));
}
END_TEST

START_TEST(test_trac_413)
{
    log_info << "START (test_trac_413)";
    class TN : gcomm::Toplay // test node
    {
    public:
        TN(gu::Config conf, const UUID& uuid)
            :
            Toplay(conf),
            p_(conf, uuid),
            tp_(uuid, true)
        {
            gcomm::connect(&tp_, &p_);
            gcomm::connect(&p_, this);
        }
        const UUID& uuid() const { return p_.get_uuid(); }
        gcomm::pc::Proto& p() { return p_; }
        DummyTransport& tp() { return tp_; }
        void handle_up(const void* id, const gu::Datagram& dg,
                       const gcomm::ProtoUpMeta& um)
        {
            // void
        }
    private:
        pc::Proto p_;
        DummyTransport tp_;
    };

    gu::Config conf;
    TN n1(conf, 1), n2(conf, 2), n3(conf, 3);


    // boot to first prim
    {
        gcomm::View tr(ViewId(V_TRANS, n1.uuid(), 0));
        tr.get_members().insert_unique(std::make_pair(n1.uuid(), ""));
        n1.p().connect(true);
        n1.p().handle_view(tr);
        Datagram* dg(n1.tp().get_out());
        fail_unless(dg == 0 && n1.p().get_state() == gcomm::pc::Proto::S_TRANS);
        gcomm::View reg(ViewId(V_REG, n1.uuid(), 1));
        reg.get_members().insert_unique(std::make_pair(n1.uuid(), ""));
        n1.p().handle_view(reg);
        dg = n1.tp().get_out();
        fail_unless(dg != 0 &&
                    n1.p().get_state() == gcomm::pc::Proto::S_STATES_EXCH);
        n1.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n1.uuid()));
        delete dg;
        dg = n1.tp().get_out();
        fail_unless(dg != 0 &&
                    n1.p().get_state() == gcomm::pc::Proto::S_INSTALL);
        n1.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n1.uuid()));
        delete dg;
        dg = n1.tp().get_out();
        fail_unless(dg == 0 && n1.p().get_state() == gcomm::pc::Proto::S_PRIM);
    }

    // add remaining nodes
    {
        gcomm::View tr(ViewId(V_TRANS, n1.uuid(), 1));
        tr.get_members().insert_unique(std::make_pair(n1.uuid(), ""));
        n1.p().handle_view(tr);
    }
    {
        gcomm::View tr(ViewId(V_TRANS, n2.uuid(), 0));
        tr.get_members().insert_unique(std::make_pair(n2.uuid(), ""));
        n2.p().connect(false);
        n2.p().handle_view(tr);
    }
    {
        gcomm::View tr(ViewId(V_TRANS, n3.uuid(), 0));
        tr.get_members().insert_unique(std::make_pair(n3.uuid(), ""));
        n3.p().connect(false);
        n3.p().handle_view(tr);
    }

    {
        gcomm::View reg(ViewId(V_REG, n1.uuid(), 2));
        reg.get_members().insert_unique(std::make_pair(n1.uuid(), ""));
        reg.get_members().insert_unique(std::make_pair(n2.uuid(), ""));
        reg.get_members().insert_unique(std::make_pair(n3.uuid(), ""));
        n1.p().handle_view(reg);
        n2.p().handle_view(reg);
        n3.p().handle_view(reg);

        gu::Datagram* dg(n1.tp().get_out());
        fail_unless(dg != 0);
        n1.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n1.uuid()));
        n2.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n1.uuid()));
        n3.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n1.uuid()));
        delete dg;

        dg = n2.tp().get_out();
        fail_unless(dg != 0);
        n1.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n2.uuid()));
        n2.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n2.uuid()));
        n3.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n2.uuid()));
        delete dg;

        dg = n3.tp().get_out();
        fail_unless(dg != 0);
        n1.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n3.uuid()));
        n2.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n3.uuid()));
        n3.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n3.uuid()));
        delete dg;

        dg = n1.tp().get_out();
        fail_unless(dg != 0);
        n1.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n1.uuid()));
        n2.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n1.uuid()));
        n3.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n1.uuid()));
        delete dg;

        fail_unless(n1.tp().get_out() == 0 &&
                    n1.p().get_state() == gcomm::pc::Proto::S_PRIM);
        fail_unless(n2.tp().get_out() == 0 &&
                    n2.p().get_state() == gcomm::pc::Proto::S_PRIM);
        fail_unless(n3.tp().get_out() == 0 &&
                    n3.p().get_state() == gcomm::pc::Proto::S_PRIM);
    }

    mark_point();
    // drop n1 from view and deliver only state messages in
    // the following reg view
    {
        gcomm::View tr(gcomm::ViewId(V_TRANS, n1.uuid(), 2));
        tr.get_members().insert_unique(std::make_pair(n2.uuid(), ""));
        tr.get_members().insert_unique(std::make_pair(n3.uuid(), ""));

        n2.p().handle_view(tr);
        n3.p().handle_view(tr);

        gcomm::View reg(gcomm::ViewId(V_REG, n2.uuid(), 3));
        reg.get_members().insert_unique(std::make_pair(n2.uuid(), ""));
        reg.get_members().insert_unique(std::make_pair(n3.uuid(), ""));
        n2.p().handle_view(reg);
        n3.p().handle_view(reg);


        gu::Datagram* dg(n2.tp().get_out());
        n2.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n2.uuid()));
        n3.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n2.uuid()));
        delete dg;

        dg = n3.tp().get_out();
        n2.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n3.uuid()));
        n3.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n3.uuid()));
        delete dg;
    }

    mark_point();
    // drop n2 from view and make sure that n3 ends in non-prim
    {
        gcomm::View tr(gcomm::ViewId(V_TRANS, n2.uuid(), 3));
        tr.get_members().insert_unique(std::make_pair(n3.uuid(), ""));
        n3.p().handle_view(tr);
        fail_unless(n3.tp().get_out() == 0 &&
                    n3.p().get_state() == gcomm::pc::Proto::S_TRANS);

        gcomm::View reg(gcomm::ViewId(V_REG, n3.uuid(), 4));
        reg.get_members().insert_unique(std::make_pair(n3.uuid(), ""));
        n3.p().handle_view(reg);

        fail_unless(n3.p().get_state() == gcomm::pc::Proto::S_STATES_EXCH);

        gu::Datagram* dg(n3.tp().get_out());
        fail_unless(dg != 0);
        n3.p().handle_up(0, *dg, ProtoUpMeta(n3.uuid()));
        dg = n3.tp().get_out();
        fail_unless(dg == 0 &&
                    n3.p().get_state() == gcomm::pc::Proto::S_NON_PRIM,
                    "%p %d", dg, n3.p().get_state());
    }

}
END_TEST


START_TEST(test_fifo_violation)
{
    log_info << "START (test_fifo_violation)";
    gu::Config conf;
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf, uuid1);
    DummyTransport tp1;
    PCUser pu1(conf, uuid1, &tp1, &pc1);
    single_boot(&pu1);

    assert(pc1.get_state() == Proto::S_PRIM);
    pu1.send();
    pu1.send();
    Datagram* dg1(tp1.get_out());
    fail_unless(dg1 != 0);
    Datagram* dg2(tp1.get_out());
    fail_unless(dg2 != 0);

    try
    {
        pc1.handle_up(0, *dg2, ProtoUpMeta(uuid1, ViewId(), 0, 0xff, O_SAFE));
        fail("");
    }
    catch (Exception& e)
    {
        fail_unless(e.get_errno() == ENOTRECOVERABLE);
    }
    delete dg1;
    delete dg2;
}
END_TEST

START_TEST(test_checksum)
{
    log_info << "START (test_checksum)";
    gu::Config conf;
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf, uuid1);
    DummyTransport tp1;
    PCUser pu1(conf, uuid1, &tp1, &pc1);
    single_boot(&pu1);

    assert(pc1.get_state() == Proto::S_PRIM);
    pu1.send();
    Datagram* dg(tp1.get_out());
    fail_unless(dg != 0);
    dg->normalize();
    pc1.handle_up(0, *dg, ProtoUpMeta(uuid1));
    delete dg;

    pu1.send();
    dg = tp1.get_out();
    fail_unless(dg != 0);
    dg->normalize();
    *(&dg->get_payload()[0] + dg->get_payload().size() - 1) ^= 0x10;
    try
    {
        pc1.handle_up(0, *dg, ProtoUpMeta(uuid1));
        fail("");
    }
    catch (Exception& e)
    {
        fail_unless(e.get_errno() == ENOTRECOVERABLE);
    }
    delete dg;
}
END_TEST


START_TEST(test_set_param)
{
    log_info << "START (test_pc_transport)";
    gu::Config conf;
    auto_ptr<Protonet> net(Protonet::create(conf));
    PCUser2 pu1(*net,
                "pc://?"
                "evs.info_log_mask=0xff&"
                "gmcast.listen_addr=tcp://127.0.0.1:10001&"
                "gmcast.group=pc&"
                "gmcast.time_wait=PT0.5S&"
                "node.name=n1");
    pu1.start();
    // no such a parameter
    fail_unless(net->set_param("foo.bar", "1") == false);

    const evs::seqno_t send_window(
        gu::from_string<evs::seqno_t>(conf.get("evs.send_window")));
    const evs::seqno_t user_send_window(
        gu::from_string<evs::seqno_t>(conf.get("evs.user_send_window")));

    try
    {
        net->set_param("evs.send_window", gu::to_string(user_send_window - 1));
        fail("exception not thrown");
    }
    catch (gu::Exception& e)
    {
        fail_unless(e.get_errno() == ERANGE, "%d: %s", e.get_errno(), e.what());
    }

    try
    {
        net->set_param("evs.user_send_window",
                      gu::to_string(send_window + 1));
        fail("exception not thrown");
    }
    catch (gu::Exception& e)
    {
        fail_unless(e.get_errno() == ERANGE, "%d: %s", e.get_errno(), e.what());
    }

    // Note: These checks may have to change if defaults are changed
    fail_unless(net->set_param(
                    "evs.send_window",
                    gu::to_string(send_window - 1)) == true);
    fail_unless(gu::from_string<evs::seqno_t>(conf.get("evs.send_window")) ==
                send_window - 1);
    fail_unless(net->set_param(
                    "evs.user_send_window",
                    gu::to_string(user_send_window + 1)) == true);
    fail_unless(gu::from_string<evs::seqno_t>(
                    conf.get("evs.user_send_window")) == user_send_window + 1);
    pu1.stop();
}
END_TEST


START_TEST(test_trac_599)
{
    class D : public gcomm::Toplay
    {
    public:
        D(gu::Config& conf) : gcomm::Toplay(conf) { }
        void handle_up(const void* id, const gu::Datagram& dg,
                       const gcomm::ProtoUpMeta& um)
        {

        }
    };


    gu::Config conf;
    D d(conf);
    std::auto_ptr<gcomm::Protonet> pnet(gcomm::Protonet::create(conf));
    std::auto_ptr<gcomm::Transport> tp(
        gcomm::Transport::create(*pnet,
                                 "pc://?gmcast.group=test"));
    gcomm::connect(tp.get(), &d);
    gu::Buffer buf(10);
    gu::Datagram dg(buf);
    int err;
    err = tp->send_down(dg, gcomm::ProtoDownMeta());
    fail_unless(err == ENOTCONN, "%d", err);
    tp->connect();
    buf.resize(tp->get_mtu());
    gu::Datagram dg2(buf);
    err = tp->send_down(dg2, gcomm::ProtoDownMeta());
    fail_unless(err == 0, "%d", err);
    buf.resize(buf.size() + 1);
    gu::Datagram dg3(buf);
    err = tp->send_down(dg3, gcomm::ProtoDownMeta());
    fail_unless(err == EMSGSIZE, "%d", err);
    pnet->event_loop(gu::datetime::Sec);
    tp->close();
}
END_TEST

// test for forced teardown
START_TEST(test_trac_620)
{
    gu::Config conf;
    auto_ptr<Protonet> net(Protonet::create(conf));
    Transport* tp(Transport::create(*net, "pc://?"
				    "evs.info_log_mask=0xff&"
				    "gmcast.listen_addr=tcp://127.0.0.1:10001&"
				    "gmcast.group=pc&"
				    "gmcast.time_wait=PT0.5S&"
				    "node.name=n1"));
    class D : public gcomm::Toplay
    {
    public:
        D(gu::Config& conf) : gcomm::Toplay(conf) { }
        void handle_up(const void* id, const gu::Datagram& dg,
                       const gcomm::ProtoUpMeta& um)
        {

        }
    };
    D d(conf);
    gcomm::connect(tp, &d);
    tp->connect();
    tp->close(true);
    gcomm::disconnect(tp, &d);
    delete tp;
}
END_TEST


Suite* pc_suite()
{
    Suite* s = suite_create("gcomm::pc");
    TCase* tc;

    tc = tcase_create("test_pc_messages");
    tcase_add_test(tc, test_pc_messages);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_pc_view_changes_single");
    tcase_add_test(tc, test_pc_view_changes_single);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_pc_view_changes_double");
    tcase_add_test(tc, test_pc_view_changes_double);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_pc_view_changes_reverse");
    tcase_add_test(tc, test_pc_view_changes_reverse);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_pc_state1");
    tcase_add_test(tc, test_pc_state1);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_pc_state2");
    tcase_add_test(tc, test_pc_state2);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_pc_state3");
    tcase_add_test(tc, test_pc_state3);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_pc_conflicting_prims");
    tcase_add_test(tc, test_pc_conflicting_prims);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_pc_conflicting_prims_npvo");
    tcase_add_test(tc, test_pc_conflicting_prims_npvo);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_pc_split_merge");
    tcase_add_test(tc, test_pc_split_merge);
    tcase_set_timeout(tc, 15);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_pc_split_merge_w_user_msg");
    tcase_add_test(tc, test_pc_split_merge_w_user_msg);
    tcase_set_timeout(tc, 15);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_pc_complete_split_merge");
    tcase_add_test(tc, test_pc_complete_split_merge);
    tcase_set_timeout(tc, 25);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_pc_transport");
    tcase_add_test(tc, test_pc_transport);
    tcase_set_timeout(tc, 35);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_trac_191");
    tcase_add_test(tc, test_trac_191);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_trac_413");
    tcase_add_test(tc, test_trac_413);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_fifo_violation");
    tcase_add_test(tc, test_fifo_violation);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_checksum");
    tcase_add_test(tc, test_checksum);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_set_param");
    tcase_add_test(tc, test_set_param);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_trac_599");
    tcase_add_test(tc, test_trac_599);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_trac_620");
    tcase_add_test(tc, test_trac_620);
    suite_add_tcase(s, tc);

    return s;
}
