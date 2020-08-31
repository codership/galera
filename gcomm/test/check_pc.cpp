/*
 * Copyright (C) 2009-2020 Codership Oy <info@codership.com>
 */

#include "check_gcomm.hpp"

#include "pc_message.hpp"
#include "pc_proto.hpp"
#include "evs_proto.hpp"

#include "check_templ.hpp"
#include "check_trace.hpp"
#include "gcomm/conf.hpp"
#include "gu_errno.h"

#include "gu_asio.hpp" // gu::ssl_register_params()

#include <check.h>

#include <list>
#include <cstdlib>
#include <vector>

using namespace std;
using namespace std::rel_ops;
using namespace gu::datetime;
using namespace gcomm;
using namespace gcomm::pc;
using gu::byte_t;
using gu::Buffer;
using gu::Exception;
using gu::URI;
using gu::DeleteObject;

START_TEST(test_pc_messages)
{
    StateMessage pcs(0);
    pc::NodeMap& sim(pcs.node_map());

    sim.insert(std::make_pair(UUID(0,0),
                              pc::Node(true, false, false, 6,
                                       ViewId(V_PRIM,
                                              UUID(0, 0), 9),
                                       42, -1)));
    sim.insert(std::make_pair(UUID(0,0),
                              pc::Node(false, true, false, 88, ViewId(V_PRIM,
                                                         UUID(0, 0), 3),
                                       472, 0)));
    sim.insert(std::make_pair(UUID(0,0),
                              pc::Node(true, false, true, 78, ViewId(V_PRIM,
                                                        UUID(0, 0), 87),
                                       52, 1)));

    size_t expt_size = 4 // hdr
        + 4              // seq
        + 4 + 3*(UUID::serial_size() + sizeof(uint32_t) + 4 + 20 + 8); // NodeMap
    check_serialization(pcs, expt_size, StateMessage(-1));

    InstallMessage pci(0);
    pc::NodeMap& iim = pci.node_map();

    iim.insert(std::make_pair(UUID(0,0),
                              pc::Node(true, true, true, 6, ViewId(V_PRIM,
                                                             UUID(0, 0), 9), 42, -1)));
    iim.insert(std::make_pair(UUID(0,0),
                              pc::Node(false, false, false, 88, ViewId(V_NON_PRIM,
                                                         UUID(0, 0), 3), 472, 0)));
    iim.insert(std::make_pair(UUID(0,0),
                              pc::Node(true, false, false, 78, ViewId(V_PRIM,
                                                        UUID(0, 0), 87), 52, 1)));
    iim.insert(std::make_pair(UUID(0,0),
                              pc::Node(false, true, true, 457, ViewId(V_NON_PRIM,
                                                          UUID(0, 0), 37), 56, 0xff)));

    expt_size = 4 // hdr
        + 4              // seq
        + 4 + 4*(UUID::serial_size() + sizeof(uint32_t) + 4 + 20 + 8); // NodeMap
    check_serialization(pci, expt_size, InstallMessage(-1));

    UserMessage pcu(0, 7);
    pcu.checksum(0xfefe, true);

    expt_size = 4 + 4;
    check_serialization(pcu, expt_size, UserMessage(-1, -1U));
    ck_assert(pcu.serial_size() % 4 == 0);
}
END_TEST

class PCUser : public Toplay
{
public:
    PCUser(gu::Config& conf, const UUID& uuid,
           DummyTransport *tp, Proto* pc) :
        Toplay(conf),
        views_(),
        uuid_(uuid),
        tp_(tp),
        pc_(pc)
    {
        gcomm::connect(tp_, pc_);
        gcomm::connect(pc_, this);
    }

    const UUID& uuid() const { return uuid_; }
    DummyTransport* tp() { return tp_; }
    Proto* pc() { return pc_; }

    void handle_up(const void* cid, const Datagram& rb,
                   const ProtoUpMeta& um)
    {
        if (um.has_view() == true)
        {
            const View& view(um.view());
            log_info << view;
            ck_assert(view.type() == V_PRIM ||
                        view.type() == V_NON_PRIM);
            views_.push_back(View(view));
        }
    }

    void send()
    {
        byte_t pl[4] = {1, 2, 3, 4};
        Buffer buf(pl, pl + sizeof(pl));
        Datagram dg(buf);
        ck_assert(send_down(dg, ProtoDownMeta()) == 0);
    }
private:

    PCUser(const PCUser&);
    void operator=(const PCUser&);

    list<View> views_;
    UUID uuid_;
    DummyTransport* tp_;
    Proto* pc_;
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
        const byte_t* begin(gcomm::begin(*rb));
        const size_t available(gcomm::available(*rb));
        ck_assert(msg->unserialize(begin,
                                     available, 0) != 0);
        log_info << "get_msg: " << msg->to_string();
        if (release)
            delete rb;
    }

}

void single_boot(int version, PCUser* pu1)
{

    ProtoUpMeta sum1(pu1->uuid());

    View vt0(version, ViewId(V_TRANS, pu1->uuid(), 0));
    vt0.add_member(pu1->uuid(), 0);
    ProtoUpMeta um1(UUID::nil(), ViewId(), &vt0);
    pu1->pc()->connect(true);
    // pu1->pc()->shift_to(Proto::S_JOINING);
    pu1->pc()->handle_up(0, Datagram(), um1);
    ck_assert(pu1->pc()->state() == Proto::S_TRANS);

    View vr1(version, ViewId(V_REG, pu1->uuid(), 1));
    vr1.add_member(pu1->uuid(), 0);
    ProtoUpMeta um2(UUID::nil(), ViewId(), &vr1);
    pu1->pc()->handle_up(0, Datagram(), um2);
    ck_assert(pu1->pc()->state() == Proto::S_STATES_EXCH);

    Datagram* rb = pu1->tp()->out();
    ck_assert(rb != 0);
    Message sm1;
    get_msg(rb, &sm1);
    ck_assert(sm1.type() == Message::PC_T_STATE);
    ck_assert(sm1.node_map().size() == 1);
    {
        const pc::Node& pi1 = pc::NodeMap::value(sm1.node_map().begin());
        ck_assert(pi1.prim() == true);
        ck_assert(pi1.last_prim() == ViewId(V_PRIM, pu1->uuid(), 0));
    }
    pu1->pc()->handle_msg(sm1, Datagram(), sum1);
    ck_assert(pu1->pc()->state() == Proto::S_INSTALL);

    rb = pu1->tp()->out();
    ck_assert(rb != 0);
    Message im1;
    get_msg(rb, &im1);
    ck_assert(im1.type() == Message::PC_T_INSTALL);
    ck_assert(im1.node_map().size() == 1);
    {
        const pc::Node& pi1 = pc::NodeMap::value(im1.node_map().begin());
        ck_assert(pi1.prim() == true);
        ck_assert(pi1.last_prim() == ViewId(V_PRIM, pu1->uuid(), 0));
    }
    pu1->pc()->handle_msg(im1, Datagram(), sum1);
    ck_assert(pu1->pc()->state() == Proto::S_PRIM);
}

START_TEST(test_pc_view_changes_single)
{
    log_info << "START (test_pc_view_changes_single)";
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    UUID uuid1(0, 0);
    Proto pc1(conf, uuid1, 0);
    DummyTransport tp1;
    PCUser pu1(conf, uuid1, &tp1, &pc1);
    single_boot(0, &pu1);

}
END_TEST


static void double_boot(int version, PCUser* pu1, PCUser* pu2)
{
    ProtoUpMeta pum1(pu1->uuid());
    ProtoUpMeta pum2(pu2->uuid());

    View t11(version, ViewId(V_TRANS, pu1->pc()->current_view().id()));
    t11.add_member(pu1->uuid(), 0);
    pu1->pc()->handle_view(t11);
    ck_assert(pu1->pc()->state() == Proto::S_TRANS);

    View t12(version, ViewId(V_TRANS, pu2->uuid(), 0));
    t12.add_member(pu2->uuid(), 0);
    // pu2->pc()->shift_to(Proto::S_JOINING);
    pu2->pc()->connect(false);
    pu2->pc()->handle_view(t12);
    ck_assert(pu2->pc()->state() == Proto::S_TRANS);

    View r1(version, ViewId(V_REG,
                   pu1->uuid(),
                   pu1->pc()->current_view().id().seq() + 1));
    r1.add_member(pu1->uuid(), 0);
    r1.add_member(pu2->uuid(), 0);
    pu1->pc()->handle_view(r1);
    ck_assert(pu1->pc()->state() == Proto::S_STATES_EXCH);

    pu2->pc()->handle_view(r1);
    ck_assert(pu2->pc()->state() == Proto::S_STATES_EXCH);

    Datagram* rb = pu1->tp()->out();
    ck_assert(rb != 0);
    Message sm1;
    get_msg(rb, &sm1);
    ck_assert(sm1.type() == Message::PC_T_STATE);

    rb = pu2->tp()->out();
    ck_assert(rb != 0);
    Message sm2;
    get_msg(rb, &sm2);
    ck_assert(sm2.type() == Message::PC_T_STATE);

    rb = pu1->tp()->out();
    ck_assert(rb == 0);
    rb = pu2->tp()->out();
    ck_assert(rb == 0);

    pu1->pc()->handle_msg(sm1, Datagram(), pum1);
    rb = pu1->tp()->out();
    ck_assert(rb == 0);
    ck_assert(pu1->pc()->state() == Proto::S_STATES_EXCH);
    pu1->pc()->handle_msg(sm2, Datagram(), pum2);
    ck_assert(pu1->pc()->state() == Proto::S_INSTALL);

    pu2->pc()->handle_msg(sm1, Datagram(), pum1);
    rb = pu2->tp()->out();
    ck_assert(rb == 0);
    ck_assert(pu2->pc()->state() == Proto::S_STATES_EXCH);
    pu2->pc()->handle_msg(sm2, Datagram(), pum2);
    ck_assert(pu2->pc()->state() == Proto::S_INSTALL);

    Message im1;
    UUID imsrc;
    if (pu1->uuid() < pu2->uuid())
    {
        rb = pu1->tp()->out();
        imsrc = pu1->uuid();
    }
    else
    {
        rb = pu2->tp()->out();
        imsrc = pu2->uuid();
    }

    ck_assert(rb != 0);
    get_msg(rb, &im1);
    ck_assert(im1.type() == Message::PC_T_INSTALL);

    ck_assert(pu1->tp()->out() == 0);
    ck_assert(pu2->tp()->out() == 0);

    ProtoUpMeta ipum(imsrc);
    pu1->pc()->handle_msg(im1, Datagram(), ipum);
    ck_assert(pu1->pc()->state() == Proto::S_PRIM);

    pu2->pc()->handle_msg(im1, Datagram(), ipum);
    ck_assert(pu2->pc()->state() == Proto::S_PRIM);
}

// Form PC for three instances.
static void triple_boot(int version, PCUser* pu1, PCUser* pu2, PCUser* pu3)
{
    ck_assert(pu1->uuid() < pu2->uuid() && pu2->uuid() < pu3->uuid());

    // trans views
    {
        View tr12(version, ViewId(V_TRANS, pu1->pc()->current_view().id()));
        tr12.add_member(pu1->uuid(), 0);
        tr12.add_member(pu2->uuid(), 0);

        ProtoUpMeta trum12(UUID::nil(), ViewId(), &tr12);
        pu1->pc()->handle_up(0, Datagram(), trum12);
        pu2->pc()->handle_up(0, Datagram(), trum12);

        ck_assert(pu1->pc()->state() == Proto::S_TRANS);
        ck_assert(pu2->pc()->state() == Proto::S_TRANS);

        pu3->pc()->connect(false);
        View tr3(version, ViewId(V_TRANS, pu3->uuid(), 0));
        tr3.add_member(pu3->uuid(), 0);
        ProtoUpMeta trum3(UUID::nil(), ViewId(), &tr3);
        pu3->pc()->handle_up(0, Datagram(), trum3);

        ck_assert(pu3->pc()->state() == Proto::S_TRANS);
    }

    // reg view
    {
        View reg(version,
            ViewId(V_REG,
                   pu1->uuid(), pu1->pc()->current_view().id().seq() + 1));
        reg.add_member(pu1->uuid(), 0);
        reg.add_member(pu2->uuid(), 0);
        reg.add_member(pu3->uuid(), 0);

        ProtoUpMeta pum(UUID::nil(), ViewId(), &reg);
        pu1->pc()->handle_up(0, Datagram(), pum);
        pu2->pc()->handle_up(0, Datagram(), pum);
        pu3->pc()->handle_up(0, Datagram(), pum);

        ck_assert(pu1->pc()->state() == Proto::S_STATES_EXCH);
        ck_assert(pu2->pc()->state() == Proto::S_STATES_EXCH);
        ck_assert(pu3->pc()->state() == Proto::S_STATES_EXCH);

    }

    // states exch
    {
        Datagram* dg(pu1->tp()->out());
        ck_assert(dg != 0);
        pu1->pc()->handle_up(0, *dg, ProtoUpMeta(pu1->uuid()));
        pu2->pc()->handle_up(0, *dg, ProtoUpMeta(pu1->uuid()));
        pu3->pc()->handle_up(0, *dg, ProtoUpMeta(pu1->uuid()));
        delete dg;

        dg = pu2->tp()->out();
        ck_assert(dg != 0);
        pu1->pc()->handle_up(0, *dg, ProtoUpMeta(pu2->uuid()));
        pu2->pc()->handle_up(0, *dg, ProtoUpMeta(pu2->uuid()));
        pu3->pc()->handle_up(0, *dg, ProtoUpMeta(pu2->uuid()));
        delete dg;

        dg = pu3->tp()->out();
        ck_assert(dg != 0);
        pu1->pc()->handle_up(0, *dg, ProtoUpMeta(pu3->uuid()));
        pu2->pc()->handle_up(0, *dg, ProtoUpMeta(pu3->uuid()));
        pu3->pc()->handle_up(0, *dg, ProtoUpMeta(pu3->uuid()));
        delete dg;

        ck_assert(pu1->pc()->state() == Proto::S_INSTALL);
        ck_assert(pu2->pc()->state() == Proto::S_INSTALL);
        ck_assert(pu3->pc()->state() == Proto::S_INSTALL);
    }

    // install
    {
        Datagram* dg(pu1->tp()->out());
        ck_assert(dg != 0);
        pu1->pc()->handle_up(0, *dg, ProtoUpMeta(pu1->uuid()));
        pu2->pc()->handle_up(0, *dg, ProtoUpMeta(pu1->uuid()));
        pu3->pc()->handle_up(0, *dg, ProtoUpMeta(pu1->uuid()));
        delete dg;

        ck_assert(pu1->pc()->state() == Proto::S_PRIM);
        ck_assert(pu2->pc()->state() == Proto::S_PRIM);
        ck_assert(pu3->pc()->state() == Proto::S_PRIM);
    }
}


START_TEST(test_pc_view_changes_double)
{
    log_info << "START (test_pc_view_changes_double)";
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf, uuid1, 0);
    DummyTransport tp1;
    PCUser pu1(conf, uuid1, &tp1, &pc1);
    single_boot(0, &pu1);

    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf, uuid2, 0);
    DummyTransport tp2;
    PCUser pu2(conf, uuid2, &tp2, &pc2);

    double_boot(0, &pu1, &pu2);

    Datagram* rb;

    View tnp(0, ViewId(V_TRANS, pu1.pc()->current_view().id()));
    tnp.add_member(uuid1, 0);
    pu1.pc()->handle_view(tnp);
    ck_assert(pu1.pc()->state() == Proto::S_TRANS);
    View reg(0, ViewId(V_REG, uuid1,
                    pu1.pc()->current_view().id().seq() + 1));
    reg.add_member(uuid1, 0);
    pu1.pc()->handle_view(reg);
    ck_assert(pu1.pc()->state() == Proto::S_STATES_EXCH);
    rb = pu1.tp()->out();
    ck_assert(rb != 0);
    pu1.pc()->handle_up(0, *rb, ProtoUpMeta(uuid1));
    ck_assert(pu1.pc()->state() == Proto::S_NON_PRIM);
    delete rb;

    View tpv2(0, ViewId(V_TRANS, pu2.pc()->current_view().id()));
    tpv2.add_member(uuid2, 0);
    tpv2.add_left(uuid1, 0);
    pu2.pc()->handle_view(tpv2);
    ck_assert(pu2.pc()->state() == Proto::S_TRANS);
    ck_assert(pu2.tp()->out() == 0);

    View rp2(0, ViewId(V_REG, uuid2,
                       pu1.pc()->current_view().id().seq() + 1));
    rp2.add_member(uuid2, 0);
    rp2.add_left(uuid1, 0);
    pu2.pc()->handle_view(rp2);
    ck_assert(pu2.pc()->state() == Proto::S_STATES_EXCH);
    rb = pu2.tp()->out();
    ck_assert(rb != 0);
    Message sm2;
    get_msg(rb, &sm2);
    ck_assert(sm2.type() == Message::PC_T_STATE);
    ck_assert(pu2.tp()->out() == 0);
    pu2.pc()->handle_msg(sm2, Datagram(), pum2);
    ck_assert(pu2.pc()->state() == Proto::S_INSTALL);
    rb = pu2.tp()->out();
    ck_assert(rb != 0);
    Message im2;
    get_msg(rb, &im2);
    ck_assert(im2.type() == Message::PC_T_INSTALL);
    pu2.pc()->handle_msg(im2, Datagram(), pum2);
    ck_assert(pu2.pc()->state() == Proto::S_PRIM);

}
END_TEST

/* Test that UUID ordering does not matter when starting nodes */
START_TEST(test_pc_view_changes_reverse)
{
    log_info << "START (test_pc_view_changes_reverse)";
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf, uuid1, 0);
    DummyTransport tp1;
    PCUser pu1(conf, uuid1, &tp1, &pc1);


    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf, uuid2, 0);
    DummyTransport tp2;
    PCUser pu2(conf, uuid2, &tp2, &pc2);

    single_boot(0, &pu2);
    double_boot(0, &pu2, &pu1);
}
END_TEST



START_TEST(test_pc_state1)
{
    log_info << "START (test_pc_state1)";
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf, uuid1, 0);
    DummyTransport tp1;
    PCUser pu1(conf, uuid1, &tp1, &pc1);
    single_boot(0, &pu1);

    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf, uuid2, 0);
    DummyTransport tp2;
    PCUser pu2(conf, uuid2, &tp2, &pc2);

    // n1: PRIM -> TRANS -> STATES_EXCH -> RTR -> PRIM
    // n2: JOINING -> STATES_EXCH -> RTR -> PRIM
    double_boot(0, &pu1, &pu2);

    ck_assert(pu1.pc()->state() == Proto::S_PRIM);
    ck_assert(pu2.pc()->state() == Proto::S_PRIM);

    // PRIM -> TRANS -> STATES_EXCH -> RTR -> TRANS -> STATES_EXCH -> RTR ->PRIM
    View tr1(0, ViewId(V_TRANS, pu1.pc()->current_view().id()));
    tr1.add_member(uuid1, 0);
    tr1.add_member(uuid2, 0);
    pu1.pc()->handle_view(tr1);
    pu2.pc()->handle_view(tr1);

    ck_assert(pu1.pc()->state() == Proto::S_TRANS);
    ck_assert(pu2.pc()->state() == Proto::S_TRANS);

    ck_assert(pu1.tp()->out() == 0);
    ck_assert(pu2.tp()->out() == 0);

    View reg2(0, ViewId(V_REG, uuid1,
                     pu1.pc()->current_view().id().seq() + 1));
    reg2.add_member(uuid1, 0);
    reg2.add_member(uuid2, 0);
    pu1.pc()->handle_view(reg2);
    pu2.pc()->handle_view(reg2);

    ck_assert(pu1.pc()->state() == Proto::S_STATES_EXCH);
    ck_assert(pu2.pc()->state() == Proto::S_STATES_EXCH);

    Message msg;
    get_msg(pu1.tp()->out(), &msg);
    pu1.pc()->handle_msg(msg, Datagram(), pum1);
    pu2.pc()->handle_msg(msg, Datagram(), pum1);

    ck_assert(pu1.pc()->state() == Proto::S_STATES_EXCH);
    ck_assert(pu2.pc()->state() == Proto::S_STATES_EXCH);

    get_msg(pu2.tp()->out(), &msg);
    pu1.pc()->handle_msg(msg, Datagram(), pum2);
    pu2.pc()->handle_msg(msg, Datagram(), pum2);

    ck_assert(pu1.pc()->state() == Proto::S_INSTALL);
    ck_assert(pu2.pc()->state() == Proto::S_INSTALL);

    View tr2(0, ViewId(V_TRANS, pu1.pc()->current_view().id()));
    tr2.add_member(uuid1, 0);
    tr2.add_member(uuid2, 0);

    pu1.pc()->handle_view(tr2);
    pu2.pc()->handle_view(tr2);


    ck_assert(pu1.pc()->state() == Proto::S_TRANS);
    ck_assert(pu2.pc()->state() == Proto::S_TRANS);

    Message im;

    if (uuid1 < uuid2)
    {
        get_msg(pu1.tp()->out(), &im);
        pu1.pc()->handle_msg(im, Datagram(), pum1);
        pu2.pc()->handle_msg(im, Datagram(), pum1);
    }
    else
    {
        get_msg(pu2.tp()->out(), &im);
        pu1.pc()->handle_msg(im, Datagram(), pum2);
        pu2.pc()->handle_msg(im, Datagram(), pum2);
    }


    ck_assert(pu1.pc()->state() == Proto::S_TRANS);
    ck_assert(pu2.pc()->state() == Proto::S_TRANS);


    View reg3(0, ViewId(V_REG, uuid1,
                        pu1.pc()->current_view().id().seq() + 1));

    reg3.add_member(uuid1, 0);
    reg3.add_member(uuid2, 0);

    pu1.pc()->handle_view(reg3);
    pu2.pc()->handle_view(reg3);

    ck_assert(pu1.pc()->state() == Proto::S_STATES_EXCH);
    ck_assert(pu2.pc()->state() == Proto::S_STATES_EXCH);

    get_msg(pu1.tp()->out(), &msg);
    pu1.pc()->handle_msg(msg, Datagram(), pum1);
    pu2.pc()->handle_msg(msg, Datagram(), pum1);

    ck_assert(pu1.pc()->state() == Proto::S_STATES_EXCH);
    ck_assert(pu2.pc()->state() == Proto::S_STATES_EXCH);

    get_msg(pu2.tp()->out(), &msg);
    pu1.pc()->handle_msg(msg, Datagram(), pum2);
    pu2.pc()->handle_msg(msg, Datagram(), pum2);

    ck_assert(pu1.pc()->state() == Proto::S_INSTALL);
    ck_assert(pu2.pc()->state() == Proto::S_INSTALL);

    if (uuid1 < uuid2)
    {
        get_msg(pu1.tp()->out(), &im);
        pu1.pc()->handle_msg(im, Datagram(), pum1);
        pu2.pc()->handle_msg(im, Datagram(), pum1);
    }
    else
    {
        get_msg(pu2.tp()->out(), &im);
        pu1.pc()->handle_msg(im, Datagram(), pum2);
        pu2.pc()->handle_msg(im, Datagram(), pum2);
    }

    ck_assert(pu1.pc()->state() == Proto::S_PRIM);
    ck_assert(pu2.pc()->state() == Proto::S_PRIM);

}
END_TEST

START_TEST(test_pc_state2)
{
    log_info << "START (test_pc_state2)";
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf, uuid1, 0);
    DummyTransport tp1;
    PCUser pu1(conf, uuid1, &tp1, &pc1);
    single_boot(0, &pu1);

    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf, uuid2, 0);
    DummyTransport tp2;
    PCUser pu2(conf, uuid2, &tp2, &pc2);

    // n1: PRIM -> TRANS -> STATES_EXCH -> RTR -> PRIM
    // n2: JOINING -> STATES_EXCH -> RTR -> PRIM
    double_boot(0, &pu1, &pu2);

    ck_assert(pu1.pc()->state() == Proto::S_PRIM);
    ck_assert(pu2.pc()->state() == Proto::S_PRIM);

    // PRIM -> TRANS -> STATES_EXCH -> TRANS -> STATES_EXCH -> RTR -> PRIM
    View tr1(0, ViewId(V_TRANS, pu1.pc()->current_view().id()));
    tr1.add_member(uuid1, 0);
    tr1.add_member(uuid2, 0);
    pu1.pc()->handle_view(tr1);
    pu2.pc()->handle_view(tr1);

    ck_assert(pu1.pc()->state() == Proto::S_TRANS);
    ck_assert(pu2.pc()->state() == Proto::S_TRANS);

    ck_assert(pu1.tp()->out() == 0);
    ck_assert(pu2.tp()->out() == 0);

    View reg2(0, ViewId(V_REG, uuid1,
                        pu1.pc()->current_view().id().seq() + 1));
    reg2.add_member(uuid1, 0);
    reg2.add_member(uuid2, 0);
    pu1.pc()->handle_view(reg2);
    pu2.pc()->handle_view(reg2);

    ck_assert(pu1.pc()->state() == Proto::S_STATES_EXCH);
    ck_assert(pu2.pc()->state() == Proto::S_STATES_EXCH);



    View tr2(0, ViewId(V_TRANS, pu1.pc()->current_view().id()));
    tr2.add_member(uuid1, 0);
    tr2.add_member(uuid2, 0);

    pu1.pc()->handle_view(tr2);
    pu2.pc()->handle_view(tr2);


    ck_assert(pu1.pc()->state() == Proto::S_TRANS);
    ck_assert(pu2.pc()->state() == Proto::S_TRANS);

    Message msg;
    get_msg(pu1.tp()->out(), &msg);
    pu1.pc()->handle_msg(msg, Datagram(), pum1);
    pu2.pc()->handle_msg(msg, Datagram(), pum1);

    ck_assert(pu1.pc()->state() == Proto::S_TRANS);
    ck_assert(pu2.pc()->state() == Proto::S_TRANS);

    get_msg(pu2.tp()->out(), &msg);
    pu1.pc()->handle_msg(msg, Datagram(), pum2);
    pu2.pc()->handle_msg(msg, Datagram(), pum2);

    ck_assert(pu1.pc()->state() == Proto::S_TRANS);
    ck_assert(pu2.pc()->state() == Proto::S_TRANS);


    View reg3(0, ViewId(V_REG, uuid1,
                        pu1.pc()->current_view().id().seq() + 1));

    reg3.add_member(uuid1, 0);
    reg3.add_member(uuid2, 0);

    pu1.pc()->handle_view(reg3);
    pu2.pc()->handle_view(reg3);

    ck_assert(pu1.pc()->state() == Proto::S_STATES_EXCH);
    ck_assert(pu2.pc()->state() == Proto::S_STATES_EXCH);

    get_msg(pu1.tp()->out(), &msg);
    pu1.pc()->handle_msg(msg, Datagram(), pum1);
    pu2.pc()->handle_msg(msg, Datagram(), pum1);

    ck_assert(pu1.pc()->state() == Proto::S_STATES_EXCH);
    ck_assert(pu2.pc()->state() == Proto::S_STATES_EXCH);

    get_msg(pu2.tp()->out(), &msg);
    pu1.pc()->handle_msg(msg, Datagram(), pum2);
    pu2.pc()->handle_msg(msg, Datagram(), pum2);

    ck_assert(pu1.pc()->state() == Proto::S_INSTALL);
    ck_assert(pu2.pc()->state() == Proto::S_INSTALL);

    Message im;
    if (uuid1 < uuid2)
    {
        get_msg(pu1.tp()->out(), &im);
        pu1.pc()->handle_msg(im, Datagram(), pum1);
        pu2.pc()->handle_msg(im, Datagram(), pum1);
    }
    else
    {
        get_msg(pu2.tp()->out(), &im);
        pu1.pc()->handle_msg(im, Datagram(), pum2);
        pu2.pc()->handle_msg(im, Datagram(), pum2);
    }

    ck_assert(pu1.pc()->state() == Proto::S_PRIM);
    ck_assert(pu2.pc()->state() == Proto::S_PRIM);

}
END_TEST

START_TEST(test_pc_state3)
{
    log_info << "START (test_pc_state3)";
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf, uuid1, 0);
    DummyTransport tp1;
    PCUser pu1(conf, uuid1, &tp1, &pc1);
    single_boot(0, &pu1);

    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf, uuid2, 0);
    DummyTransport tp2;
    PCUser pu2(conf, uuid2, &tp2, &pc2);

    // n1: PRIM -> TRANS -> STATES_EXCH -> RTR -> PRIM
    // n2: JOINING -> STATES_EXCH -> RTR -> PRIM
    double_boot(0, &pu1, &pu2);

    ck_assert(pu1.pc()->state() == Proto::S_PRIM);
    ck_assert(pu2.pc()->state() == Proto::S_PRIM);

    // PRIM -> NON_PRIM -> STATES_EXCH -> RTR -> NON_PRIM -> STATES_EXCH -> ...
    //      -> NON_PRIM -> STATES_EXCH -> RTR -> NON_PRIM
    View tr11(0, ViewId(V_TRANS, pu1.pc()->current_view().id()));
    tr11.add_member(uuid1, 0);
    pu1.pc()->handle_view(tr11);

    View tr12(0, ViewId(V_TRANS, pu1.pc()->current_view().id()));
    tr12.add_member(uuid2, 0);
    pu2.pc()->handle_view(tr12);

    ck_assert(pu1.pc()->state() == Proto::S_TRANS);
    ck_assert(pu2.pc()->state() == Proto::S_TRANS);

    ck_assert(pu1.tp()->out() == 0);
    ck_assert(pu2.tp()->out() == 0);

    View reg21(0, ViewId(V_REG, uuid1,
                         pu1.pc()->current_view().id().seq() + 1));
    reg21.add_member(uuid1, 0);
    pu1.pc()->handle_view(reg21);
    ck_assert(pu1.pc()->state() == Proto::S_STATES_EXCH);

    View reg22(0, ViewId(V_REG, uuid2,
                         pu2.pc()->current_view().id().seq() + 1));
    reg22.add_member(uuid2, 0);
    pu2.pc()->handle_view(reg22);
    ck_assert(pu2.pc()->state() == Proto::S_STATES_EXCH);


    Message msg;
    get_msg(pu1.tp()->out(), &msg);
    pu1.pc()->handle_msg(msg, Datagram(), pum1);

    get_msg(pu2.tp()->out(), &msg);
    pu2.pc()->handle_msg(msg, Datagram(), pum2);

    ck_assert(pu1.pc()->state() == Proto::S_NON_PRIM);
    ck_assert(pu2.pc()->state() == Proto::S_NON_PRIM);



    View tr21(0, ViewId(V_TRANS, pu1.pc()->current_view().id()));
    tr21.add_member(uuid1, 0);
    pu1.pc()->handle_view(tr21);

    View tr22(0, ViewId(V_TRANS, pu2.pc()->current_view().id()));
    tr22.add_member(uuid2, 0);
    pu2.pc()->handle_view(tr22);

    ck_assert(pu1.pc()->state() == Proto::S_TRANS);
    ck_assert(pu2.pc()->state() == Proto::S_TRANS);

    ck_assert(pu1.tp()->out() == 0);
    ck_assert(pu2.tp()->out() == 0);

    View reg3(0, ViewId(V_REG, uuid1,
                        pu1.pc()->current_view().id().seq() + 1));
    reg3.add_member(uuid1, 0);
    reg3.add_member(uuid2, 0);

    pu1.pc()->handle_view(reg3);
    pu2.pc()->handle_view(reg3);

    ck_assert(pu1.pc()->state() == Proto::S_STATES_EXCH);
    ck_assert(pu2.pc()->state() == Proto::S_STATES_EXCH);

    get_msg(pu1.tp()->out(), &msg);
    pu1.pc()->handle_msg(msg, Datagram(), pum1);
    pu2.pc()->handle_msg(msg, Datagram(), pum1);

    ck_assert(pu1.pc()->state() == Proto::S_STATES_EXCH);
    ck_assert(pu2.pc()->state() == Proto::S_STATES_EXCH);

    get_msg(pu2.tp()->out(), &msg);
    pu1.pc()->handle_msg(msg, Datagram(), pum2);
    pu2.pc()->handle_msg(msg, Datagram(), pum2);

    ck_assert(pu1.pc()->state() == Proto::S_INSTALL);
    ck_assert(pu2.pc()->state() == Proto::S_INSTALL);

    Message im;
    if (uuid1 < uuid2)
    {
        get_msg(pu1.tp()->out(), &im);
        pu1.pc()->handle_msg(im, Datagram(), pum1);
        pu2.pc()->handle_msg(im, Datagram(), pum1);
    }
    else
    {
        get_msg(pu2.tp()->out(), &im);
        pu1.pc()->handle_msg(im, Datagram(), pum2);
        pu2.pc()->handle_msg(im, Datagram(), pum2);
    }

    ck_assert(pu1.pc()->state() == Proto::S_PRIM);
    ck_assert(pu2.pc()->state() == Proto::S_PRIM);

}
END_TEST

START_TEST(test_pc_conflicting_prims)
{
    log_info << "START (test_pc_conflicting_prims)";
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf, uuid1, 0);
    DummyTransport tp1;
    PCUser pu1(conf, uuid1, &tp1, &pc1);
    single_boot(0, &pu1);

    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf, uuid2, 0);
    DummyTransport tp2;
    PCUser pu2(conf, uuid2, &tp2, &pc2);
    single_boot(0, &pu2);

    View tr1(0, ViewId(V_TRANS, pu1.pc()->current_view().id()));
    tr1.add_member(uuid1, 0);
    pu1.pc()->handle_view(tr1);
    View tr2(0, ViewId(V_TRANS, pu2.pc()->current_view().id()));
    tr2.add_member(uuid2, 0);
    pu2.pc()->handle_view(tr2);

    View reg(0, ViewId(V_REG, uuid1, tr1.id().seq() + 1));
    reg.add_member(uuid1, 0);
    reg.add_member(uuid2, 0);
    pu1.pc()->handle_view(reg);
    pu2.pc()->handle_view(reg);

    Message msg1, msg2;

    /* First node must discard msg2 and stay in states exch waiting for
     * trans view */
    get_msg(pu1.tp()->out(), &msg1);
    get_msg(pu2.tp()->out(), &msg2);
    ck_assert(pu1.pc()->state() == Proto::S_STATES_EXCH);

    pu1.pc()->handle_msg(msg1, Datagram(), pum1);
    pu1.pc()->handle_msg(msg2, Datagram(), pum2);

    /* Second node must abort */
    try
    {
        pu2.pc()->handle_msg(msg1, Datagram(), pum1);
        ck_abort_msg("not aborted");
    }
    catch (Exception& e)
    {
        log_info << e.what();
    }

    ck_assert(pu1.tp()->out() == 0);

    View tr3(0, ViewId(V_TRANS, reg.id()));
    tr3.add_member(uuid1, 0);
    pu1.pc()->handle_view(tr3);
    View reg3(0, ViewId(V_REG, uuid1, tr3.id().seq() + 1));
    reg3.add_member(uuid1, 0);
    pu1.pc()->handle_view(reg3);

    get_msg(pu1.tp()->out(), &msg1);
    pu1.pc()->handle_msg(msg1, Datagram(), pum1);

    get_msg(pu1.tp()->out(), &msg1);
    pu1.pc()->handle_msg(msg1, Datagram(), pum1);

    ck_assert(pu1.pc()->state() == Proto::S_PRIM);

}
END_TEST

START_TEST(test_pc_conflicting_prims_npvo)
{
    log_info << "START (test_pc_conflicting_npvo)";
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf, uuid1, 0, URI("pc://?pc.npvo=true"));
    DummyTransport tp1;
    PCUser pu1(conf, uuid1, &tp1, &pc1);
    single_boot(0, &pu1);

    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf, uuid2, 0, URI("pc://?pc.npvo=true"));
    DummyTransport tp2;
    PCUser pu2(conf, uuid2, &tp2, &pc2);
    single_boot(0, &pu2);

    View tr1(0, ViewId(V_TRANS, pu1.pc()->current_view().id()));
    tr1.add_member(uuid1, 0);
    pu1.pc()->handle_view(tr1);
    View tr2(0, ViewId(V_TRANS, pu2.pc()->current_view().id()));
    tr2.add_member(uuid2, 0);
    pu2.pc()->handle_view(tr2);

    View reg(0, ViewId(V_REG, uuid1, tr1.id().seq() + 1));
    reg.add_member(uuid1, 0);
    reg.add_member(uuid2, 0);
    pu1.pc()->handle_view(reg);
    pu2.pc()->handle_view(reg);

    Message msg1, msg2;

    /* First node must discard msg2 and stay in states exch waiting for
     * trans view */
    get_msg(pu1.tp()->out(), &msg1);
    get_msg(pu2.tp()->out(), &msg2);
    ck_assert(pu1.pc()->state() == Proto::S_STATES_EXCH);

    pu1.pc()->handle_msg(msg1, Datagram(), pum1);
    pu2.pc()->handle_msg(msg1, Datagram(), pum1);

    /* First node must abort */
    try
    {
        pu1.pc()->handle_msg(msg2, Datagram(), pum2);
        ck_abort_msg("not aborted");
    }
    catch (Exception& e)
    {
        log_info << e.what();
    }

    ck_assert(pu2.tp()->out() == 0);

    View tr3(0, ViewId(V_TRANS, reg.id()));
    tr3.add_member(uuid2, 0);
    pu2.pc()->handle_view(tr3);
    View reg3(0, ViewId(V_REG, uuid2, tr3.id().seq() + 1));
    reg3.add_member(uuid2, 0);
    pu2.pc()->handle_view(reg3);

    get_msg(pu2.tp()->out(), &msg2);
    pu2.pc()->handle_msg(msg2, Datagram(), pum2);

    get_msg(pu2.tp()->out(), &msg2);
    pu2.pc()->handle_msg(msg2, Datagram(), pum2);

    ck_assert(pu2.pc()->state() == Proto::S_PRIM);

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
                                nvec[0]->uuid() :
                                nvec[i_begin]->uuid(),
                                static_cast<uint32_t>(type == V_NON_PRIM ? seq - 1 : seq)));
    }
}

struct InitGuConf
{
    explicit InitGuConf(gu::Config& conf) { gcomm::Conf::register_params(conf); }
};

static gu::Config&
static_gu_conf()
{
    static gu::Config conf;
    static InitGuConf init(conf);

    return conf;
}

static DummyNode* create_dummy_node(size_t idx,
                                    int version,
                                    const string& suspect_timeout = "PT1H",
                                    const string& inactive_timeout = "PT1H",
                                    const string& retrans_period = "PT20M",
                                    int weight = 1)
{
    gu::Config& gu_conf(static_gu_conf());
    gu::ssl_register_params(gu_conf);
    gcomm::Conf::register_params(gu_conf);
    const string conf = "evs://?" + Conf::EvsViewForgetTimeout + "=PT1H&"
        + Conf::EvsInactiveCheckPeriod + "=" + to_string(Period(suspect_timeout)/3) + "&"
        + Conf::EvsSuspectTimeout + "=" + suspect_timeout + "&"
        + Conf::EvsInactiveTimeout + "=" + inactive_timeout + "&"
        + Conf::EvsKeepalivePeriod + "=" + retrans_period + "&"
        + Conf::EvsJoinRetransPeriod + "=" + retrans_period + "&"
        + Conf::EvsInstallTimeout + "=" + inactive_timeout + "&"
        + Conf::PcWeight + "=" + gu::to_string(weight) + "&"
        + Conf::EvsVersion + "=" + gu::to_string<int>(version) + "&"
        + Conf::EvsInfoLogMask + "=" + "0x3";
    list<Protolay*> protos;

    UUID uuid(static_cast<int32_t>(idx));
    protos.push_back(new DummyTransport(uuid, false));
    protos.push_back(new evs::Proto(gu_conf, uuid, 0, conf));
    protos.push_back(new Proto(gu_conf, uuid, 0, conf));
    return new DummyNode(gu_conf, idx, protos);
}

namespace
{
    gcomm::pc::Proto* pc_from_dummy(DummyNode* dn)
    {
        return reinterpret_cast<Proto*>(dn->protos().back());
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
    const string suspect_timeout("PT0.35S");
    const string inactive_timeout("PT0.7S");
    const string retrans_period("PT0.1S");
    uint32_t view_seq = 0;

    mark_point();

    for (size_t i = 0; i < n_nodes; ++i)
    {
        dn.push_back(create_dummy_node(i + 1, 0, suspect_timeout,
                                       inactive_timeout, retrans_period));
        gu_trace(join_node(&prop, dn[i], i == 0));
        set_cvi(dn, 0, i, ++view_seq, V_PRIM);
        gu_trace(prop.propagate_until_cvi(false));
    }

    mark_point();

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
        set_cvi(dn, i, n_nodes - 1, view_seq, view_type(i,n_nodes - 1,n_nodes));
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

    mark_point();

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
    const string suspect_timeout("PT0.35S");
    const string inactive_timeout("PT0.7S");
    const string retrans_period("PT0.1S");
    uint32_t view_seq = 0;

    for (size_t i = 0; i < n_nodes; ++i)
    {
        dn.push_back(create_dummy_node(i + 1, 0, suspect_timeout,
                                       inactive_timeout, retrans_period));
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
    const string suspect_timeout("PT0.35S");
    const string inactive_timeout("PT0.31S");
    const string retrans_period("PT0.1S");
    uint32_t view_seq = 0;

    for (size_t i = 0; i < n_nodes; ++i)
    {
        dn.push_back(create_dummy_node(i + 1, 0, suspect_timeout,
                                       inactive_timeout, retrans_period));
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


START_TEST(test_pc_protocol_upgrade)
{
    log_info << "START (test_pc_protocol_upgrade)";
    vector<DummyNode*> dn;
    PropagationMatrix prop;
    uint32_t view_seq(0);

    for (int i(0); i <= GCOMM_PROTOCOL_MAX_VERSION; ++i)
    {
        dn.push_back(create_dummy_node(i + 1, i));
        gu_trace(join_node(&prop, dn[i], i == 0));
        set_cvi(dn, 0, i, view_seq, V_PRIM);
        gu_trace(prop.propagate_until_cvi(false));
        ++view_seq;
        for (int j(0); j <= i; ++j)
        {
            ck_assert(pc_from_dummy(dn[j])->current_view().version() == 0);
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
        dn[i]->set_cvi(V_NON_PRIM);
        set_cvi(dn, i + 1, GCOMM_PROTOCOL_MAX_VERSION, view_seq, V_PRIM);
        gu_trace(prop.propagate_until_cvi(true));
        ++view_seq;
        for (int j(i + 1); j <= GCOMM_PROTOCOL_MAX_VERSION; ++j)
        {
            gu_trace(send_n(dn[j], 5 + ::rand() % 4));
        }
        gu_trace(prop.propagate_until_empty());
    }
    ck_assert(pc_from_dummy(dn[GCOMM_PROTOCOL_MAX_VERSION])->current_view().version() == GCOMM_PROTOCOL_MAX_VERSION);
    check_trace(dn);
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST



START_TEST(test_trac_191)
{
    log_info << "START (test_trac_191)";
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    UUID uuid1(1), uuid2(2), uuid3(3), uuid4(4);
    Proto p(conf, uuid4, 0);
    DummyTransport tp(uuid4, true);
    // gcomm::connect(&tp, &p);
    PCUser pu(conf, uuid4, &tp, &p);

    p.shift_to(Proto::S_NON_PRIM);
    View t0(0, ViewId(V_TRANS, uuid4, 0));
    t0.add_member(uuid4, 0);
    p.handle_view(t0);

    View r5(0, ViewId(V_REG, uuid2, 5));
    r5.add_member(uuid3, 0);
    r5.add_member(uuid4, 0);

    p.handle_view(r5);

    Datagram* dg = tp.out();
    ck_assert(dg != 0);
    Message sm4;
    get_msg(dg, &sm4);
    ck_assert(sm4.type() == Message::PC_T_STATE);

    // Handle first sm from uuid3

    StateMessage sm3(0);
    pc::NodeMap& im3(sm3.node_map());
    im3.insert_unique(make_pair(uuid1,
                                pc::Node(true, false, false, 254, ViewId(V_PRIM, uuid1, 3), 20)));
    im3.insert_unique(make_pair(uuid2,
                                pc::Node(true, false, false, 254, ViewId(V_PRIM, uuid1, 3), 20)));
    im3.insert_unique(make_pair(uuid3,
                                pc::Node(false, false, false, 254, ViewId(V_PRIM, uuid1, 3), 25)));
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
            p_(conf, uuid, 0),
            tp_(uuid, true)
        {
            gcomm::connect(&tp_, &p_);
            gcomm::connect(&p_, this);
        }
        const UUID& uuid() const { return p_.uuid(); }
        gcomm::pc::Proto& p() { return p_; }
        DummyTransport& tp() { return tp_; }
        void handle_up(const void* id, const Datagram& dg,
                       const gcomm::ProtoUpMeta& um)
        {
            // void
        }
    private:
        pc::Proto p_;
        DummyTransport tp_;
    };

    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);

    TN n1(conf, 1), n2(conf, 2), n3(conf, 3);


    // boot to first prim
    {
        gcomm::View tr(0, ViewId(V_TRANS, n1.uuid(), 0));
        tr.members().insert_unique(std::make_pair(n1.uuid(), 0));
        n1.p().connect(true);
        n1.p().handle_view(tr);
        Datagram* dg(n1.tp().out());
        ck_assert(dg == 0 && n1.p().state() == gcomm::pc::Proto::S_TRANS);
        gcomm::View reg(0, ViewId(V_REG, n1.uuid(), 1));
        reg.members().insert_unique(std::make_pair(n1.uuid(), 0));
        n1.p().handle_view(reg);
        dg = n1.tp().out();
        ck_assert(dg != 0 &&
                    n1.p().state() == gcomm::pc::Proto::S_STATES_EXCH);
        n1.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n1.uuid()));
        delete dg;
        dg = n1.tp().out();
        ck_assert(dg != 0 &&
                    n1.p().state() == gcomm::pc::Proto::S_INSTALL);
        n1.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n1.uuid()));
        delete dg;
        dg = n1.tp().out();
        ck_assert(dg == 0 && n1.p().state() == gcomm::pc::Proto::S_PRIM);
    }

    // add remaining nodes
    {
        gcomm::View tr(0, ViewId(V_TRANS, n1.uuid(), 1));
        tr.members().insert_unique(std::make_pair(n1.uuid(), 0));
        n1.p().handle_view(tr);
    }
    {
        gcomm::View tr(0, ViewId(V_TRANS, n2.uuid(), 0));
        tr.members().insert_unique(std::make_pair(n2.uuid(), 0));
        n2.p().connect(false);
        n2.p().handle_view(tr);
    }
    {
        gcomm::View tr(0, ViewId(V_TRANS, n3.uuid(), 0));
        tr.members().insert_unique(std::make_pair(n3.uuid(), 0));
        n3.p().connect(false);
        n3.p().handle_view(tr);
    }

    {
        gcomm::View reg(0, ViewId(V_REG, n1.uuid(), 2));
        reg.members().insert_unique(std::make_pair(n1.uuid(), 0));
        reg.members().insert_unique(std::make_pair(n2.uuid(), 0));
        reg.members().insert_unique(std::make_pair(n3.uuid(), 0));
        n1.p().handle_view(reg);
        n2.p().handle_view(reg);
        n3.p().handle_view(reg);

        Datagram* dg(n1.tp().out());
        ck_assert(dg != 0);
        n1.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n1.uuid()));
        n2.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n1.uuid()));
        n3.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n1.uuid()));
        delete dg;

        dg = n2.tp().out();
        ck_assert(dg != 0);
        n1.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n2.uuid()));
        n2.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n2.uuid()));
        n3.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n2.uuid()));
        delete dg;

        dg = n3.tp().out();
        ck_assert(dg != 0);
        n1.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n3.uuid()));
        n2.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n3.uuid()));
        n3.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n3.uuid()));
        delete dg;

        dg = n1.tp().out();
        ck_assert(dg != 0);
        n1.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n1.uuid()));
        n2.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n1.uuid()));
        n3.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n1.uuid()));
        delete dg;

        ck_assert(n1.tp().out() == 0 &&
                    n1.p().state() == gcomm::pc::Proto::S_PRIM);
        ck_assert(n2.tp().out() == 0 &&
                    n2.p().state() == gcomm::pc::Proto::S_PRIM);
        ck_assert(n3.tp().out() == 0 &&
                    n3.p().state() == gcomm::pc::Proto::S_PRIM);
    }

    mark_point();
    // drop n1 from view and deliver only state messages in
    // the following reg view
    {
        gcomm::View tr(0, gcomm::ViewId(V_TRANS, n1.uuid(), 2));
        tr.members().insert_unique(std::make_pair(n2.uuid(), 0));
        tr.members().insert_unique(std::make_pair(n3.uuid(), 0));

        n2.p().handle_view(tr);
        n3.p().handle_view(tr);

        gcomm::View reg(0, gcomm::ViewId(V_REG, n2.uuid(), 3));
        reg.members().insert_unique(std::make_pair(n2.uuid(), 0));
        reg.members().insert_unique(std::make_pair(n3.uuid(), 0));
        n2.p().handle_view(reg);
        n3.p().handle_view(reg);


        Datagram* dg(n2.tp().out());
        n2.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n2.uuid()));
        n3.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n2.uuid()));
        delete dg;

        dg = n3.tp().out();
        n2.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n3.uuid()));
        n3.p().handle_up(0, *dg, gcomm::ProtoUpMeta(n3.uuid()));
        delete dg;
        // Clean up n2 out queue
        dg = n2.tp().out();
        delete dg;
    }

    mark_point();
    // drop n2 from view and make sure that n3 ends in non-prim
    {
        gcomm::View tr(0, gcomm::ViewId(V_TRANS, n2.uuid(), 3));
        tr.members().insert_unique(std::make_pair(n3.uuid(), 0));
        n3.p().handle_view(tr);
        ck_assert(n3.tp().out() == 0 &&
                    n3.p().state() == gcomm::pc::Proto::S_TRANS);

        gcomm::View reg(0, gcomm::ViewId(V_REG, n3.uuid(), 4));
        reg.members().insert_unique(std::make_pair(n3.uuid(), 0));
        n3.p().handle_view(reg);

        ck_assert(n3.p().state() == gcomm::pc::Proto::S_STATES_EXCH);

        Datagram* dg(n3.tp().out());
        ck_assert(dg != 0);
        n3.p().handle_up(0, *dg, ProtoUpMeta(n3.uuid()));
        delete dg;
        dg = n3.tp().out();
        ck_assert_msg(dg == 0 &&
                      n3.p().state() == gcomm::pc::Proto::S_NON_PRIM,
                      "%p %d", dg, n3.p().state());
    }

}
END_TEST


START_TEST(test_fifo_violation)
{
    log_info << "START (test_fifo_violation)";
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf, uuid1, 0);
    DummyTransport tp1;
    PCUser pu1(conf, uuid1, &tp1, &pc1);
    single_boot(0, &pu1);

    assert(pc1.state() == Proto::S_PRIM);
    pu1.send();
    pu1.send();
    Datagram* dg1(tp1.out());
    ck_assert(dg1 != 0);
    Datagram* dg2(tp1.out());
    ck_assert(dg2 != 0);

    try
    {
        pc1.handle_up(0, *dg2, ProtoUpMeta(uuid1, ViewId(), 0, 0xff, O_SAFE));
        ck_abort_msg("Exception not thrown");
    }
    catch (Exception& e)
    {
        ck_assert(e.get_errno() == ENOTRECOVERABLE);
    }
    delete dg1;
    delete dg2;
}
END_TEST

START_TEST(test_checksum)
{
    log_info << "START (test_checksum)";
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    conf.set(Conf::PcChecksum, gu::to_string(true));
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf, uuid1, 0);
    DummyTransport tp1;
    PCUser pu1(conf, uuid1, &tp1, &pc1);
    single_boot(0, &pu1);

    assert(pc1.state() == Proto::S_PRIM);
    pu1.send();
    Datagram* dg(tp1.out());
    ck_assert(dg != 0);
    dg->normalize();
    pc1.handle_up(0, *dg, ProtoUpMeta(uuid1));
    delete dg;

    pu1.send();
    dg = tp1.out();
    ck_assert(dg != 0);
    dg->normalize();
    *(&dg->payload()[0] + dg->payload().size() - 1) ^= 0x10;
    try
    {
        pc1.handle_up(0, *dg, ProtoUpMeta(uuid1));
        ck_abort_msg("Exception not thrown");
    }
    catch (Exception& e)
    {
        ck_assert(e.get_errno() == ENOTRECOVERABLE);
    }
    delete dg;
}
END_TEST





START_TEST(test_trac_277)
{
    log_info << "START (test_trac_277)";
    size_t n_nodes(3);
    vector<DummyNode*> dn;
    PropagationMatrix prop;
    const string suspect_timeout("PT0.35S");
    const string inactive_timeout("PT0.7S");
    const string retrans_period("PT0.1S");
    uint32_t view_seq = 0;

    for (size_t i = 0; i < n_nodes; ++i)
    {
        dn.push_back(create_dummy_node(i + 1, 0, suspect_timeout,
                                       inactive_timeout, retrans_period));
        gu_trace(join_node(&prop, dn[i], i == 0));
        set_cvi(dn, 0, i, ++view_seq, V_PRIM);
        gu_trace(prop.propagate_until_cvi(false));
    }

    log_info << "generate messages";
    send_n(dn[0], 1);
    send_n(dn[1], 1);
    send_n(dn[2], 1);
    gu_trace(prop.propagate_until_empty());

    log_info << "isolate 3";
    prop.split(1, 3);
    prop.split(2, 3);
    ++view_seq;
    set_cvi(dn, 0, 1, view_seq, V_PRIM);
    set_cvi(dn, 2, 2, view_seq, V_NON_PRIM);
    gu_trace(prop.propagate_until_cvi(true));

    log_info << "isolate 1 and 2";
    ++view_seq;
    prop.split(1, 2);
    set_cvi(dn, 0, 1, view_seq, V_NON_PRIM);
    gu_trace(prop.propagate_until_cvi(true));

    log_info << "merge 1 and 2";
    ++view_seq;
    prop.merge(1, 2);
    set_cvi(dn, 0, 1, view_seq, V_PRIM);
    gu_trace(prop.propagate_until_cvi(true));



    log_info << "merge 3";
    ++view_seq;
    prop.merge(1, 3);
    prop.merge(2, 3);
    set_cvi(dn, 0, 2, view_seq, V_PRIM);
    gu_trace(prop.propagate_until_cvi(true));

    check_trace(dn);
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST


// This test checks the case when another node of two node cluster
// crashes or becomes completely isolated and prim view of cluster
// is established by starting third instance directly in prim mode.
START_TEST(test_trac_622_638)
{
    log_info << "START (test_trac_622_638)";
    vector<DummyNode*> dn;
    PropagationMatrix prop;
    const string suspect_timeout("PT0.35S");
    const string inactive_timeout("PT0.7S");
    const string retrans_period("PT0.1S");
    uint32_t view_seq = 0;

    // Create two node cluster and make it split. First node is
    // considered crashed after split (stay isolated in non-prim).
    dn.push_back(create_dummy_node(1, 0, suspect_timeout,
                                   inactive_timeout, retrans_period));
    gu_trace(join_node(&prop, dn[0], true));
    set_cvi(dn, 0, 0, ++view_seq, V_PRIM);
    gu_trace(prop.propagate_until_cvi(false));

    dn.push_back(create_dummy_node(2, 0, suspect_timeout,
                                   inactive_timeout, retrans_period));
    gu_trace(join_node(&prop, dn[1], false));
    set_cvi(dn, 0, 1, ++view_seq, V_PRIM);
    gu_trace(prop.propagate_until_cvi(false));

    log_info << "generate messages";
    send_n(dn[0], 1);
    send_n(dn[1], 1);
    gu_trace(prop.propagate_until_empty());

    log_info << "isolate 1 and 2";
    prop.split(1, 2);
    ++view_seq;
    set_cvi(dn, 0, 0, view_seq, V_NON_PRIM);
    set_cvi(dn, 1, 1, view_seq, V_NON_PRIM);
    gu_trace(prop.propagate_until_cvi(true));

    // Add third node which will be connected with node 2. This will
    // be started with prim status.
    dn.push_back(create_dummy_node(3, 0, suspect_timeout,
                                   inactive_timeout, retrans_period));
    gu_trace(join_node(&prop, dn[2], true));
    prop.split(1, 3); // avoid 1 <-> 3 communication
    ++view_seq;
    set_cvi(dn, 1, 2, view_seq, V_PRIM);
    gu_trace(prop.propagate_until_cvi(false));

    check_trace(dn);
    for_each(dn.begin(), dn.end(), DeleteObject());
}
END_TEST

START_TEST(test_weighted_quorum)
{
    log_info << "START (test_weighted_quorum)";
    size_t n_nodes(3);
    vector<DummyNode*> dn;
    PropagationMatrix prop;
    const string suspect_timeout("PT0.35S");
    const string inactive_timeout("PT0.7S");
    const string retrans_period("PT0.1S");
    uint32_t view_seq = 0;

    for (size_t i = 0; i < n_nodes; ++i)
    {
        dn.push_back(create_dummy_node(i + 1, 0, suspect_timeout,
                                       inactive_timeout,
                                       retrans_period, i));
        gu_trace(join_node(&prop, dn[i], i == 0));
        set_cvi(dn, 0, i, ++view_seq, V_PRIM);
        gu_trace(prop.propagate_until_cvi(false));
    }

    for (size_t i(0); i < n_nodes; ++i)
    {
        int weight(pc_from_dummy(dn[i])->cluster_weight());
        ck_assert_msg(weight == 3,
                      "index: %zu weight: %d", i, weight);
    }
    // split node 3 (weight 2) out, node 3 should remain in prim while
    // nodes 1 and 2 (weights 0 + 1 = 1) should end up in non-prim
    prop.split(1, 3);
    prop.split(2, 3);
    ++view_seq;
    set_cvi(dn, 0, 1, view_seq, V_NON_PRIM);
    set_cvi(dn, 2, 2, view_seq, V_PRIM);
    gu_trace(prop.propagate_until_cvi(true));

    ck_assert(pc_from_dummy(dn[0])->cluster_weight() == 0);
    ck_assert(pc_from_dummy(dn[1])->cluster_weight() == 0);
    ck_assert(pc_from_dummy(dn[2])->cluster_weight() == 2);
    std::for_each(dn.begin(), dn.end(), gu::DeleteObject());
}
END_TEST


//
// The scenario is the following (before fix):
//
// - Two nodes 2 and 3 started with weights 1
// - Third node 1 with weight 3 is brought in the cluster
//   (becomes representative)
// - Partitioning to (1) and (2, 3) happens so that INSTALL message is
//   delivered on 2 and 3 in TRANS and on 1 in REG
// - Node 1 forms PC
// - Nodes 2 and 3 renegotiate and form PC too because node 1 was not present
//   in the previous PC
//
// What should happen is that nodes 2 and 3 recompute quorum on handling
// install message and shift to non-PC
//
START_TEST(test_weighted_partitioning_1)
{
    log_info << "START (test_weighted_partitioning_1)";
    gu::Config conf3;
    gu::ssl_register_params(conf3);
    gcomm::Conf::register_params(conf3);
    conf3.set("pc.weight", "1");
    UUID uuid3(3);
    ProtoUpMeta pum3(uuid3);
    Proto pc3(conf3, uuid3, 0);
    DummyTransport tp3;
    PCUser pu3(conf3, uuid3, &tp3, &pc3);
    single_boot(0, &pu3);

    gu::Config conf2;
    gu::ssl_register_params(conf2);
    gcomm::Conf::register_params(conf2);
    conf2.set("pc.weight", "1");
    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf2, uuid2, 0);
    DummyTransport tp2;
    PCUser pu2(conf2, uuid2, &tp2, &pc2);

    double_boot(0, &pu3, &pu2);

    gu::Config conf1;
    gu::ssl_register_params(conf1);
    gcomm::Conf::register_params(conf1);
    conf1.set("pc.weight", "3");
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf1, uuid1, 0);
    DummyTransport tp1;
    PCUser pu1(conf1, uuid1, &tp1, &pc1);

    // trans views
    {
        View tr1(0, ViewId(V_TRANS, uuid1, 0));
        tr1.add_member(uuid1, 0);
        pu1.pc()->connect(false);
        ProtoUpMeta um1(UUID::nil(), ViewId(), &tr1);
        pu1.pc()->handle_up(0, Datagram(), um1);

        View tr23(0, ViewId(V_TRANS, pu2.pc()->current_view().id()));
        tr23.add_member(uuid2, 0);
        tr23.add_member(uuid3, 0);
        ProtoUpMeta um23(UUID::nil(), ViewId(), &tr23);
        pu2.pc()->handle_up(0, Datagram(), um23);
        pu3.pc()->handle_up(0, Datagram(), um23);
    }


    // reg view
    {
        View reg(0,
                 ViewId(V_REG, uuid1, pu2.pc()->current_view().id().seq() + 1));
        reg.add_member(uuid1, 0);
        reg.add_member(uuid2, 0);
        reg.add_member(uuid3, 0);
        ProtoUpMeta um(UUID::nil(), ViewId(), &reg);
        pu1.pc()->handle_up(0, Datagram(), um);
        pu2.pc()->handle_up(0, Datagram(), um);
        pu3.pc()->handle_up(0, Datagram(), um);
    }

    // states exch
    {
        Datagram* dg(pu1.tp()->out());
        ck_assert(dg != 0);
        pu1.pc()->handle_up(0, *dg, ProtoUpMeta(uuid1));
        pu2.pc()->handle_up(0, *dg, ProtoUpMeta(uuid1));
        pu3.pc()->handle_up(0, *dg, ProtoUpMeta(uuid1));
        delete dg;

        dg = pu2.tp()->out();
        ck_assert(dg != 0);
        pu1.pc()->handle_up(0, *dg, ProtoUpMeta(uuid2));
        pu2.pc()->handle_up(0, *dg, ProtoUpMeta(uuid2));
        pu3.pc()->handle_up(0, *dg, ProtoUpMeta(uuid2));
        delete dg;

        dg = pu3.tp()->out();
        ck_assert(dg != 0);
        pu1.pc()->handle_up(0, *dg, ProtoUpMeta(uuid3));
        pu2.pc()->handle_up(0, *dg, ProtoUpMeta(uuid3));
        pu3.pc()->handle_up(0, *dg, ProtoUpMeta(uuid3));
        delete dg;

        ck_assert(pu2.tp()->out() == 0);
        ck_assert(pu3.tp()->out() == 0);
    }

    // install msg
    {
        Datagram* dg(pu1.tp()->out());
        ck_assert(dg != 0);

        pu1.pc()->handle_up(0, *dg, ProtoUpMeta(uuid1));
        ck_assert(pu1.pc()->state() == Proto::S_PRIM);

        // trans view for 2 and 3
        View tr23(0, ViewId(V_TRANS, pu2.pc()->current_view().id()));
        tr23.add_member(uuid2, 0);
        tr23.add_member(uuid3, 0);
        tr23.add_partitioned(uuid1, 0);

        ProtoUpMeta trum23(UUID::nil(), ViewId(), &tr23);
        pu2.pc()->handle_up(0, Datagram(), trum23);
        pu3.pc()->handle_up(0, Datagram(), trum23);

        // 2 and 3 handle install
        pu2.pc()->handle_up(0, *dg, ProtoUpMeta(uuid1));
        pu3.pc()->handle_up(0, *dg, ProtoUpMeta(uuid1));
        delete dg;

        // reg view for 2 and 3
        View reg23(0, ViewId(V_REG, uuid2, pu2.pc()->current_view().id().seq() + 1));
        reg23.add_member(uuid2, 0);
        reg23.add_member(uuid3, 0);
        ProtoUpMeta rum23(UUID::nil(), ViewId(), &reg23);
        pu2.pc()->handle_up(0, Datagram(), rum23);
        pu3.pc()->handle_up(0, Datagram(), rum23);

        // states exch

        dg = pu2.tp()->out();
        ck_assert(dg != 0);
        pu2.pc()->handle_up(0, *dg, ProtoUpMeta(uuid2));
        pu3.pc()->handle_up(0, *dg, ProtoUpMeta(uuid2));
        delete dg;

        dg = pu3.tp()->out();
        ck_assert(dg != 0);
        pu2.pc()->handle_up(0, *dg, ProtoUpMeta(uuid3));
        pu3.pc()->handle_up(0, *dg, ProtoUpMeta(uuid3));
        delete dg;

        // 2 and 3 should end up in non prim
        ck_assert_msg(pu2.pc()->state() == Proto::S_NON_PRIM,
                      "state: %s", Proto::to_string(pu2.pc()->state()).c_str());
        ck_assert_msg(pu3.pc()->state() == Proto::S_NON_PRIM,
                      "state: %s", Proto::to_string(pu3.pc()->state()).c_str());
    }


}
END_TEST

//
// - Two nodes 2 and 3 started with weights 1
// - Third node 1 with weight 3 is brought in the cluster
//   (becomes representative)
// - Partitioning to (1) and (2, 3) happens so that INSTALL message is
//   delivered in trans view on all nodes
// - All nodes should end up in non-prim, nodes 2 and 3 because they don't know
//   if node 1 ended up in prim (see test_weighted_partitioning_1 above),
//   node 1 because it hasn't been in primary before and fails to deliver
//   install message in reg view
//
START_TEST(test_weighted_partitioning_2)
{
    log_info << "START (test_weighted_partitioning_2)";
    gu::Config conf3;
    gu::ssl_register_params(conf3);
    gcomm::Conf::register_params(conf3);
    conf3.set("pc.weight", "1");
    UUID uuid3(3);
    ProtoUpMeta pum3(uuid3);
    Proto pc3(conf3, uuid3, 0);
    DummyTransport tp3;
    PCUser pu3(conf3, uuid3, &tp3, &pc3);
    single_boot(0, &pu3);

    gu::Config conf2;
    gu::ssl_register_params(conf2);
    gcomm::Conf::register_params(conf2);
    conf2.set("pc.weight", "1");
    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf2, uuid2, 0);
    DummyTransport tp2;
    PCUser pu2(conf2, uuid2, &tp2, &pc2);

    double_boot(0, &pu3, &pu2);

    gu::Config conf1;
    gu::ssl_register_params(conf1);
    gcomm::Conf::register_params(conf1);
    conf1.set("pc.weight", "3");
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf1, uuid1, 0);
    DummyTransport tp1;
    PCUser pu1(conf1, uuid1, &tp1, &pc1);

    // trans views
    {
        View tr1(0, ViewId(V_TRANS, uuid1, 0));
        tr1.add_member(uuid1, 0);
        pu1.pc()->connect(false);
        ProtoUpMeta um1(UUID::nil(), ViewId(), &tr1);
        pu1.pc()->handle_up(0, Datagram(), um1);

        View tr23(0, ViewId(V_TRANS, pu2.pc()->current_view().id()));
        tr23.add_member(uuid2, 0);
        tr23.add_member(uuid3, 0);
        ProtoUpMeta um23(UUID::nil(), ViewId(), &tr23);
        pu2.pc()->handle_up(0, Datagram(), um23);
        pu3.pc()->handle_up(0, Datagram(), um23);
    }


    // reg view
    {
        View reg(0,
                 ViewId(V_REG, uuid1, pu2.pc()->current_view().id().seq() + 1));
        reg.add_member(uuid1, 0);
        reg.add_member(uuid2, 0);
        reg.add_member(uuid3, 0);
        ProtoUpMeta um(UUID::nil(), ViewId(), &reg);
        pu1.pc()->handle_up(0, Datagram(), um);
        pu2.pc()->handle_up(0, Datagram(), um);
        pu3.pc()->handle_up(0, Datagram(), um);
    }

    // states exch
    {
        Datagram* dg(pu1.tp()->out());
        ck_assert(dg != 0);
        pu1.pc()->handle_up(0, *dg, ProtoUpMeta(uuid1));
        pu2.pc()->handle_up(0, *dg, ProtoUpMeta(uuid1));
        pu3.pc()->handle_up(0, *dg, ProtoUpMeta(uuid1));
        delete dg;

        dg = pu2.tp()->out();
        ck_assert(dg != 0);
        pu1.pc()->handle_up(0, *dg, ProtoUpMeta(uuid2));
        pu2.pc()->handle_up(0, *dg, ProtoUpMeta(uuid2));
        pu3.pc()->handle_up(0, *dg, ProtoUpMeta(uuid2));
        delete dg;

        dg = pu3.tp()->out();
        ck_assert(dg != 0);
        pu1.pc()->handle_up(0, *dg, ProtoUpMeta(uuid3));
        pu2.pc()->handle_up(0, *dg, ProtoUpMeta(uuid3));
        pu3.pc()->handle_up(0, *dg, ProtoUpMeta(uuid3));
        delete dg;

        ck_assert(pu2.tp()->out() == 0);
        ck_assert(pu3.tp()->out() == 0);
    }

    // install msg
    {
        Datagram* dg(pu1.tp()->out());
        ck_assert(dg != 0);

        // trans view for 1
        View tr1(0, ViewId(V_TRANS, pu1.pc()->current_view().id()));
        tr1.add_member(uuid1, 0);
        tr1.add_partitioned(uuid2, 0);
        tr1.add_partitioned(uuid3, 0);
        ProtoUpMeta trum1(UUID::nil(), ViewId(), &tr1);
        pu1.pc()->handle_up(0, Datagram(), trum1);
        ck_assert(pu1.pc()->state() == Proto::S_TRANS);

        // 1 handle install
        pu1.pc()->handle_up(0, *dg, ProtoUpMeta(uuid1));
        ck_assert(pu1.pc()->state() == Proto::S_TRANS);


        // trans view for 2 and 3
        View tr23(0, ViewId(V_TRANS, pu2.pc()->current_view().id()));
        tr23.add_member(uuid2, 0);
        tr23.add_member(uuid3, 0);
        tr23.add_partitioned(uuid1, 0);
        ProtoUpMeta trum23(UUID::nil(), ViewId(), &tr23);
        pu2.pc()->handle_up(0, Datagram(), trum23);
        pu3.pc()->handle_up(0, Datagram(), trum23);
        ck_assert(pu2.pc()->state() == Proto::S_TRANS);
        ck_assert(pu3.pc()->state() == Proto::S_TRANS);

        // 2 and 3 handle install
        pu2.pc()->handle_up(0, *dg, ProtoUpMeta(uuid1));
        pu3.pc()->handle_up(0, *dg, ProtoUpMeta(uuid1));
        ck_assert(pu2.pc()->state() == Proto::S_TRANS);
        ck_assert(pu3.pc()->state() == Proto::S_TRANS);

        delete dg;

        // reg view for 1
        View reg1(0, ViewId(V_REG, uuid1, pu1.pc()->current_view().id().seq() + 1));
        reg1.add_member(uuid1, 0);
        ProtoUpMeta rum1(UUID::nil(), ViewId(), &reg1);
        pu1.pc()->handle_up(0, Datagram(), rum1);
        ck_assert(pu1.pc()->state() == Proto::S_STATES_EXCH);

        // reg view for 2 and 3
        View reg23(0, ViewId(V_REG, uuid2, pu2.pc()->current_view().id().seq() + 1));
        reg23.add_member(uuid2, 0);
        reg23.add_member(uuid3, 0);
        ProtoUpMeta rum23(UUID::nil(), ViewId(), &reg23);
        pu2.pc()->handle_up(0, Datagram(), rum23);
        pu3.pc()->handle_up(0, Datagram(), rum23);
        ck_assert(pu2.pc()->state() == Proto::S_STATES_EXCH);
        ck_assert(pu3.pc()->state() == Proto::S_STATES_EXCH);


        // states exch

        dg = pu1.tp()->out();
        ck_assert(dg != 0);
        pu1.pc()->handle_up(0, *dg, ProtoUpMeta(uuid1));
        ck_assert_msg(pu1.pc()->state() == Proto::S_NON_PRIM,
                      "state: %s", Proto::to_string(pu1.pc()->state()).c_str());
        delete dg;

        dg = pu2.tp()->out();
        ck_assert(dg != 0);
        pu2.pc()->handle_up(0, *dg, ProtoUpMeta(uuid2));
        pu3.pc()->handle_up(0, *dg, ProtoUpMeta(uuid2));
        delete dg;

        dg = pu3.tp()->out();
        ck_assert(dg != 0);
        pu2.pc()->handle_up(0, *dg, ProtoUpMeta(uuid3));
        pu3.pc()->handle_up(0, *dg, ProtoUpMeta(uuid3));
        delete dg;


        ck_assert_msg(pu2.pc()->state() == Proto::S_NON_PRIM,
                      "state: %s", Proto::to_string(pu2.pc()->state()).c_str());
        ck_assert_msg(pu3.pc()->state() == Proto::S_NON_PRIM,
                      "state: %s", Proto::to_string(pu3.pc()->state()).c_str());
    }


}
END_TEST


//
// - Nodes 1-3 started with equal weights
// - Weight for node 1 is changed to 3
// - Group splits to (1), (2, 3)
// - Weigh changing message is delivered in reg view in (1) and in
//   trans in (2, 3)
// - Expected outcome: 1 stays in prim, 2 and 3 end up in non-prim
//
START_TEST(test_weight_change_partitioning_1)
{
    log_info << "START (test_weight_change_partitioning_1)";
    gu::Config conf1;
    gu::ssl_register_params(conf1);
    gcomm::Conf::register_params(conf1);
    conf1.set("pc.weight", "1");
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf1, uuid1, 0);
    DummyTransport tp1;
    PCUser pu1(conf1, uuid1, &tp1, &pc1);
    single_boot(0, &pu1);

    gu::Config conf2;
    gu::ssl_register_params(conf2);
    gcomm::Conf::register_params(conf2);
    conf2.set("pc.weight", "1");
    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf2, uuid2, 0);
    DummyTransport tp2;
    PCUser pu2(conf2, uuid2, &tp2, &pc2);

    double_boot(0, &pu1, &pu2);

    gu::Config conf3;
    gu::ssl_register_params(conf3);
    gcomm::Conf::register_params(conf3);
    conf3.set("pc.weight", "1");
    UUID uuid3(3);
    ProtoUpMeta pum3(uuid3);
    Proto pc3(conf3, uuid3, 0);
    DummyTransport tp3;
    PCUser pu3(conf3, uuid3, &tp3, &pc3);

    triple_boot(0, &pu1, &pu2, &pu3);

    // weight change
    {
        Protolay::sync_param_cb_t sync_param_cb;
        pu1.pc()->set_param("pc.weight", "3", sync_param_cb);
        ck_assert(sync_param_cb.empty() == false);
        Datagram* install_dg(pu1.tp()->out());
        ck_assert(install_dg != 0);

        // node 1 handle weight change install, proceed to singleton prim
        pu1.pc()->handle_up(0, *install_dg, ProtoUpMeta(pu1.uuid()));

        View tr1(0, ViewId(V_TRANS, pu1.pc()->current_view().id()));
        tr1.add_member(pu1.uuid(), 0);
        tr1.add_partitioned(pu2.uuid(), 0);
        tr1.add_partitioned(pu3.uuid(), 0);

        pu1.pc()->handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr1));
        ck_assert(pu1.pc()->state() == Proto::S_TRANS);

        View reg1(0, ViewId(V_REG, pu1.uuid(),
                            pu1.pc()->current_view().id().seq() + 1));
        reg1.add_member(pu1.uuid(), 0);
        pu1.pc()->handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg1));
        ck_assert(pu1.pc()->state() == Proto::S_STATES_EXCH);

        Datagram* dg(pu1.tp()->out());
        ck_assert(dg != 0);
        pu1.pc()->handle_up(0, *dg, ProtoUpMeta(pu1.uuid()));
        delete dg;
        ck_assert(pu1.pc()->state() == Proto::S_INSTALL);

        dg = pu1.tp()->out();
        ck_assert(dg != 0);
        pu1.pc()->handle_up(0, *dg, ProtoUpMeta(pu1.uuid()));
        delete dg;
        ck_assert(pu1.pc()->state() == Proto::S_PRIM);

        // nodes 2 and 3 go to trans, handle install
        View tr23(0, ViewId(V_TRANS, pu2.pc()->current_view().id()));
        tr23.add_member(pu2.uuid(), 0);
        tr23.add_member(pu3.uuid(), 0);
        tr23.add_partitioned(pu1.uuid(), 0);

        pu2.pc()->handle_up(0, Datagram(),
                            ProtoUpMeta(UUID::nil(), ViewId(), &tr23));
        pu3.pc()->handle_up(0, Datagram(),
                            ProtoUpMeta(UUID::nil(), ViewId(), &tr23));
        ck_assert(pu2.pc()->state() == Proto::S_TRANS);
        ck_assert(pu3.pc()->state() == Proto::S_TRANS);

        pu2.pc()->handle_up(0, *install_dg, ProtoUpMeta(pu1.uuid()));
        pu3.pc()->handle_up(0, *install_dg, ProtoUpMeta(pu1.uuid()));

        View reg23(0, ViewId(V_REG, pu2.uuid(),
                             pu2.pc()->current_view().id().seq() + 1));
        reg23.add_member(pu2.uuid(), 0);
        reg23.add_member(pu3.uuid(), 0);

        pu2.pc()->handle_up(0, Datagram(),
                            ProtoUpMeta(UUID::nil(), ViewId(), &reg23));
        pu3.pc()->handle_up(0, Datagram(),
                            ProtoUpMeta(UUID::nil(), ViewId(), &reg23));
        ck_assert(pu2.pc()->state() == Proto::S_STATES_EXCH);
        ck_assert(pu3.pc()->state() == Proto::S_STATES_EXCH);

        dg = pu2.tp()->out();
        ck_assert(dg != 0);
        pu2.pc()->handle_up(0, *dg, ProtoUpMeta(pu2.uuid()));
        pu3.pc()->handle_up(0, *dg, ProtoUpMeta(pu2.uuid()));
        delete dg;

        dg = pu3.tp()->out();
        ck_assert(dg != 0);
        pu2.pc()->handle_up(0, *dg, ProtoUpMeta(pu3.uuid()));
        pu3.pc()->handle_up(0, *dg, ProtoUpMeta(pu3.uuid()));
        delete dg;

        ck_assert(pu2.pc()->state() == Proto::S_NON_PRIM);
        ck_assert(pu3.pc()->state() == Proto::S_NON_PRIM);

        delete install_dg;
    }

}
END_TEST


//
// - Nodes 2 and 3 start with weight 1, node 1 with weight 3
// - Weight for node 1 is changed to 1
// - Group splits to (1), (2, 3)
// - Weigh changing message is delivered in reg view in (1) and in
//   trans in (2, 3)
// - Expected outcome: all nodes go non-prim
//
START_TEST(test_weight_change_partitioning_2)
{
    log_info << "START (test_weight_change_partitioning_2)";
    gu::Config conf1;
    gu::ssl_register_params(conf1);
    gcomm::Conf::register_params(conf1);
    conf1.set("pc.weight", "3");
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf1, uuid1, 0);
    DummyTransport tp1;
    PCUser pu1(conf1, uuid1, &tp1, &pc1);
    single_boot(0, &pu1);

    gu::Config conf2;
    gu::ssl_register_params(conf2);
    gcomm::Conf::register_params(conf2);
    conf2.set("pc.weight", "1");
    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf2, uuid2, 0);
    DummyTransport tp2;
    PCUser pu2(conf2, uuid2, &tp2, &pc2);

    double_boot(0, &pu1, &pu2);

    gu::Config conf3;
    gu::ssl_register_params(conf3);
    gcomm::Conf::register_params(conf3);
    conf3.set("pc.weight", "1");
    UUID uuid3(3);
    ProtoUpMeta pum3(uuid3);
    Proto pc3(conf3, uuid3, 0);
    DummyTransport tp3;
    PCUser pu3(conf3, uuid3, &tp3, &pc3);

    triple_boot(0, &pu1, &pu2, &pu3);

    // weight change
    {   
        Protolay::sync_param_cb_t sync_param_cb;
        pu1.pc()->set_param("pc.weight", "1", sync_param_cb);
        ck_assert(sync_param_cb.empty() == false);
        Datagram* install_dg(pu1.tp()->out());
        ck_assert(install_dg != 0);

        // node 1 handle weight change install, proceed to singleton prim
        pu1.pc()->handle_up(0, *install_dg, ProtoUpMeta(pu1.uuid()));

        View tr1(0, ViewId(V_TRANS, pu1.pc()->current_view().id()));
        tr1.add_member(pu1.uuid(), 0);
        tr1.add_partitioned(pu2.uuid(), 0);
        tr1.add_partitioned(pu3.uuid(), 0);

        pu1.pc()->handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr1));
        ck_assert(pu1.pc()->state() == Proto::S_TRANS);

        View reg1(0, ViewId(V_REG, pu1.uuid(),
                            pu1.pc()->current_view().id().seq() + 1));
        reg1.add_member(pu1.uuid(), 0);
        pu1.pc()->handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg1));
        ck_assert(pu1.pc()->state() == Proto::S_STATES_EXCH);

        Datagram* dg(pu1.tp()->out());
        ck_assert(dg != 0);
        pu1.pc()->handle_up(0, *dg, ProtoUpMeta(pu1.uuid()));
        delete dg;
        ck_assert(pu1.pc()->state() == Proto::S_NON_PRIM);

        // nodes 2 and 3 go to trans, handle install
        View tr23(0, ViewId(V_TRANS, pu2.pc()->current_view().id()));
        tr23.add_member(pu2.uuid(), 0);
        tr23.add_member(pu3.uuid(), 0);
        tr23.add_partitioned(pu1.uuid(), 0);

        pu2.pc()->handle_up(0, Datagram(),
                            ProtoUpMeta(UUID::nil(), ViewId(), &tr23));
        pu3.pc()->handle_up(0, Datagram(),
                            ProtoUpMeta(UUID::nil(), ViewId(), &tr23));
        ck_assert(pu2.pc()->state() == Proto::S_TRANS);
        ck_assert(pu3.pc()->state() == Proto::S_TRANS);

        pu2.pc()->handle_up(0, *install_dg, ProtoUpMeta(pu1.uuid()));
        pu3.pc()->handle_up(0, *install_dg, ProtoUpMeta(pu1.uuid()));

        View reg23(0, ViewId(V_REG, pu2.uuid(),
                             pu2.pc()->current_view().id().seq() + 1));
        reg23.add_member(pu2.uuid(), 0);
        reg23.add_member(pu3.uuid(), 0);

        pu2.pc()->handle_up(0, Datagram(),
                            ProtoUpMeta(UUID::nil(), ViewId(), &reg23));
        pu3.pc()->handle_up(0, Datagram(),
                            ProtoUpMeta(UUID::nil(), ViewId(), &reg23));
        ck_assert(pu2.pc()->state() == Proto::S_STATES_EXCH);
        ck_assert(pu3.pc()->state() == Proto::S_STATES_EXCH);

        dg = pu2.tp()->out();
        ck_assert(dg != 0);
        pu2.pc()->handle_up(0, *dg, ProtoUpMeta(pu2.uuid()));
        pu3.pc()->handle_up(0, *dg, ProtoUpMeta(pu2.uuid()));
        delete dg;

        dg = pu3.tp()->out();
        ck_assert(dg != 0);
        pu2.pc()->handle_up(0, *dg, ProtoUpMeta(pu3.uuid()));
        pu3.pc()->handle_up(0, *dg, ProtoUpMeta(pu3.uuid()));
        delete dg;

        ck_assert(pu2.pc()->state() == Proto::S_NON_PRIM);
        ck_assert(pu3.pc()->state() == Proto::S_NON_PRIM);

        delete install_dg;
    }

}
END_TEST


//
// Weight changing message is delivered in transitional view when new node is
// joining. All nodes should end up in prim.
//
START_TEST(test_weight_change_joining)
{
    log_info << "START (test_weight_change_joining)";
    gu::Config conf1;
    gu::ssl_register_params(conf1);
    gcomm::Conf::register_params(conf1);
    conf1.set("pc.weight", "1");
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf1, uuid1, 0);
    DummyTransport tp1;
    PCUser pu1(conf1, uuid1, &tp1, &pc1);
    single_boot(0, &pu1);

    gu::Config conf2;
    gu::ssl_register_params(conf2);
    gcomm::Conf::register_params(conf2);
    conf2.set("pc.weight", "1");
    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf2, uuid2, 0);
    DummyTransport tp2;
    PCUser pu2(conf2, uuid2, &tp2, &pc2);

    double_boot(0, &pu1, &pu2);

    gu::Config conf3;
    gu::ssl_register_params(conf3);
    gcomm::Conf::register_params(conf3);
    conf3.set("pc.weight", "1");
    UUID uuid3(3);
    ProtoUpMeta pum3(uuid3);
    Proto pc3(conf3, uuid3, 0);
    DummyTransport tp3;
    PCUser pu3(conf3, uuid3, &tp3, &pc3);

    // weight change
    {
        Protolay::sync_param_cb_t sync_param_cb;
        pu1.pc()->set_param("pc.weight", "1", sync_param_cb);
        ck_assert(sync_param_cb.empty() == false);
        Datagram* install_dg(pu1.tp()->out());
        ck_assert(install_dg != 0);

        // trans views
        {
            View tr12(0, ViewId(V_TRANS, pu1.pc()->current_view().id()));
            tr12.add_member(pu1.uuid(), 0);
            tr12.add_member(pu2.uuid(), 0);

            ProtoUpMeta trum12(UUID::nil(), ViewId(), &tr12);
            pu1.pc()->handle_up(0, Datagram(), trum12);
            pu2.pc()->handle_up(0, Datagram(), trum12);

            ck_assert(pu1.pc()->state() == Proto::S_TRANS);
            ck_assert(pu2.pc()->state() == Proto::S_TRANS);

            // deliver weight change install in trans view
            pu1.pc()->handle_up(0, *install_dg, ProtoUpMeta(pu1.uuid()));
            pu2.pc()->handle_up(0, *install_dg, ProtoUpMeta(pu1.uuid()));

            pu3.pc()->connect(false);
            View tr3(0, ViewId(V_TRANS, pu3.uuid(), 0));
            tr3.add_member(pu3.uuid(), 0);
            ProtoUpMeta trum3(UUID::nil(), ViewId(), &tr3);
            pu3.pc()->handle_up(0, Datagram(), trum3);

            ck_assert(pu3.pc()->state() == Proto::S_TRANS);
        }

        // reg view
        {
            View reg(0,
                     ViewId(V_REG,
                            pu1.uuid(), pu1.pc()->current_view().id().seq() + 1));
            reg.add_member(pu1.uuid(), 0);
            reg.add_member(pu2.uuid(), 0);
            reg.add_member(pu3.uuid(), 0);

            ProtoUpMeta pum(UUID::nil(), ViewId(), &reg);
            pu1.pc()->handle_up(0, Datagram(), pum);
            pu2.pc()->handle_up(0, Datagram(), pum);
            pu3.pc()->handle_up(0, Datagram(), pum);

            ck_assert(pu1.pc()->state() == Proto::S_STATES_EXCH);
            ck_assert(pu2.pc()->state() == Proto::S_STATES_EXCH);
            ck_assert(pu3.pc()->state() == Proto::S_STATES_EXCH);

        }

        // states exch
        {
            Datagram* dg(pu1.tp()->out());
            ck_assert(dg != 0);
            pu1.pc()->handle_up(0, *dg, ProtoUpMeta(pu1.uuid()));
            pu2.pc()->handle_up(0, *dg, ProtoUpMeta(pu1.uuid()));
            pu3.pc()->handle_up(0, *dg, ProtoUpMeta(pu1.uuid()));
            delete dg;

            dg = pu2.tp()->out();
            ck_assert(dg != 0);
            pu1.pc()->handle_up(0, *dg, ProtoUpMeta(pu2.uuid()));
            pu2.pc()->handle_up(0, *dg, ProtoUpMeta(pu2.uuid()));
            pu3.pc()->handle_up(0, *dg, ProtoUpMeta(pu2.uuid()));
            delete dg;

            dg = pu3.tp()->out();
            ck_assert(dg != 0);
            pu1.pc()->handle_up(0, *dg, ProtoUpMeta(pu3.uuid()));
            pu2.pc()->handle_up(0, *dg, ProtoUpMeta(pu3.uuid()));
            pu3.pc()->handle_up(0, *dg, ProtoUpMeta(pu3.uuid()));
            delete dg;

            ck_assert(pu1.pc()->state() == Proto::S_INSTALL);
            ck_assert(pu2.pc()->state() == Proto::S_INSTALL);
            ck_assert(pu3.pc()->state() == Proto::S_INSTALL);
        }

        // install
        {
            Datagram* dg(pu1.tp()->out());
            ck_assert(dg != 0);
            pu1.pc()->handle_up(0, *dg, ProtoUpMeta(pu1.uuid()));
            pu2.pc()->handle_up(0, *dg, ProtoUpMeta(pu1.uuid()));
            pu3.pc()->handle_up(0, *dg, ProtoUpMeta(pu1.uuid()));
            delete dg;

            ck_assert(pu1.pc()->state() == Proto::S_PRIM);
            ck_assert(pu2.pc()->state() == Proto::S_PRIM);
            ck_assert(pu3.pc()->state() == Proto::S_PRIM);
        }
        delete install_dg;
    }
}
END_TEST


//
// One of the nodes leaves gracefully from group and weight change message
// is delivered in trans view. Remaining nodes must not enter non-prim.
//
START_TEST(test_weight_change_leaving)
{
    log_info << "START (test_weight_change_leaving)";
    gu::Config conf1;
    gu::ssl_register_params(conf1);
    gcomm::Conf::register_params(conf1);
    conf1.set("pc.weight", "3");
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf1, uuid1, 0);
    DummyTransport tp1;
    PCUser pu1(conf1, uuid1, &tp1, &pc1);
    single_boot(0, &pu1);

    gu::Config conf2;
    gu::ssl_register_params(conf2);
    gcomm::Conf::register_params(conf2);
    conf2.set("pc.weight", "2");
    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf2, uuid2, 0);
    DummyTransport tp2;
    PCUser pu2(conf2, uuid2, &tp2, &pc2);

    double_boot(0, &pu1, &pu2);

    gu::Config conf3;
    gu::ssl_register_params(conf3);
    gcomm::Conf::register_params(conf3);
    conf3.set("pc.weight", "1");
    UUID uuid3(3);
    ProtoUpMeta pum3(uuid3);
    Proto pc3(conf3, uuid3, 0);
    DummyTransport tp3;
    PCUser pu3(conf3, uuid3, &tp3, &pc3);

    triple_boot(0, &pu1, &pu2, &pu3);

    // weight change
    {
        Protolay::sync_param_cb_t sync_param_cb;
        // change weight for node 2 while node 1 leaves the group gracefully
        pu2.pc()->set_param("pc.weight", "1", sync_param_cb);
        ck_assert(sync_param_cb.empty() == false);
        Datagram* install_dg(pu2.tp()->out());
        ck_assert(install_dg != 0);

        // nodes 2 and 3 go to trans, handle install
        View tr23(0, ViewId(V_TRANS, pu2.pc()->current_view().id()));
        tr23.add_member(pu2.uuid(), 0);
        tr23.add_member(pu3.uuid(), 0);
        tr23.add_left(pu1.uuid(), 0);

        pu2.pc()->handle_up(0, Datagram(),
                            ProtoUpMeta(UUID::nil(), ViewId(), &tr23));
        pu3.pc()->handle_up(0, Datagram(),
                            ProtoUpMeta(UUID::nil(), ViewId(), &tr23));
        ck_assert(pu2.pc()->state() == Proto::S_TRANS);
        ck_assert(pu3.pc()->state() == Proto::S_TRANS);

        pu2.pc()->handle_up(0, *install_dg, ProtoUpMeta(pu1.uuid()));
        pu3.pc()->handle_up(0, *install_dg, ProtoUpMeta(pu1.uuid()));

        View reg23(0, ViewId(V_REG, pu2.uuid(),
                             pu2.pc()->current_view().id().seq() + 1));
        reg23.add_member(pu2.uuid(), 0);
        reg23.add_member(pu3.uuid(), 0);

        pu2.pc()->handle_up(0, Datagram(),
                            ProtoUpMeta(UUID::nil(), ViewId(), &reg23));
        pu3.pc()->handle_up(0, Datagram(),
                            ProtoUpMeta(UUID::nil(), ViewId(), &reg23));
        ck_assert(pu2.pc()->state() == Proto::S_STATES_EXCH);
        ck_assert(pu3.pc()->state() == Proto::S_STATES_EXCH);

        Datagram* dg(pu2.tp()->out());
        ck_assert(dg != 0);
        pu2.pc()->handle_up(0, *dg, ProtoUpMeta(pu2.uuid()));
        pu3.pc()->handle_up(0, *dg, ProtoUpMeta(pu2.uuid()));
        delete dg;

        dg = pu3.tp()->out();
        ck_assert(dg != 0);
        pu2.pc()->handle_up(0, *dg, ProtoUpMeta(pu3.uuid()));
        pu3.pc()->handle_up(0, *dg, ProtoUpMeta(pu3.uuid()));
        delete dg;

        ck_assert(pu2.pc()->state() == Proto::S_INSTALL);
        ck_assert(pu3.pc()->state() == Proto::S_INSTALL);

        dg = pu2.tp()->out();
        ck_assert(dg != 0);
        pu2.pc()->handle_up(0, *dg, ProtoUpMeta(pu3.uuid()));
        pu3.pc()->handle_up(0, *dg, ProtoUpMeta(pu3.uuid()));
        delete dg;

        ck_assert(pu2.pc()->state() == Proto::S_PRIM);
        ck_assert(pu3.pc()->state() == Proto::S_PRIM);

        delete install_dg;
    }

}
END_TEST

// node1 and node2 are a cluster.
// before node3 joins, node2 lost connection to node1 and node3.
// after node1 and node3 merged, node2 joins.
// we expect all nodes are a cluster, and they are all in prim state.
static void _test_join_split_cluster(
    const UUID& uuid1, const UUID& uuid2, const UUID& uuid3)
{
    // construct restored view.
    const UUID& prim_uuid = uuid1 < uuid2 ? uuid1 : uuid2;
    View rst_view(0, ViewId(V_PRIM, prim_uuid, 0));
    rst_view.add_member(uuid1, 0);
    rst_view.add_member(uuid2, 0);

    gu::Config conf1;
    gu::ssl_register_params(conf1);
    gcomm::Conf::register_params(conf1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf1, uuid1, 0);
    pc1.set_restored_view(&rst_view);
    DummyTransport tp1;
    PCUser pu1(conf1, uuid1, &tp1, &pc1);
    single_boot(0, &pu1);

    gu::Config conf2;
    gu::ssl_register_params(conf2);
    gcomm::Conf::register_params(conf2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf2, uuid2, 0);
    pc2.set_restored_view(&rst_view);
    DummyTransport tp2;
    PCUser pu2(conf2, uuid2, &tp2, &pc2);

    double_boot(0, &pu1, &pu2);

    gu::Config conf3;
    gu::ssl_register_params(conf3);
    gcomm::Conf::register_params(conf3);
    ProtoUpMeta pum3(uuid3);
    Proto pc3(conf3, uuid3, 0);
    DummyTransport tp3;
    PCUser pu3(conf3, uuid3, &tp3, &pc3);
    // assume previous cluster is node1 and node3.
    const UUID& prim_uuid2 = uuid1 < uuid3 ? uuid1 : uuid3;
    View rst_view2(0, ViewId(V_PRIM, prim_uuid2, 0));
    rst_view2.add_member(uuid1, 0);
    rst_view2.add_member(uuid3, 0);
    pc3.set_restored_view(&rst_view2);


    {
        uint32_t seq = pc1.current_view().id().seq();
        const UUID& reg_uuid = pu1.uuid() < pu3.uuid()
                                            ? pu1.uuid() : pu3.uuid();

        // node1
        View tr1(0, ViewId(V_TRANS, pc1.current_view().id()));
        tr1.add_member(pu1.uuid(), 0);
        tr1.add_partitioned(pu2.uuid(), 0);
        pc1.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr1));
        ck_assert(pc1.state() == Proto::S_TRANS);

        View reg1(0, ViewId(V_REG, reg_uuid, seq + 1));
        reg1.add_member(pu1.uuid(), 0);
        reg1.add_member(pu3.uuid(), 0);
        reg1.add_joined(pu3.uuid(), 0);
        reg1.add_partitioned(pu2.uuid(), 0);
        pc1.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg1));
        ck_assert(pc1.state() == Proto::S_STATES_EXCH);

        // node3
        View tr3(0, ViewId(V_TRANS, pc3.current_view().id()));
        tr3.add_member(pu3.uuid(), 0);
        pc3.connect(false);
        pc3.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr3));
        ck_assert(pc3.state() == Proto::S_TRANS);

        View reg3(0, ViewId(V_REG, reg_uuid, seq + 1));
        reg3.add_member(pu1.uuid(), 0);
        reg3.add_member(pu3.uuid(), 0);
        reg3.add_joined(pu1.uuid(), 0);
        pc3.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg3));
        ck_assert(pc3.state() == Proto::S_STATES_EXCH);

        Datagram* dg1(pu1.tp()->out());
        ck_assert(dg1 != 0);
        Datagram* dg3(pu3.tp()->out());
        ck_assert(dg3 != 0);

        pc1.handle_up(0, *dg1, ProtoUpMeta(pu1.uuid()));
        pc1.handle_up(0, *dg3, ProtoUpMeta(pu3.uuid()));
        ck_assert(pc1.state() == Proto::S_NON_PRIM);
        pc3.handle_up(0, *dg1, ProtoUpMeta(pu1.uuid()));
        pc3.handle_up(0, *dg3, ProtoUpMeta(pu3.uuid()));
        ck_assert(pc3.state() == Proto::S_NON_PRIM);

        delete dg1;
        delete dg3;
    }
    {
        // node2
        uint32_t seq = pc2.current_view().id().seq();
        View tr2(0, ViewId(V_TRANS, pc2.current_view().id()));
        tr2.add_member(pu2.uuid(), 0);
        tr2.add_partitioned(pu1.uuid(), 0);
        pc2.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr2));
        ck_assert(pc2.state() == Proto::S_TRANS);

        View reg2(0, ViewId(V_REG, pc2.uuid(), seq + 1));
        reg2.add_member(pu2.uuid(), 0);
        reg2.add_partitioned(pu1.uuid(), 0);
        pc2.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg2));
        ck_assert(pc2.state() == Proto::S_STATES_EXCH);

        Datagram* dg2(pu2.tp()->out());
        ck_assert(dg2 != 0);
        pc2.handle_up(0, *dg2, ProtoUpMeta(pu2.uuid()));
        ck_assert(pc2.state() == Proto::S_NON_PRIM);
        delete dg2;
    }
    {
        View tr1(0, ViewId(V_TRANS, pc1.current_view().id()));
        tr1.add_member(pu1.uuid(), 0);
        tr1.add_member(pu3.uuid(), 0);
        pc1.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr1));
        ck_assert(pc1.state() == Proto::S_TRANS);

        View tr2(0, ViewId(V_TRANS, pc2.current_view().id()));
        tr2.add_member(pu2.uuid(), 0);
        pc2.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr2));
        ck_assert(pc2.state() == Proto::S_TRANS);

        View tr3(0, ViewId(V_TRANS, pc3.current_view().id()));
        tr3.add_member(pu1.uuid(), 0);
        tr3.add_member(pu3.uuid(), 0);
        pc3.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr3));
        ck_assert(pc3.state() == Proto::S_TRANS);

        int seq = pc1.current_view().id().seq();
        const UUID& reg_uuid1 = pu1.uuid() < pu2.uuid() ? pu1.uuid() : pu2.uuid();
        const UUID& reg_uuid = reg_uuid1 < pu3.uuid() ? reg_uuid1 : pu3.uuid();
        View reg1(0, ViewId(V_REG, reg_uuid, seq + 1));
        reg1.add_member(pu1.uuid(), 0);
        reg1.add_member(pu2.uuid(), 0);
        reg1.add_member(pu3.uuid(), 0);
        reg1.add_joined(pu2.uuid(), 0);
        pc1.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg1));
        ck_assert(pc1.state() == Proto::S_STATES_EXCH);
        pc3.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg1));
        ck_assert(pc3.state() == Proto::S_STATES_EXCH);

        View reg2(0, ViewId(V_REG, reg_uuid, seq + 1));
        reg2.add_member(pu1.uuid(), 0);
        reg2.add_member(pu2.uuid(), 0);
        reg2.add_member(pu3.uuid(), 0);
        reg2.add_joined(pu1.uuid(), 0);
        reg2.add_joined(pu3.uuid(), 0);
        pc2.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg2));
        ck_assert(pc2.state() == Proto::S_STATES_EXCH);

        Datagram* dg1(pu1.tp()->out());
        Datagram* dg2(pu2.tp()->out());
        Datagram* dg3(pu3.tp()->out());
        pc1.handle_up(0, *dg1, ProtoUpMeta(pu1.uuid()));
        pc1.handle_up(0, *dg2, ProtoUpMeta(pu2.uuid()));
        pc1.handle_up(0, *dg3, ProtoUpMeta(pu3.uuid()));
        ck_assert(pc1.state() == Proto::S_INSTALL);
        pc2.handle_up(0, *dg1, ProtoUpMeta(pu1.uuid()));
        pc2.handle_up(0, *dg2, ProtoUpMeta(pu2.uuid()));
        pc2.handle_up(0, *dg3, ProtoUpMeta(pu3.uuid()));
        ck_assert(pc2.state() == Proto::S_INSTALL);
        pc3.handle_up(0, *dg1, ProtoUpMeta(pu1.uuid()));
        pc3.handle_up(0, *dg2, ProtoUpMeta(pu2.uuid()));
        pc3.handle_up(0, *dg3, ProtoUpMeta(pu3.uuid()));
        ck_assert(pc3.state() == Proto::S_INSTALL);
        delete dg1;
        delete dg2;
        delete dg3;

        Datagram* dg = 0;
        PCUser* pcs[3] = {&pu1, &pu2, &pu3};
        for (int i=0; i<3; i++) {
            if (pcs[i]->uuid() == reg_uuid) {
                dg = pcs[i]->tp()->out();
                ck_assert(dg != 0);
            } else {
                ck_assert(!pcs[i]->tp()->out());
            }
        }
        pc1.handle_up(0, *dg, ProtoUpMeta(reg_uuid));
        pc2.handle_up(0, *dg, ProtoUpMeta(reg_uuid));
        pc3.handle_up(0, *dg, ProtoUpMeta(reg_uuid));
        ck_assert(pc1.state() == Proto::S_PRIM);
        ck_assert(pc2.state() == Proto::S_PRIM);
        ck_assert(pc3.state() == Proto::S_PRIM);
        delete dg;
    }
}
START_TEST(test_join_split_cluster)
{
    log_info << "START (test_join_split_cluster)";
    gu_conf_debug_on();
    UUID uuid1(1);
    UUID uuid2(2);
    UUID uuid3(3);
    _test_join_split_cluster(uuid1, uuid2, uuid3);
    _test_join_split_cluster(uuid2, uuid1, uuid3);
    _test_join_split_cluster(uuid2, uuid3, uuid1);
}
END_TEST

START_TEST(test_trac_762)
{
    log_info << "START (trac_762)";
    size_t n_nodes(3);
    vector<DummyNode*> dn;
    PropagationMatrix prop;
    const string suspect_timeout("PT0.35S");
    const string inactive_timeout("PT0.7S");
    const string retrans_period("PT0.1S");
    uint32_t view_seq = 0;

    for (size_t i = 0; i < n_nodes; ++i)
    {
        dn.push_back(create_dummy_node(i + 1, 0,
                                       suspect_timeout,
                                       inactive_timeout,
                                       retrans_period));
        gu_trace(join_node(&prop, dn[i], i == 0));
        set_cvi(dn, 0, i, ++view_seq, V_PRIM);
        gu_trace(prop.propagate_until_cvi(false));
    }

    log_info << "split 1";
    // split group so that node 3 becomes isolated
    prop.split(1, 3);
    prop.split(2, 3);
    ++view_seq;
    set_cvi(dn, 0, 1, view_seq, V_PRIM);
    set_cvi(dn, 2, 2, view_seq, V_NON_PRIM);
    gu_trace(prop.propagate_until_cvi(true));

    mark_point();
    log_info << "remerge 1";

    // detach PC layer from EVS and lower layers, attach to DummyTransport
    for (size_t i(0); i < n_nodes; ++i)
    {
        std::list<Protolay*>::iterator li0(dn[i]->protos().begin());
        std::list<Protolay*>::iterator li1(li0);
        ++li1;
        assert(li1 != dn[i]->protos().end());
        std::list<Protolay*>::iterator li2(li1);
        ++li2;
        assert(li2 != dn[i]->protos().end());
        gcomm::disconnect(*li0, *li1);
        gcomm::disconnect(*li1, *li2);
        delete *li0;
        delete *li1;
        dn[i]->protos().pop_front();
        dn[i]->protos().pop_front();

        DummyTransport* tp(new DummyTransport(dn[i]->uuid(), true));
        dn[i]->protos().push_front(tp);
        gcomm::connect(tp, *li2);
    }

    Proto* pc1(pc_from_dummy(dn[0]));
    DummyTransport* tp1(reinterpret_cast<DummyTransport*>(
                            dn[0]->protos().front()));
    Proto* pc2(pc_from_dummy(dn[1]));
    DummyTransport* tp2(reinterpret_cast<DummyTransport*>(
                            dn[1]->protos().front()));
    Proto* pc3(pc_from_dummy(dn[2]));
    DummyTransport* tp3(reinterpret_cast<DummyTransport*>(
                            dn[2]->protos().front()));


    // remerge group, process event by event so that nodes 1 and 2 handle
    // install message in reg view and reach prim view, node 3 partitions and
    // handles install in trans view and marks nodes 1 and 2 to have un state
    {
        View tr1(0, ViewId(V_TRANS, tp1->uuid(), view_seq));
        tr1.add_member(tp1->uuid(), 0);
        tr1.add_member(tp2->uuid(), 0);
        pc1->handle_view(tr1);
        pc2->handle_view(tr1);

        View tr2(0, ViewId(V_TRANS, tp3->uuid(), view_seq));
        tr2.add_member(tp3->uuid(), 0);
        pc3->handle_view(tr2);

        ++view_seq;
        View reg(0, ViewId(V_REG, tp1->uuid(), view_seq));
        reg.add_member(tp1->uuid(), 0);
        reg.add_member(tp2->uuid(), 0);
        reg.add_member(tp3->uuid(), 0);

        pc1->handle_view(reg);
        pc2->handle_view(reg);
        pc3->handle_view(reg);

        // states exch
        Datagram* dg(tp1->out());
        ck_assert(dg != 0);
        pc1->handle_up(0, *dg, ProtoUpMeta(tp1->uuid()));
        pc2->handle_up(0, *dg, ProtoUpMeta(tp1->uuid()));
        pc3->handle_up(0, *dg, ProtoUpMeta(tp1->uuid()));
        delete dg;

        dg = tp2->out();
        ck_assert(dg != 0);
        pc1->handle_up(0, *dg, ProtoUpMeta(tp2->uuid()));
        pc2->handle_up(0, *dg, ProtoUpMeta(tp2->uuid()));
        pc3->handle_up(0, *dg, ProtoUpMeta(tp2->uuid()));
        delete dg;

        dg = tp3->out();
        ck_assert(dg != 0);
        pc1->handle_up(0, *dg, ProtoUpMeta(tp3->uuid()));
        pc2->handle_up(0, *dg, ProtoUpMeta(tp3->uuid()));
        pc3->handle_up(0, *dg, ProtoUpMeta(tp3->uuid()));
        delete dg;

        // install message
        dg = tp1->out();
        ck_assert(dg != 0);
        pc1->handle_up(0, *dg, ProtoUpMeta(tp1->uuid()));
        pc2->handle_up(0, *dg, ProtoUpMeta(tp1->uuid()));

        View tr3(0, ViewId(V_TRANS, tp1->uuid(), view_seq));
        tr3.add_member(tp1->uuid(), 0);
        tr3.add_member(tp2->uuid(), 0);
        tr3.add_partitioned(tp3->uuid(), 0);

        pc1->handle_view(tr3);
        pc2->handle_view(tr3);

        View tr4(0, ViewId(V_TRANS, tp1->uuid(), view_seq));
        tr4.add_member(tp3->uuid(), 0);
        tr4.add_partitioned(tp1->uuid(), 0);
        tr4.add_partitioned(tp2->uuid(), 0);
        pc3->handle_view(tr4);
        pc3->handle_up(0, *dg, ProtoUpMeta(tp1->uuid()));
        delete dg;
    }

    ++view_seq;
    // ... intermediate reg/trans views
    // 1 and 2
    {

        View reg(0, ViewId(V_REG, tp1->uuid(), view_seq));
        reg.add_member(tp1->uuid(), 0);
        reg.add_member(tp2->uuid(), 0);
        pc1->handle_view(reg);
        pc2->handle_view(reg);

        View tr(0, ViewId(V_TRANS, tp1->uuid(), view_seq));
        tr.add_member(tp1->uuid(), 0);
        tr.add_member(tp2->uuid(), 0);
        pc1->handle_view(tr);
        pc2->handle_view(tr);

        Datagram* dg(tp1->out());
        ck_assert(dg != 0);
        pc1->handle_up(0, *dg, ProtoUpMeta(tp1->uuid()));
        pc2->handle_up(0, *dg, ProtoUpMeta(tp1->uuid()));
        delete dg;

        dg = tp2->out();
        ck_assert(dg != 0);
        pc1->handle_up(0, *dg, ProtoUpMeta(tp1->uuid()));
        pc2->handle_up(0, *dg, ProtoUpMeta(tp1->uuid()));
        delete dg;
    }
    // 3
    {
        View reg(0, ViewId(V_REG, tp3->uuid(), view_seq));
        reg.add_member(tp3->uuid(), 0);
        pc3->handle_view(reg);

        Datagram* dg(tp3->out());
        ck_assert(dg != 0);
        pc3->handle_up(0, *dg, ProtoUpMeta(tp3->uuid()));
        delete dg;

        View tr(0, ViewId(V_TRANS, tp3->uuid(), view_seq));
        tr.add_member(tp3->uuid(), 0);
        pc3->handle_view(tr);
    }

    // Remerge and PC crash should occur if bug is present.
    ++view_seq;
    {
        View reg(0, ViewId(V_REG, tp1->uuid(), view_seq));
        reg.add_member(tp1->uuid(), 0);
        reg.add_member(tp2->uuid(), 0);
        reg.add_member(tp3->uuid(), 0);

        pc1->handle_view(reg);
        pc2->handle_view(reg);
        pc3->handle_view(reg);

        // State msgs
        Datagram* dg(tp1->out());
        ck_assert(dg != 0);
        pc1->handle_up(0, *dg, ProtoUpMeta(tp1->uuid()));
        pc2->handle_up(0, *dg, ProtoUpMeta(tp1->uuid()));
        pc3->handle_up(0, *dg, ProtoUpMeta(tp1->uuid()));
        delete dg;

        dg = tp2->out();
        ck_assert(dg != 0);
        pc1->handle_up(0, *dg, ProtoUpMeta(tp2->uuid()));
        pc2->handle_up(0, *dg, ProtoUpMeta(tp2->uuid()));
        pc3->handle_up(0, *dg, ProtoUpMeta(tp2->uuid()));
        delete dg;

        dg = tp3->out();
        ck_assert(dg != 0);
        pc1->handle_up(0, *dg, ProtoUpMeta(tp3->uuid()));
        pc2->handle_up(0, *dg, ProtoUpMeta(tp3->uuid()));
        pc3->handle_up(0, *dg, ProtoUpMeta(tp3->uuid()));
        delete dg;

        // Install msg
        dg = tp1->out();
        ck_assert(dg != 0);
        pc1->handle_up(0, *dg, ProtoUpMeta(tp1->uuid()));
        pc2->handle_up(0, *dg, ProtoUpMeta(tp1->uuid()));
        pc3->handle_up(0, *dg, ProtoUpMeta(tp1->uuid()));

        ck_assert(tp1->out() == 0);
        ck_assert(tp2->out() == 0);
        ck_assert(tp3->out() == 0);
        delete dg;
    }
    std::for_each(dn.begin(), dn.end(), gu::DeleteObject());
}
END_TEST

START_TEST(test_gh_92)
{
    UUID uuid1(1), uuid2(2), uuid3(3);
    gu::Config conf1;
    gu::ssl_register_params(conf1);
    gcomm::Conf::register_params(conf1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf1, uuid1, 0);
    DummyTransport tp1;
    PCUser pu1(conf1, uuid1, &tp1, &pc1);
    single_boot(0, &pu1);

    gu::Config conf2;
    gu::ssl_register_params(conf2);
    gcomm::Conf::register_params(conf2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf2, uuid2, 0);
    DummyTransport tp2;
    PCUser pu2(conf2, uuid2, &tp2, &pc2);
    double_boot(0, &pu1, &pu2);

    gu::Config conf3;
    gu::ssl_register_params(conf3);
    gcomm::Conf::register_params(conf3);
    ProtoUpMeta pum3(uuid3);
    Proto pc3(conf3, uuid3, 0);
    DummyTransport tp3;
    PCUser pu3(conf3, uuid3, &tp3, &pc3);
    triple_boot(0, &pu1, &pu2, &pu3);

    uint32_t seq = pc1.current_view().id().seq();
    Datagram* im = 0;
    Datagram* dg = 0;

    // they split into three parts.
    {
        View tr1(0, ViewId(V_TRANS, pc1.current_view().id()));
        tr1.add_member(pu1.uuid(), 0);
        tr1.add_partitioned(pu2.uuid(), 0);
        tr1.add_partitioned(pu3.uuid(), 0);
        pc1.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr1));
        ck_assert(pc1.state() == Proto::S_TRANS);

        View reg1(0, ViewId(V_REG, uuid1, seq + 1));
        reg1.add_member(pu1.uuid(), 0);
        pc1.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg1));
        ck_assert(pc1.state() == Proto::S_STATES_EXCH);
        dg = pu1.tp()->out();
        pc1.handle_up(0, *dg, ProtoUpMeta(pu1.uuid()));
        ck_assert(pc1.state() == Proto::S_NON_PRIM);

        View tr2(0, ViewId(V_TRANS, pc2.current_view().id()));
        tr2.add_member(pu2.uuid(), 0);
        tr2.add_partitioned(pu1.uuid(), 0);
        tr2.add_partitioned(pu3.uuid(), 0);
        pc2.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr2));
        ck_assert(pc2.state() == Proto::S_TRANS);

        View reg2(0, ViewId(V_REG, uuid2, seq + 1));
        reg2.add_member(pu2.uuid(), 0);
        pc2.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg2));
        ck_assert(pc2.state() == Proto::S_STATES_EXCH);
        delete dg;
        dg = pu2.tp()->out();
        pc2.handle_up(0, *dg, ProtoUpMeta(pu2.uuid()));
        ck_assert(pc2.state() == Proto::S_NON_PRIM);

        View tr3(0, ViewId(V_TRANS, pc3.current_view().id()));
        tr3.add_member(pu3.uuid(), 0);
        tr3.add_partitioned(pu1.uuid(), 0);
        tr3.add_partitioned(pu2.uuid(), 0);
        pc3.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr3));
        ck_assert(pc3.state() == Proto::S_TRANS);

        View reg3(0, ViewId(V_REG, uuid3, seq + 1));
        reg3.add_member(pu3.uuid(), 0);
        pc3.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg3));
        ck_assert(pc3.state() == Proto::S_STATES_EXCH);
        delete dg;
        dg = pu3.tp()->out();
        pc3.handle_up(0, *dg, ProtoUpMeta(pu3.uuid()));
        ck_assert(pc3.state() == Proto::S_NON_PRIM);
        delete dg;
        dg = 0;
    }
    seq += 1;

    // they try to merge into a primary component, but fails when sending install message.
    {
        View tr1(0, ViewId(V_TRANS, pc1.current_view().id()));
        tr1.add_member(pu1.uuid(), 0);
        pc1.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr1));
        ck_assert(pc1.state() == Proto::S_TRANS);

        View reg1(0, ViewId(V_REG, uuid1, seq + 1));
        reg1.add_member(pu1.uuid(), 0);
        reg1.add_member(pu2.uuid(), 0);
        reg1.add_member(pu3.uuid(), 0);
        reg1.add_joined(pu2.uuid(), 0);
        reg1.add_joined(pu3.uuid(), 0);
        pc1.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg1));
        ck_assert(pc1.state() == Proto::S_STATES_EXCH);

        View tr2(0, ViewId(V_TRANS, pc2.current_view().id()));
        tr2.add_member(pu2.uuid(), 0);
        pc2.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr2));
        ck_assert(pc2.state() == Proto::S_TRANS);

        View reg2(0, ViewId(V_REG, uuid1, seq + 1));
        reg2.add_member(pu1.uuid(), 0);
        reg2.add_member(pu2.uuid(), 0);
        reg2.add_member(pu3.uuid(), 0);
        reg2.add_joined(pu1.uuid(), 0);
        reg2.add_joined(pu3.uuid(), 0);
        pc2.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg2));
        ck_assert(pc2.state() == Proto::S_STATES_EXCH);

        View tr3(0, ViewId(V_TRANS, pc3.current_view().id()));
        tr3.add_member(pu3.uuid(), 0);
        pc3.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr3));
        ck_assert(pc3.state() == Proto::S_TRANS);

        View reg3(0, ViewId(V_REG, uuid1, seq + 1));
        reg3.add_member(pu1.uuid(), 0);
        reg3.add_member(pu2.uuid(), 0);
        reg3.add_member(pu3.uuid(), 0);
        reg3.add_joined(pu1.uuid(), 0);
        reg3.add_joined(pu2.uuid(), 0);
        pc3.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg3));
        ck_assert(pc3.state() == Proto::S_STATES_EXCH);

        Datagram* dg1(pu1.tp()->out());
        Datagram* dg2(pu2.tp()->out());
        Datagram* dg3(pu3.tp()->out());
        ck_assert(dg1 != 0);
        ck_assert(dg2 != 0);
        ck_assert(dg3 != 0);
        pc1.handle_up(0, *dg1, ProtoUpMeta(pu1.uuid()));
        pc1.handle_up(0, *dg2, ProtoUpMeta(pu2.uuid()));
        pc1.handle_up(0, *dg3, ProtoUpMeta(pu3.uuid()));
        pc2.handle_up(0, *dg1, ProtoUpMeta(pu1.uuid()));
        pc2.handle_up(0, *dg2, ProtoUpMeta(pu2.uuid()));
        pc2.handle_up(0, *dg3, ProtoUpMeta(pu3.uuid()));
        pc3.handle_up(0, *dg1, ProtoUpMeta(pu1.uuid()));
        pc3.handle_up(0, *dg2, ProtoUpMeta(pu2.uuid()));
        pc3.handle_up(0, *dg3, ProtoUpMeta(pu3.uuid()));
        delete dg1; delete dg2; delete dg3;
        ck_assert(pc1.state() == Proto::S_INSTALL);
        ck_assert(pc2.state() == Proto::S_INSTALL);
        ck_assert(pc3.state() == Proto::S_INSTALL);

        im = pu1.tp()->out();
        ck_assert(im != 0);
        ck_assert(pu2.tp()->out() == 0);
        ck_assert(pu3.tp()->out() == 0);
    }
    seq += 1;

    // node3 is separate from node1 and node2.
    // they get the stale install message when they get transient view.
    {
        View tr1(0, ViewId(V_TRANS, pc1.current_view().id()));
        tr1.add_member(pu1.uuid(), 0);
        tr1.add_member(pu2.uuid(), 0);
        tr1.add_partitioned(pu3.uuid(), 0);
        pc1.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr1));
        pc2.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr1));
        ck_assert(pc1.state() == Proto::S_TRANS);
        ck_assert(pc2.state() == Proto::S_TRANS);
        pc1.handle_up(0, *im, ProtoUpMeta(pu1.uuid()));
        pc2.handle_up(0, *im, ProtoUpMeta(pu1.uuid()));
        ck_assert(pc1.state() == Proto::S_TRANS);
        ck_assert(pc2.state() == Proto::S_TRANS);

        View reg1(0, ViewId(V_REG, uuid1, seq + 1));
        reg1.add_member(pu1.uuid(), 0);
        reg1.add_member(pu2.uuid(), 0);
        reg1.add_partitioned(pu3.uuid(), 0);
        pc1.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg1));
        pc2.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg1));
        ck_assert(pc1.state() == Proto::S_STATES_EXCH);
        ck_assert(pc2.state() == Proto::S_STATES_EXCH);
        Datagram* dg1(pu1.tp()->out());
        Datagram* dg2(pu2.tp()->out());
        ck_assert(dg1 != 0);
        ck_assert(dg2 != 0);
        pc1.handle_up(0, *dg1, ProtoUpMeta(pu1.uuid()));
        pc1.handle_up(0, *dg2, ProtoUpMeta(pu2.uuid()));
        pc2.handle_up(0, *dg1, ProtoUpMeta(pu1.uuid()));
        pc2.handle_up(0, *dg2, ProtoUpMeta(pu2.uuid()));
        ck_assert(pc1.state() == Proto::S_NON_PRIM);
        ck_assert(pc2.state() == Proto::S_NON_PRIM);

        View tr3(0, ViewId(V_TRANS, pc3.current_view().id()));
        tr3.add_member(pu3.uuid(), 0);
        tr3.add_partitioned(pu1.uuid(), 0);
        tr3.add_partitioned(pu2.uuid(), 0);
        pc3.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr3));
        ck_assert(pc3.state() == Proto::S_TRANS);
        pc3.handle_up(0, *im, ProtoUpMeta(pu1.uuid()));
        ck_assert(pc3.state() == Proto::S_TRANS);

        View reg3(0, ViewId(V_REG, uuid3, seq + 1));
        reg3.add_member(pu3.uuid(), 0);
        reg3.add_partitioned(pu1.uuid(), 0);
        reg3.add_partitioned(pu2.uuid(), 0);
        pc3.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg3));
        ck_assert(pc3.state() == Proto::S_STATES_EXCH);
        Datagram* dg3(pu3.tp()->out());
        ck_assert(dg3 != 0);
        pc3.handle_up(0, *dg3, ProtoUpMeta(pu3.uuid()));
        ck_assert(pc3.state() == Proto::S_NON_PRIM);

        delete dg1; delete dg2; delete dg3;
    }
    seq += 1;

    // then they try to merge into a primary component again.
    {
        View tr1(0, ViewId(V_TRANS, pc1.current_view().id()));
        tr1.add_member(pu1.uuid(), 0);
        tr1.add_member(pu2.uuid(), 0);
        pc1.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr1));
        pc2.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr1));
        ck_assert(pc1.state() == Proto::S_TRANS);
        ck_assert(pc2.state() == Proto::S_TRANS);

        View reg1(0, ViewId(V_REG, uuid1, seq + 1));
        reg1.add_member(pu1.uuid(), 0);
        reg1.add_member(pu2.uuid(), 0);
        reg1.add_member(pu3.uuid(), 0);
        reg1.add_joined(pu3.uuid(), 0);
        pc1.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg1));
        pc2.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg1));
        ck_assert(pc1.state() == Proto::S_STATES_EXCH);
        ck_assert(pc2.state() == Proto::S_STATES_EXCH);

        View tr3(0, ViewId(V_TRANS, pc3.current_view().id()));
        tr3.add_member(pu3.uuid(), 0);
        pc3.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr3));
        ck_assert(pc3.state() == Proto::S_TRANS);

        View reg3(0, ViewId(V_REG, uuid1, seq + 1));
        reg3.add_member(pu1.uuid(), 0);
        reg3.add_member(pu2.uuid(), 0);
        reg3.add_member(pu3.uuid(), 0);
        reg3.add_joined(pu1.uuid(), 0);
        reg3.add_joined(pu2.uuid(), 0);
        pc3.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg3));
        ck_assert(pc3.state() == Proto::S_STATES_EXCH);

        Datagram* dg1(pu1.tp()->out());
        Datagram* dg2(pu2.tp()->out());
        Datagram* dg3(pu3.tp()->out());
        ck_assert(dg1 != 0);
        ck_assert(dg2 != 0);
        ck_assert(dg3 != 0);
        pc1.handle_up(0, *dg1, ProtoUpMeta(pu1.uuid()));
        pc1.handle_up(0, *dg2, ProtoUpMeta(pu2.uuid()));
        pc1.handle_up(0, *dg3, ProtoUpMeta(pu3.uuid()));
        pc2.handle_up(0, *dg1, ProtoUpMeta(pu1.uuid()));
        pc2.handle_up(0, *dg2, ProtoUpMeta(pu2.uuid()));
        pc2.handle_up(0, *dg3, ProtoUpMeta(pu3.uuid()));
        pc3.handle_up(0, *dg1, ProtoUpMeta(pu1.uuid()));
        pc3.handle_up(0, *dg2, ProtoUpMeta(pu2.uuid()));
        pc3.handle_up(0, *dg3, ProtoUpMeta(pu3.uuid()));
        delete dg1; delete dg2; delete dg3;
        ck_assert(pc1.state() == Proto::S_INSTALL);
        ck_assert(pc2.state() == Proto::S_INSTALL);
        ck_assert(pc3.state() == Proto::S_INSTALL);

        delete im;
        im = pu1.tp()->out();
        ck_assert(im != 0);
        ck_assert(pu2.tp()->out() == 0);
        ck_assert(pu3.tp()->out() == 0);
        pc1.handle_up(0, *im, ProtoUpMeta(pu1.uuid()));
        pc2.handle_up(0, *im, ProtoUpMeta(pu1.uuid()));
        pc3.handle_up(0, *im, ProtoUpMeta(pu1.uuid()));
        ck_assert(pc1.state() == Proto::S_PRIM);
        ck_assert(pc2.state() == Proto::S_PRIM);
        ck_assert(pc3.state() == Proto::S_PRIM);
        delete im;
    }
}
END_TEST

// Nodes 1, 2, 3. Node 3 will be evicted from group while group is
// fully partitioned. After remerging 1 and 2 they should reach
// primary component.
START_TEST(test_prim_after_evict)
{
    log_info << "START(test_prim_after_evict)";
    UUID uuid1(1), uuid2(2), uuid3(3);
    gu::Config conf1;
    gu::ssl_register_params(conf1);
    gcomm::Conf::register_params(conf1);
    ProtoUpMeta pum1(uuid1);
    Proto pc1(conf1, uuid1, 0);
    DummyTransport tp1;
    PCUser pu1(conf1, uuid1, &tp1, &pc1);
    single_boot(1, &pu1);

    gu::Config conf2;
    gu::ssl_register_params(conf2);
    gcomm::Conf::register_params(conf2);
    ProtoUpMeta pum2(uuid2);
    Proto pc2(conf2, uuid2, 0);
    DummyTransport tp2;
    PCUser pu2(conf2, uuid2, &tp2, &pc2);
    double_boot(1, &pu1, &pu2);

    gu::Config conf3;
    gu::ssl_register_params(conf3);
    gcomm::Conf::register_params(conf3);
    ProtoUpMeta pum3(uuid3);
    Proto pc3(conf3, uuid3, 0);
    DummyTransport tp3;
    PCUser pu3(conf3, uuid3, &tp3, &pc3);
    triple_boot(1, &pu1, &pu2, &pu3);

    // Node 1 partitions
    {
        // Trans view
        View tr1(1, ViewId(V_TRANS, pc1.current_view().id()));
        tr1.add_member(pc1.uuid(), 0);
        tr1.add_partitioned(pc2.uuid(), 0);
        tr1.add_partitioned(pc3.uuid(), 0);
        pc1.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr1));
        // Reg view
        View reg1(1, ViewId(V_REG, pc1.uuid(), tr1.id().seq() + 1));
        reg1.add_member(pc1.uuid(), 0);
        pc1.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg1));
        // States exch
        Datagram* dg(tp1.out());
        ck_assert(dg != 0);
        pc1.handle_up(0, *dg, ProtoUpMeta(pc1.uuid()));
        delete dg;
        // Non-prim
        dg = tp1.out();
        ck_assert(dg == 0);
        ck_assert(pc1.state() == Proto::S_NON_PRIM);
    }

    // Node 2 partitions
    {
        // Trans view
        View tr2(1, ViewId(V_TRANS, pc2.current_view().id()));
        tr2.add_member(pc2.uuid(), 0);
        tr2.add_partitioned(pc1.uuid(), 0);
        tr2.add_partitioned(pc3.uuid(), 0);
        pc2.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr2));
        // Reg view
        View reg2(1, ViewId(V_REG, pc2.uuid(), tr2.id().seq() + 1));
        reg2.add_member(pc2.uuid(), 0);
        pc2.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg2));
        // States exch
        Datagram* dg(tp2.out());
        ck_assert(dg != 0);
        pc2.handle_up(0, *dg, ProtoUpMeta(pc2.uuid()));
        delete dg;
        // Non-prim
        dg = tp2.out();
        ck_assert(dg == 0);
        ck_assert(pc2.state() == Proto::S_NON_PRIM);
    }

    // Just forget about node3, it is gone forever
    // Nodes 1 and 2 set node3 evicted

    pc1.evict(pc3.uuid());
    pc2.evict(pc3.uuid());

    // Nodes 1 and 2 merge and should reach Prim

    {
        // Trans view for node 1
        View tr1(1, ViewId(V_TRANS, pc1.current_view().id()));
        tr1.add_member(pc1.uuid(), 0);
        pc1.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr1));
        Datagram *dg(tp1.out());
        ck_assert(dg == 0);
        ck_assert(pc1.state() == Proto::S_TRANS);

        // Trans view for node 2
        View tr2(1, ViewId(V_TRANS, pc2.current_view().id()));
        tr2.add_member(pc2.uuid(), 0);
        pc2.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &tr2));
        dg = tp2.out();
        ck_assert(dg == 0);
        ck_assert(pc2.state() == Proto::S_TRANS);

        // Reg view for nodes 1 and 2
        View reg(1, ViewId(V_REG, pc1.uuid(), tr1.id().seq() + 1));
        reg.add_member(pc1.uuid(), 0);
        reg.add_member(pc2.uuid(), 0);
        pc1.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg));
        pc2.handle_up(0, Datagram(), ProtoUpMeta(UUID::nil(), ViewId(), &reg));

        // States exchange
        ck_assert(pc1.state() == Proto::S_STATES_EXCH);
        ck_assert(pc2.state() == Proto::S_STATES_EXCH);

        // State message from node 1
        dg = tp1.out();
        ck_assert(dg != 0);
        pc1.handle_up(0, *dg, ProtoUpMeta(pc1.uuid()));
        pc2.handle_up(0, *dg, ProtoUpMeta(pc1.uuid()));
        delete dg;
        dg = tp1.out();
        ck_assert(dg == 0);

        // State message from node 2
        dg = tp2.out();
        ck_assert(dg != 0);
        pc1.handle_up(0, *dg, ProtoUpMeta(pc2.uuid()));
        pc2.handle_up(0, *dg, ProtoUpMeta(pc2.uuid()));
        delete dg;
        dg = tp2.out();
        ck_assert(dg == 0);

        // Install
        ck_assert_msg(pc1.state() == Proto::S_INSTALL, "state is %s",
                      Proto::to_string(pc1.state()).c_str());
        ck_assert_msg(pc2.state() == Proto::S_INSTALL, "state is %s",
                      Proto::to_string(pc2.state()).c_str());

        // Install message from node 1
        dg = tp1.out();
        ck_assert(dg != 0);
        pc1.handle_up(0, *dg, ProtoUpMeta(pc1.uuid()));
        pc2.handle_up(0, *dg, ProtoUpMeta(pc1.uuid()));
        delete dg;

        // Prim
        dg = tp1.out();
        ck_assert(dg == 0);
        dg = tp2.out();
        ck_assert(dg == 0);
        ck_assert(pc1.state() == Proto::S_PRIM);
        ck_assert(pc2.state() == Proto::S_PRIM);
    }


}
END_TEST

class DummyEvs : public gcomm::Bottomlay
{
public:
    DummyEvs(gu::Config& conf) : gcomm::Bottomlay(conf) { }
    int handle_down(Datagram&, const ProtoDownMeta&) { return 0; }
};

class DummyTop : public gcomm::Toplay
{
public:
    DummyTop(gu::Config& conf) : gcomm::Toplay(conf) { }
    void handle_up(const void*, const gcomm::Datagram&,
                   const gcomm::ProtoUpMeta&) { }
};

// Test outline:
// * Three node cluster, nodes n1, n2, n3
// * Current primary view is (n1, n2), view number 2
// * Group is merging, current EVS view is (n1, n2, n3),
//   view number 3
// * State messages have been delivered, but group partitioned again when
//   install message was being sent.
// * Underlying EVS membership changes so that the transitional view
//   ends up in (n1, n3), paritioned (n2)
// * It is expected that the n1 ends up in non-primary component.
START_TEST(test_quorum_2_to_2_in_3_node_cluster)
{
    gu_log_max_level = GU_LOG_DEBUG;
    gcomm::pc::ProtoBuilder builder;
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);

    // Current view is EVS view (n1, n2, n3), view number 3
    gcomm::View current_view(0, gcomm::ViewId(V_REG, gcomm::UUID(1), 3));
    current_view.add_member(gcomm::UUID(1), 0);
    current_view.add_member(gcomm::UUID(2), 0);
    current_view.add_member(gcomm::UUID(3), 0);

    // Primary component view (n1, n2), view number 2
    gcomm::View pc_view(0, gcomm::ViewId(V_PRIM, gcomm::UUID(1), 2));
    pc_view.add_member(gcomm::UUID(1), 0);
    pc_view.add_member(gcomm::UUID(2), 0);

    // Known instances according to state messages.
    gcomm::pc::Node node1(true, false, false, 0,
                          gcomm::ViewId(V_PRIM, gcomm::UUID(1), 2),
                          0, 1, 0);
    gcomm::pc::Node node2(true, false, false, 0,
                          gcomm::ViewId(V_PRIM, gcomm::UUID(1), 2),
                          0, 1, 0);
    gcomm::pc::Node node3(false, false, false, 0,
                          gcomm::ViewId(V_PRIM, gcomm::UUID(1), 1),
                          0, 1, 0);
    gcomm::pc::NodeMap instances;
    instances.insert(std::make_pair(gcomm::UUID(1), node1));
    instances.insert(std::make_pair(gcomm::UUID(2), node2));
    instances.insert(std::make_pair(gcomm::UUID(3), node3));

    // State messages for all nodes.
    // * Nodes n1, n2 report previous prim view (n1, n2), view number 2.
    // * Node 3 reports previous prim view (n1, n2, n3), view number 1.
    gcomm::pc::Proto::SMMap state_msgs;
    {
        // Node n1
        gcomm::pc::NodeMap nm;
        nm.insert(std::make_pair(gcomm::UUID(1), node1));
        nm.insert(std::make_pair(gcomm::UUID(2), node2));
        gcomm::pc::Message msg(1, gcomm::pc::Message::PC_T_STATE, 0, nm);
        state_msgs.insert(std::make_pair(gcomm::UUID(1), msg));
    }
    {
        // Node n2
        gcomm::pc::NodeMap nm;
        nm.insert(std::make_pair(gcomm::UUID(1), node1));
        nm.insert(std::make_pair(gcomm::UUID(2), node2));
        gcomm::pc::Message msg(1, gcomm::pc::Message::PC_T_STATE, 0, nm);
        state_msgs.insert(std::make_pair(gcomm::UUID(2), msg));
    }
    {
        // Node3
        gcomm::pc::NodeMap nm;
        // Nodes n1 and n2 have previously been seen in prim view number 1
        nm.insert(std::make_pair(gcomm::UUID(1),
                                 gcomm::pc::Node(
                                     false, false, false, 0,
                                     gcomm::ViewId(V_PRIM, gcomm::UUID(1), 1),
                                     0, 1, 0)));
        nm.insert(std::make_pair(gcomm::UUID(2),
                                 gcomm::pc::Node(
                                     false, false, false, 0,
                                     gcomm::ViewId(V_PRIM, gcomm::UUID(1), 1),
                                     0, 1, 0)));
        nm.insert(std::make_pair(gcomm::UUID(3), node3));
        gcomm::pc::Message msg(1, gcomm::pc::Message::PC_T_STATE, 0, nm);
        state_msgs.insert(std::make_pair(gcomm::UUID(3), msg));
    }

    // Build n1 state in S_INSTALL.
    builder
        .conf(conf)
        .uuid(gcomm::UUID(1))
        .state_msgs(state_msgs)
        .current_view(current_view)
        .pc_view(pc_view)
        .instances(instances)
        .state(gcomm::pc::Proto::S_INSTALL);
    std::auto_ptr<gcomm::pc::Proto> p(builder.make_proto());
    DummyEvs devs(conf);
    DummyTop dtop(conf);
    gcomm::connect(&devs, p.get());
    gcomm::connect(p.get(), &dtop);

    // Deliver transitional EVS view where members are n1, n3 and
    // partitioned n2. After handling transitional view n1 is
    // expected to end up in non-primary.
    gcomm::View trans_view(0, gcomm::ViewId(V_TRANS, gcomm::UUID(1), 3));
    trans_view.add_member(gcomm::UUID(1), 0);
    trans_view.add_partitioned(gcomm::UUID(2), 0);
    trans_view.add_member(gcomm::UUID(3), 0);

    p->handle_view(trans_view);

    ck_assert(p->state() == gcomm::pc::Proto::S_TRANS);
    ck_assert(not p->prim());
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

    tc = tcase_create("test_pc_protocol_upgrade");
    tcase_add_test(tc, test_pc_protocol_upgrade);
    tcase_set_timeout(tc, 25);
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

    tc = tcase_create("test_trac_277");
    tcase_add_test(tc, test_trac_277);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_trac_622_638");
    tcase_add_test(tc, test_trac_622_638);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_weighted_quorum");
    tcase_add_test(tc, test_weighted_quorum);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_weighted_partitioning_1");
    tcase_add_test(tc, test_weighted_partitioning_1);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_weighted_partitioning_2");
    tcase_add_test(tc, test_weighted_partitioning_2);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_weight_change_partitioning_1");
    tcase_add_test(tc, test_weight_change_partitioning_1);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_weight_change_partitioning_2");
    tcase_add_test(tc, test_weight_change_partitioning_2);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_weight_change_joining");
    tcase_add_test(tc, test_weight_change_joining);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_weight_change_leaving");
    tcase_add_test(tc, test_weight_change_leaving);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_trac_762");
    tcase_add_test(tc, test_trac_762);
    tcase_set_timeout(tc, 15);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_join_split_cluster");
    tcase_add_test(tc, test_join_split_cluster);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_gh_92");
    tcase_add_test(tc, test_gh_92);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_prim_after_evict");
    tcase_add_test(tc, test_prim_after_evict);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_quorum_2_to_2_in_3_node_cluster");
    tcase_add_test(tc, test_quorum_2_to_2_in_3_node_cluster);
    suite_add_tcase(s, tc);

    return s;
}
