
#include "check_gcomm.hpp"

#include "pc_message.hpp"
#include "pc_proto.hpp"
#include "evs_proto.hpp"

#include "check_templ.hpp"
#include "check_trace.hpp"
#include "gcomm/pseudofd.hpp"
#include "gcomm/conf.hpp"

#include <check.h>

#include <list>
#include <cstdlib>
#include <vector>

using namespace std;

using namespace gcomm;

START_TEST(test_pc_messages)
{
    PCStateMessage pcs;
    PCInstMap& sim = pcs.get_inst_map();

    sim.insert(std::make_pair(UUID(0,0), 
                              PCInst(true, 6, 
                                     ViewId(V_PRIM,
                                            UUID(0, 0), 9), 
                                     42)));
    sim.insert(std::make_pair(UUID(0,0), 
                              PCInst(false, 88, ViewId(V_PRIM, 
                                                       UUID(0, 0), 3), 
                                     472)));
    sim.insert(std::make_pair(UUID(0,0), 
                              PCInst(true, 78, ViewId(V_PRIM,
                                                      UUID(0, 0), 87), 
                                     52)));
    
    size_t expt_size = 4 // hdr
        + 4              // seq
        + 4 + 3*(UUID::serial_size() + sizeof(uint32_t) + 4 + 20 + 8); // PCInstMap
    check_serialization(pcs, expt_size, PCStateMessage());
                       
    PCInstallMessage pci;
    PCInstMap& iim = pci.get_inst_map();

    iim.insert(std::make_pair(UUID(0,0), 
                              PCInst(true, 6, ViewId(V_PRIM,
                                                     UUID(0, 0), 9), 42)));
    iim.insert(std::make_pair(UUID(0,0), 
                              PCInst(false, 88, ViewId(V_NON_PRIM,
                                                       UUID(0, 0), 3), 472)));
    iim.insert(std::make_pair(UUID(0,0), 
                              PCInst(true, 78, ViewId(V_PRIM,
                                                      UUID(0, 0), 87), 52)));
    iim.insert(std::make_pair(UUID(0,0), 
                              PCInst(false, 457, ViewId(V_NON_PRIM,
                                                        UUID(0, 0), 37), 56)));
    
    expt_size = 4 // hdr
        + 4              // seq
        + 4 + 4*(UUID::serial_size() + sizeof(uint32_t) + 4 + 20 + 8); // PCInstMap
    check_serialization(pci, expt_size, PCInstallMessage());
    
    PCUserMessage pcu(7);
    
    expt_size = 4 + 4;
    check_serialization(pcu, expt_size, PCUserMessage(-1U));

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
    PCProto* pc;
    PCUser(const UUID& uuid_, DummyTransport *tp_, PCProto* pc_) :
        views(),
        uuid(uuid_),
        tp(tp_),
        pc(pc_)
    {
        gcomm::connect(tp, pc);
        gcomm::connect(pc, this);
    }
    
    void handle_up(int cid, const ReadBuf* rb, size_t roff,
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
    
};

void get_msg(ReadBuf* rb, PCMessage* msg, bool release = true)
{
    assert(msg != 0);
    if (rb == 0)
    {
        log_info << "get_msg: (null)";
    }
    else
    {
        fail_unless(msg->unserialize(rb->get_buf(), rb->get_len(), 0) != 0);
        log_info << "get_msg: " << msg->to_string();
        if (release)
            rb->release();
    }

}

void single_boot(PCUser* pu1)
{
    
    ProtoUpMeta sum1(pu1->uuid);

    View vt0(ViewId(V_TRANS, pu1->uuid, 0));
    vt0.add_member(pu1->uuid, "n1");
    ProtoUpMeta um1(UUID::nil(), ViewId(), &vt0);
    pu1->pc->connect(true);
    // pu1->pc->shift_to(PCProto::S_JOINING);
    pu1->pc->handle_up(0, 0, 0, um1);
    fail_unless(pu1->pc->get_state() == PCProto::S_TRANS);
    
    View vr1(ViewId(V_REG, pu1->uuid, 1));
    vr1.add_member(pu1->uuid, "n1");
    ProtoUpMeta um2(UUID::nil(), ViewId(), &vr1);
    pu1->pc->handle_up(0, 0, 0, um2);
    fail_unless(pu1->pc->get_state() == PCProto::S_STATES_EXCH);
    
    ReadBuf* rb = pu1->tp->get_out();
    fail_unless(rb != 0);
    PCMessage sm1;
    get_msg(rb, &sm1);
    fail_unless(sm1.get_type() == PCMessage::T_STATE);
    fail_unless(sm1.has_inst_map() == true);
    fail_unless(sm1.get_inst_map().size() == 1);
    {
        const PCInst& pi1 = PCInstMap::get_value(sm1.get_inst_map().begin());
        fail_unless(pi1.get_prim() == true);
        fail_unless(pi1.get_last_prim() == ViewId(V_PRIM, pu1->uuid, 0));
    }
    pu1->pc->handle_msg(sm1, 0, 0, sum1);
    fail_unless(pu1->pc->get_state() == PCProto::S_INSTALL);
    
    rb = pu1->tp->get_out();
    fail_unless(rb != 0);
    PCMessage im1;
    get_msg(rb, &im1);
    fail_unless(im1.get_type() == PCMessage::T_INSTALL);
    fail_unless(im1.has_inst_map() == true);
    fail_unless(im1.get_inst_map().size() == 1);
    {
        const PCInst& pi1 = PCInstMap::get_value(im1.get_inst_map().begin());
        fail_unless(pi1.get_prim() == true);
        fail_unless(pi1.get_last_prim() == ViewId(V_PRIM, pu1->uuid, 0));
    }
    pu1->pc->handle_msg(im1, 0, 0, sum1);
    fail_unless(pu1->pc->get_state() == PCProto::S_PRIM);
}

START_TEST(test_pc_view_changes_single)
{
    UUID uuid1(0, 0);

    PCProto pc1(uuid1);
    DummyTransport tp1;
    PCUser pu1(uuid1, &tp1, &pc1);    
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
    fail_unless(pu1->pc->get_state() == PCProto::S_TRANS);
    
    View t12(ViewId(V_TRANS, pu2->uuid, 0));
    t12.add_member(pu2->uuid, "n2");
    // pu2->pc->shift_to(PCProto::S_JOINING);
    pu2->pc->connect(false);
    pu2->pc->handle_view(t12);
    fail_unless(pu2->pc->get_state() == PCProto::S_TRANS);

    View r1(ViewId(V_REG, 
                   pu1->uuid, 
                   pu1->pc->get_current_view().get_id().get_seq() + 1));
    r1.add_member(pu1->uuid, "n1");
    r1.add_member(pu2->uuid, "n2");
    pu1->pc->handle_view(r1);
    fail_unless(pu1->pc->get_state() == PCProto::S_STATES_EXCH);

    pu2->pc->handle_view(r1);
    fail_unless(pu2->pc->get_state() == PCProto::S_STATES_EXCH);

    ReadBuf* rb = pu1->tp->get_out();
    fail_unless(rb != 0);
    PCMessage sm1;
    get_msg(rb, &sm1);
    fail_unless(sm1.get_type() == PCMessage::T_STATE);

    rb = pu2->tp->get_out();
    fail_unless(rb != 0);
    PCMessage sm2;
    get_msg(rb, &sm2);
    fail_unless(sm2.get_type() == PCMessage::T_STATE);

    rb = pu1->tp->get_out();
    fail_unless(rb == 0);
    rb = pu2->tp->get_out();
    fail_unless(rb == 0);

    pu1->pc->handle_msg(sm1, 0, 0, pum1);
    rb = pu1->tp->get_out();
    fail_unless(rb == 0);
    fail_unless(pu1->pc->get_state() == PCProto::S_STATES_EXCH);
    pu1->pc->handle_msg(sm2, 0, 0, pum2);
    fail_unless(pu1->pc->get_state() == PCProto::S_INSTALL);

    pu2->pc->handle_msg(sm1, 0, 0, pum1);
    rb = pu2->tp->get_out();
    fail_unless(rb == 0);
    fail_unless(pu2->pc->get_state() == PCProto::S_STATES_EXCH);
    pu2->pc->handle_msg(sm2, 0, 0, pum2);
    fail_unless(pu2->pc->get_state() == PCProto::S_INSTALL);

    PCMessage im1;
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
    fail_unless(im1.get_type() == PCMessage::T_INSTALL);
    
    fail_unless(pu1->tp->get_out() == 0);
    fail_unless(pu2->tp->get_out() == 0);

    ProtoUpMeta ipum(imsrc);
    pu1->pc->handle_msg(im1, 0, 0, ipum);
    fail_unless(pu1->pc->get_state() == PCProto::S_PRIM);

    pu2->pc->handle_msg(im1, 0, 0, ipum);
    fail_unless(pu2->pc->get_state() == PCProto::S_PRIM);
}

START_TEST(test_pc_view_changes_double)
{
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    PCProto pc1(uuid1);
    DummyTransport tp1;
    PCUser pu1(uuid1, &tp1, &pc1);
    single_boot(&pu1);
    
    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    PCProto pc2(uuid2);
    DummyTransport tp2;
    PCUser pu2(uuid2, &tp2, &pc2);
    
    double_boot(&pu1, &pu2);
    
    ReadBuf* rb;
    
    View tnp(ViewId(V_TRANS, pu1.pc->get_current_view().get_id()));
    tnp.add_member(uuid1, "n1");
    pu1.pc->handle_view(tnp);
    fail_unless(pu1.pc->get_state() == PCProto::S_NON_PRIM);
    
    View tpv2(ViewId(V_TRANS, pu2.pc->get_current_view().get_id()));
    tpv2.add_member(uuid2, "n2");
    tpv2.add_left(uuid1, "n1");
    pu2.pc->handle_view(tpv2);
    fail_unless(pu2.pc->get_state() == PCProto::S_TRANS);
    fail_unless(pu2.tp->get_out() == 0);

    View rp2(ViewId(V_REG, uuid2, 
                                 pu1.pc->get_current_view().get_id().get_seq() + 1));
    rp2.add_member(uuid2, "n2");
    rp2.add_left(uuid1, "n1");
    pu2.pc->handle_view(rp2);
    fail_unless(pu2.pc->get_state() == PCProto::S_STATES_EXCH);
    rb = pu2.tp->get_out();
    fail_unless(rb != 0);
    PCMessage sm2;
    get_msg(rb, &sm2);
    fail_unless(sm2.get_type() == PCMessage::T_STATE);
    fail_unless(pu2.tp->get_out() == 0);
    pu2.pc->handle_msg(sm2, 0, 0, pum2);
    fail_unless(pu2.pc->get_state() == PCProto::S_INSTALL);
    rb = pu2.tp->get_out();
    fail_unless(rb != 0);
    PCMessage im2;
    get_msg(rb, &im2);
    fail_unless(im2.get_type() == PCMessage::T_INSTALL);
    pu2.pc->handle_msg(im2, 0, 0, pum2);
    fail_unless(pu2.pc->get_state() == PCProto::S_PRIM);

}
END_TEST

/* Test that UUID ordering does not matter when starting nodes */
START_TEST(test_pc_view_changes_reverse)
{
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    PCProto pc1(uuid1);
    DummyTransport tp1;
    PCUser pu1(uuid1, &tp1, &pc1);

    
    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    PCProto pc2(uuid2);
    DummyTransport tp2;
    PCUser pu2(uuid2, &tp2, &pc2);

    single_boot(&pu2);    
    double_boot(&pu2, &pu1);
}
END_TEST



START_TEST(test_pc_state1)
{
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    PCProto pc1(uuid1);
    DummyTransport tp1;
    PCUser pu1(uuid1, &tp1, &pc1);
    single_boot(&pu1);
    
    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    PCProto pc2(uuid2);
    DummyTransport tp2;
    PCUser pu2(uuid2, &tp2, &pc2);
    
    // n1: PRIM -> TRANS -> STATES_EXCH -> RTR -> PRIM
    // n2: JOINING -> STATES_EXCH -> RTR -> PRIM
    double_boot(&pu1, &pu2);    
    
    fail_unless(pu1.pc->get_state() == PCProto::S_PRIM);
    fail_unless(pu2.pc->get_state() == PCProto::S_PRIM);
    
    // PRIM -> TRANS -> STATES_EXCH -> RTR -> TRANS -> STATES_EXCH -> RTR -> PRIM
    View tr1(ViewId(V_TRANS, pu1.pc->get_current_view().get_id()));
    tr1.add_member(uuid1, "n1");
    tr1.add_member(uuid2, "n2");
    pu1.pc->handle_view(tr1);
    pu2.pc->handle_view(tr1);
    
    fail_unless(pu1.pc->get_state() == PCProto::S_TRANS);
    fail_unless(pu2.pc->get_state() == PCProto::S_TRANS);    

    fail_unless(pu1.tp->get_out() == 0);
    fail_unless(pu2.tp->get_out() == 0);

    View reg2(ViewId(V_REG, uuid1,
                     pu1.pc->get_current_view().get_id().get_seq() + 1));
    reg2.add_member(uuid1, "n1");
    reg2.add_member(uuid2, "n2");
    pu1.pc->handle_view(reg2);
    pu2.pc->handle_view(reg2);
    
    fail_unless(pu1.pc->get_state() == PCProto::S_STATES_EXCH);
    fail_unless(pu2.pc->get_state() == PCProto::S_STATES_EXCH);    
    
    PCMessage msg;
    get_msg(pu1.tp->get_out(), &msg);
    pu1.pc->handle_msg(msg, 0, 0, pum1);
    pu2.pc->handle_msg(msg, 0, 0, pum1);

    fail_unless(pu1.pc->get_state() == PCProto::S_STATES_EXCH);
    fail_unless(pu2.pc->get_state() == PCProto::S_STATES_EXCH);    

    get_msg(pu2.tp->get_out(), &msg);
    pu1.pc->handle_msg(msg, 0, 0, pum2);
    pu2.pc->handle_msg(msg, 0, 0, pum2);

    fail_unless(pu1.pc->get_state() == PCProto::S_INSTALL);
    fail_unless(pu2.pc->get_state() == PCProto::S_INSTALL);    
    
    View tr2(ViewId(V_TRANS, pu1.pc->get_current_view().get_id())); 
    tr2.add_member(uuid1, "n1");
    tr2.add_member(uuid2, "n2");

    pu1.pc->handle_view(tr2);
    pu2.pc->handle_view(tr2);


    fail_unless(pu1.pc->get_state() == PCProto::S_TRANS);
    fail_unless(pu2.pc->get_state() == PCProto::S_TRANS);    

    PCMessage im;
    
    if (uuid1 < uuid2)
    {
        get_msg(pu1.tp->get_out(), &im);
        pu1.pc->handle_msg(im, 0, 0, pum1);
        pu2.pc->handle_msg(im, 0, 0, pum1);
    }
    else
    {
        get_msg(pu2.tp->get_out(), &im);
        pu1.pc->handle_msg(im, 0, 0, pum2);
        pu2.pc->handle_msg(im, 0, 0, pum2);
    }


    fail_unless(pu1.pc->get_state() == PCProto::S_TRANS);
    fail_unless(pu2.pc->get_state() == PCProto::S_TRANS);    
    

    View reg3(ViewId(V_REG, uuid1,
                     pu1.pc->get_current_view().get_id().get_seq() + 1));
    
    reg3.add_member(uuid1, "n1");
    reg3.add_member(uuid2, "n2");

    pu1.pc->handle_view(reg3);
    pu2.pc->handle_view(reg3);
    
    fail_unless(pu1.pc->get_state() == PCProto::S_STATES_EXCH);
    fail_unless(pu2.pc->get_state() == PCProto::S_STATES_EXCH);    

    get_msg(pu1.tp->get_out(), &msg);
    pu1.pc->handle_msg(msg, 0, 0, pum1);
    pu2.pc->handle_msg(msg, 0, 0, pum1);

    fail_unless(pu1.pc->get_state() == PCProto::S_STATES_EXCH);
    fail_unless(pu2.pc->get_state() == PCProto::S_STATES_EXCH);    

    get_msg(pu2.tp->get_out(), &msg);
    pu1.pc->handle_msg(msg, 0, 0, pum2);
    pu2.pc->handle_msg(msg, 0, 0, pum2);
    
    fail_unless(pu1.pc->get_state() == PCProto::S_INSTALL);
    fail_unless(pu2.pc->get_state() == PCProto::S_INSTALL);
    
    if (uuid1 < uuid2)
    {
        get_msg(pu1.tp->get_out(), &im);
        pu1.pc->handle_msg(im, 0, 0, pum1);
        pu2.pc->handle_msg(im, 0, 0, pum1);
    }
    else
    {
        get_msg(pu2.tp->get_out(), &im);
        pu1.pc->handle_msg(im, 0, 0, pum2);
        pu2.pc->handle_msg(im, 0, 0, pum2);
    }

    fail_unless(pu1.pc->get_state() == PCProto::S_PRIM);
    fail_unless(pu2.pc->get_state() == PCProto::S_PRIM);

}
END_TEST

START_TEST(test_pc_state2)
{
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    PCProto pc1(uuid1);
    DummyTransport tp1;
    PCUser pu1(uuid1, &tp1, &pc1);
    single_boot(&pu1);
    
    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    PCProto pc2(uuid2);
    DummyTransport tp2;
    PCUser pu2(uuid2, &tp2, &pc2);
    
    // n1: PRIM -> TRANS -> STATES_EXCH -> RTR -> PRIM
    // n2: JOINING -> STATES_EXCH -> RTR -> PRIM
    double_boot(&pu1, &pu2);    
    
    fail_unless(pu1.pc->get_state() == PCProto::S_PRIM);
    fail_unless(pu2.pc->get_state() == PCProto::S_PRIM);
    
    // PRIM -> TRANS -> STATES_EXCH -> TRANS -> STATES_EXCH -> RTR -> PRIM
    View tr1(ViewId(V_TRANS, pu1.pc->get_current_view().get_id()));
    tr1.add_member(uuid1, "n1");
    tr1.add_member(uuid2, "n2");
    pu1.pc->handle_view(tr1);
    pu2.pc->handle_view(tr1);

    fail_unless(pu1.pc->get_state() == PCProto::S_TRANS);
    fail_unless(pu2.pc->get_state() == PCProto::S_TRANS);    

    fail_unless(pu1.tp->get_out() == 0);
    fail_unless(pu2.tp->get_out() == 0);

    View reg2(ViewId(V_REG, uuid1,
                     pu1.pc->get_current_view().get_id().get_seq() + 1));
    reg2.add_member(uuid1, "n1");
    reg2.add_member(uuid2, "n2");
    pu1.pc->handle_view(reg2);
    pu2.pc->handle_view(reg2);

    fail_unless(pu1.pc->get_state() == PCProto::S_STATES_EXCH);
    fail_unless(pu2.pc->get_state() == PCProto::S_STATES_EXCH);    


    
    View tr2(ViewId(V_TRANS, pu1.pc->get_current_view().get_id())); 
    tr2.add_member(uuid1, "n1");
    tr2.add_member(uuid2, "n2");

    pu1.pc->handle_view(tr2);
    pu2.pc->handle_view(tr2);


    fail_unless(pu1.pc->get_state() == PCProto::S_TRANS);
    fail_unless(pu2.pc->get_state() == PCProto::S_TRANS);    

    PCMessage msg;
    get_msg(pu1.tp->get_out(), &msg);
    pu1.pc->handle_msg(msg, 0, 0, pum1);
    pu2.pc->handle_msg(msg, 0, 0, pum1);
    
    fail_unless(pu1.pc->get_state() == PCProto::S_TRANS);
    fail_unless(pu2.pc->get_state() == PCProto::S_TRANS);    

    get_msg(pu2.tp->get_out(), &msg);
    pu1.pc->handle_msg(msg, 0, 0, pum2);
    pu2.pc->handle_msg(msg, 0, 0, pum2);

    fail_unless(pu1.pc->get_state() == PCProto::S_TRANS);
    fail_unless(pu2.pc->get_state() == PCProto::S_TRANS);    
    

    View reg3(ViewId(V_REG, uuid1,
                     pu1.pc->get_current_view().get_id().get_seq() + 1));
    
    reg3.add_member(uuid1, "n1");
    reg3.add_member(uuid2, "n2");

    pu1.pc->handle_view(reg3);
    pu2.pc->handle_view(reg3);
    
    fail_unless(pu1.pc->get_state() == PCProto::S_STATES_EXCH);
    fail_unless(pu2.pc->get_state() == PCProto::S_STATES_EXCH);    

    get_msg(pu1.tp->get_out(), &msg);
    pu1.pc->handle_msg(msg, 0, 0, pum1);
    pu2.pc->handle_msg(msg, 0, 0, pum1);

    fail_unless(pu1.pc->get_state() == PCProto::S_STATES_EXCH);
    fail_unless(pu2.pc->get_state() == PCProto::S_STATES_EXCH);    

    get_msg(pu2.tp->get_out(), &msg);
    pu1.pc->handle_msg(msg, 0, 0, pum2);
    pu2.pc->handle_msg(msg, 0, 0, pum2);
    
    fail_unless(pu1.pc->get_state() == PCProto::S_INSTALL);
    fail_unless(pu2.pc->get_state() == PCProto::S_INSTALL);
    
    PCMessage im;
    if (uuid1 < uuid2)
    {
        get_msg(pu1.tp->get_out(), &im);
        pu1.pc->handle_msg(im, 0, 0, pum1);
        pu2.pc->handle_msg(im, 0, 0, pum1);
    }
    else
    {
        get_msg(pu2.tp->get_out(), &im);
        pu1.pc->handle_msg(im, 0, 0, pum2);
        pu2.pc->handle_msg(im, 0, 0, pum2);
    }

    fail_unless(pu1.pc->get_state() == PCProto::S_PRIM);
    fail_unless(pu2.pc->get_state() == PCProto::S_PRIM);

}
END_TEST

START_TEST(test_pc_state3)
{
    log_info << "START";
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    PCProto pc1(uuid1);
    DummyTransport tp1;
    PCUser pu1(uuid1, &tp1, &pc1);
    single_boot(&pu1);
    
    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    PCProto pc2(uuid2);
    DummyTransport tp2;
    PCUser pu2(uuid2, &tp2, &pc2);
    
    // n1: PRIM -> TRANS -> STATES_EXCH -> RTR -> PRIM
    // n2: JOINING -> STATES_EXCH -> RTR -> PRIM
    double_boot(&pu1, &pu2);    
    
    fail_unless(pu1.pc->get_state() == PCProto::S_PRIM);
    fail_unless(pu2.pc->get_state() == PCProto::S_PRIM);
    
    // PRIM -> NON_PRIM -> STATES_EXCH -> RTR -> NON_PRIM -> STATES_EXCH -> ...
    //      -> NON_PRIM -> STATES_EXCH -> RTR -> NON_PRIM
    View tr11(ViewId(V_TRANS, pu1.pc->get_current_view().get_id()));
    tr11.add_member(uuid1, "n1");
    pu1.pc->handle_view(tr11);
    
    View tr12(ViewId(V_TRANS, pu1.pc->get_current_view().get_id()));
    tr12.add_member(uuid2, "n2");
    pu2.pc->handle_view(tr12);
    
    fail_unless(pu1.pc->get_state() == PCProto::S_NON_PRIM);
    fail_unless(pu2.pc->get_state() == PCProto::S_NON_PRIM);    
    
    fail_unless(pu1.tp->get_out() == 0);
    fail_unless(pu2.tp->get_out() == 0);
    
    View reg21(ViewId(V_REG, uuid1,
                      pu1.pc->get_current_view().get_id().get_seq() + 1));
    reg21.add_member(uuid1, "n1");
    pu1.pc->handle_view(reg21);
    fail_unless(pu1.pc->get_state() == PCProto::S_STATES_EXCH);
    
    View reg22(ViewId(V_REG, uuid2,
                      pu2.pc->get_current_view().get_id().get_seq() + 1));
    reg22.add_member(uuid2, "n2");
    pu2.pc->handle_view(reg22);
    fail_unless(pu2.pc->get_state() == PCProto::S_STATES_EXCH);    
    

    PCMessage msg;
    get_msg(pu1.tp->get_out(), &msg);
    pu1.pc->handle_msg(msg, 0, 0, pum1);

    get_msg(pu2.tp->get_out(), &msg);
    pu2.pc->handle_msg(msg, 0, 0, pum2);
    
    fail_unless(pu1.pc->get_state() == PCProto::S_NON_PRIM);
    fail_unless(pu2.pc->get_state() == PCProto::S_NON_PRIM);
    
    
    
    View tr21(ViewId(V_TRANS, pu1.pc->get_current_view().get_id())); 
    tr21.add_member(uuid1, "n1");
    pu1.pc->handle_view(tr21);
    
    View tr22(ViewId(V_TRANS, pu2.pc->get_current_view().get_id())); 
    tr22.add_member(uuid2, "n2");
    pu2.pc->handle_view(tr22);
    
    fail_unless(pu1.pc->get_state() == PCProto::S_TRANS);
    fail_unless(pu2.pc->get_state() == PCProto::S_TRANS);

    fail_unless(pu1.tp->get_out() == 0);
    fail_unless(pu2.tp->get_out() == 0);

    View reg3(ViewId(V_REG, uuid1,
                     pu1.pc->get_current_view().get_id().get_seq() + 1));
    reg3.add_member(uuid1, "n1");
    reg3.add_member(uuid2, "n2");

    pu1.pc->handle_view(reg3);
    pu2.pc->handle_view(reg3);

    fail_unless(pu1.pc->get_state() == PCProto::S_STATES_EXCH);
    fail_unless(pu2.pc->get_state() == PCProto::S_STATES_EXCH);

    get_msg(pu1.tp->get_out(), &msg);
    pu1.pc->handle_msg(msg, 0, 0, pum1);
    pu2.pc->handle_msg(msg, 0, 0, pum1);

    fail_unless(pu1.pc->get_state() == PCProto::S_STATES_EXCH);
    fail_unless(pu2.pc->get_state() == PCProto::S_STATES_EXCH);    

    get_msg(pu2.tp->get_out(), &msg);
    pu1.pc->handle_msg(msg, 0, 0, pum2);
    pu2.pc->handle_msg(msg, 0, 0, pum2);
    
    fail_unless(pu1.pc->get_state() == PCProto::S_INSTALL);
    fail_unless(pu2.pc->get_state() == PCProto::S_INSTALL);

    PCMessage im;
    if (uuid1 < uuid2)
    {
        get_msg(pu1.tp->get_out(), &im);
        pu1.pc->handle_msg(im, 0, 0, pum1);
        pu2.pc->handle_msg(im, 0, 0, pum1);
    }
    else
    {
        get_msg(pu2.tp->get_out(), &im);
        pu1.pc->handle_msg(im, 0, 0, pum2);
        pu2.pc->handle_msg(im, 0, 0, pum2);
    }

    fail_unless(pu1.pc->get_state() == PCProto::S_PRIM);
    fail_unless(pu2.pc->get_state() == PCProto::S_PRIM);    

}
END_TEST

START_TEST(test_pc_conflicting_prims)
{
    log_info << "START";
    UUID uuid1(1);
    ProtoUpMeta pum1(uuid1);
    PCProto pc1(uuid1);
    DummyTransport tp1;
    PCUser pu1(uuid1, &tp1, &pc1);
    single_boot(&pu1);
    
    UUID uuid2(2);
    ProtoUpMeta pum2(uuid2);
    PCProto pc2(uuid2);
    DummyTransport tp2;
    PCUser pu2(uuid2, &tp2, &pc2);
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
    
    PCMessage msg1, msg2;

    /* First node must discard msg2 and stay in states exch waiting for
     * trans view */
    get_msg(pu1.tp->get_out(), &msg1);
    get_msg(pu2.tp->get_out(), &msg2);
    fail_unless(pu1.pc->get_state() == PCProto::S_STATES_EXCH);
    
    pu1.pc->handle_msg(msg1, 0, 0, pum1);
    pu1.pc->handle_msg(msg2, 0, 0, pum2);
    
    /* Second node must abort */
    try
    {
        pu2.pc->handle_msg(msg1, 0, 0, pum1);
        fail("not aborted");
    }
    catch (FatalException& e)
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
    pu1.pc->handle_msg(msg1, 0, 0, pum1);

    get_msg(pu1.tp->get_out(), &msg1);
    pu1.pc->handle_msg(msg1, 0, 0, pum1);
    
    fail_unless(pu1.pc->get_state() == PCProto::S_PRIM);

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

static DummyNode* create_dummy_node(size_t idx, 
                                    const string& inactive_timeout = "PT1H",
                                    const string& retrans_period = "PT1H")
{
    const string conf = "evs://?" + Conf::EvsParamViewForgetTimeout + "=PT1H&"
        + Conf::EvsParamInactiveTimeout + "=" + inactive_timeout + "&"
        + Conf::EvsParamInactiveCheckPeriod + "=PT0.01S&"
        + Conf::EvsParamConsensusTimeout + "=PT1H&"
        + Conf::EvsParamRetransPeriod + "=" + retrans_period + "&"
        + Conf::EvsParamJoinRetransPeriod + "=" + retrans_period;
    list<Protolay*> protos;
    try
    {
        UUID uuid(static_cast<int32_t>(idx));
        protos.push_back(new DummyTransport(uuid, false));
        protos.push_back(new evs::Proto(uuid, conf));
        protos.push_back(new PCProto(uuid));
        return new DummyNode(idx, protos);
    }
    catch (...)
    {
        for_each(protos.begin(), protos.end(), DeleteObjectOp());
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
    log_info << "START";
    size_t n_nodes(5);
    vector<DummyNode*> dn;
    PropagationMatrix prop;
    const string inactive_timeout("PT0.3S");
    const string retrans_period("PT0.01S");
    uint32_t view_seq = 0;
    
    for (size_t i = 0; i < n_nodes; ++i)
    {
        dn.push_back(create_dummy_node(i + 1, inactive_timeout, retrans_period));
        log_info << "i " << i;
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
    for_each(dn.begin(), dn.end(), DeleteObjectOp());
}
END_TEST



START_TEST(test_pc_split_merge_w_user_msg)
{
    log_info << "START";
    size_t n_nodes(5);
    vector<DummyNode*> dn;
    PropagationMatrix prop;
    const string inactive_timeout("PT0.3S");
    const string retrans_period("PT0.01S");
    uint32_t view_seq = 0;
    
    for (size_t i = 0; i < n_nodes; ++i)
    {
        dn.push_back(create_dummy_node(i + 1, inactive_timeout, retrans_period));
        log_info << "i " << i;
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
    for_each(dn.begin(), dn.end(), DeleteObjectOp());
}
END_TEST


class PCUser2 : public Toplay, EventContext
{
    Transport* tp;
    EventLoop* event_loop;
    bool sending;
    PseudoFd pfd;
    uint8_t my_type;
    bool send;
    PCUser2(const PCUser2&);
    void operator=(const PCUser2);
public:
    PCUser2(const string& uri, EventLoop* el, const bool send_ = true) :
        tp(0),
        event_loop(el),
        sending(false),
        pfd(),
        my_type(static_cast<uint8_t>(1 + ::rand()%4)),
        send(send_)
    {
        tp = Transport::create(uri, el);
        gcomm::connect(tp, this);
        event_loop->insert(pfd.get(), this);
    }
    
    ~PCUser2()
    {
        event_loop->erase(pfd.get());
        gcomm::disconnect(tp, this);
        delete tp;
    }
    
    void start()
    {
        tp->connect();
    }
    
    void stop()
    {
        sending = false;
        tp->close();
    }

    void handle_up(int cid, const ReadBuf* rb, size_t roff,
                   const ProtoUpMeta& um)
    {

        if (um.has_view())
        {
            const View& view(um.get_view());
            log_info << view;
            if (view.get_type() == V_PRIM && send == true)
            {
                sending = true;
                event_loop->queue_event(pfd.get(), Event(Event::E_USER,
                                                         Time(Time::now() + Period("PT0.05S"))));
            }
        }
        else
        {
            // log_debug << "received message: " << um.get_to_seq();
            if (um.get_source() == tp->get_uuid())
            {
                fail_unless(um.get_user_type() == my_type);
            }
        }
    }
    
    void handle_event(const int fd, const Event& ev)
    {
        if (sending)
        {
            const byte_t buf[8] = "pcmsg12";
            WriteBuf wb(buf, sizeof(buf));
            ProtoDownMeta dm(my_type);
            int ret = pass_down(&wb, dm);
            if (ret != 0 && ret != EAGAIN)
            {
                log_warn << "pass_down(): " << strerror(ret);
            }
            
            event_loop->queue_event(pfd.get(), Event(Event::E_USER,
                                                     (Time::now() + Period("PT0.01"))));
        }
    }
};

START_TEST(test_pc_transport)
{
    EventLoop el;
    PCUser2 pu1("gcomm+pc://?gmcast.listen_addr=gcomm+tcp://127.0.0.1:10001&gmcast.group=pc&node.name=n1", &el);
    PCUser2 pu2("gcomm+pc://localhost:10001?gmcast.group=pc&gmcast.listen_addr=gcomm+tcp://localhost:10002&node.name=n2", &el);
    PCUser2 pu3("gcomm+pc://localhost:10001?gmcast.group=pc&gmcast.listen_addr=gcomm+tcp://localhost:10003&node.name=n3", &el);

    pu1.start();

    Time stop = Time::now() + Period("PT10S");
    do
    {
        el.poll(50);
    }
    while (Time::now() < stop);
    
    pu2.start();

    stop = Time::now() + Period("PT5S");
    do
    {
        el.poll(50);
    }
    while (Time::now() < stop);

    pu3.start();

    stop = Time::now() + Period("PT5S");
    do
    {
        el.poll(50);
    }
    while (Time::now() < stop);

    pu2.stop();

    stop = Time::now() + Period("PT5S");
    do
    {
        el.poll(50);
    }
    while (Time::now() < stop);

    pu1.stop();
    stop = Time::now() + Period("PT5S");
    do
    {
        el.poll(50);
    }
    while (Time::now() < stop);

    pu3.stop();

}
END_TEST



static bool skip = false;

Suite* pc_suite()
{
    Suite* s = suite_create("pc");
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

    tc = tcase_create("test_pc_split_merge");
    tcase_add_test(tc, test_pc_split_merge);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_pc_split_merge_w_user_msg");
    tcase_add_test(tc, test_pc_split_merge_w_user_msg);
    suite_add_tcase(s, tc);

    if (skip == true) return s;

    tc = tcase_create("test_pc_transport");
    tcase_add_test(tc, test_pc_transport);
    tcase_set_timeout(tc, 120);
    suite_add_tcase(s, tc);

    return s;
}
