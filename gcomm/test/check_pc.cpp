
#include "check_gcomm.hpp"

#include "pc_message.hpp"
#include "pc_proto.hpp"

#include "check_templ.hpp"
#include <check.h>

#include <list>
using std::list;

using namespace gcomm;

START_TEST(test_pc_messages)
{
    PCStateMessage pcs;
    PCInstMap& sim = pcs.get_inst_map();

    sim.insert(make_pair(UUID(0,0), PCInst(true, 6, ViewId(UUID(0, 0), 9), 42)));
    sim.insert(make_pair(UUID(0,0), PCInst(false, 88, ViewId(UUID(0, 0), 3), 472)));
    sim.insert(make_pair(UUID(0,0), PCInst(true, 78, ViewId(UUID(0, 0), 87), 52)));
    
    size_t expt_size = 4 // hdr
        + 4              // seq
        + 4 + 3*(UUID::size() + 4 + 4 + 20 + 8); // PCInstMap
    check_serialization(pcs, expt_size, PCStateMessage());
                       
    PCInstallMessage pci;
    PCInstMap& iim = pci.get_inst_map();

    iim.insert(make_pair(UUID(0,0), PCInst(true, 6, ViewId(UUID(0, 0), 9), 42)));
    iim.insert(make_pair(UUID(0,0), PCInst(false, 88, ViewId(UUID(0, 0), 3), 472)));
    iim.insert(make_pair(UUID(0,0), PCInst(true, 78, ViewId(UUID(0, 0), 87), 52)));
    iim.insert(make_pair(UUID(0,0), PCInst(false, 457, ViewId(UUID(0, 0), 37), 56)));

    expt_size = 4 // hdr
        + 4              // seq
        + 4 + 4*(UUID::size() + 4 + 4 + 20 + 8); // PCInstMap
    LOG_INFO(make_int(expt_size).to_string() + " - " + make_int(pci.size()).to_string());
    check_serialization(pci, expt_size, PCInstallMessage());

    PCUserMessage pcu(7);
    
    expt_size = 4 + 4;
    check_serialization(pcu, expt_size, PCUserMessage(-1));

}
END_TEST

class PCUser : public Toplay
{
    list<View> views;
public:
    UUID uuid;
    DummyTransport* tp;
    PCProto* pc;
    PCUser(const UUID& uuid_, DummyTransport *tp_, PCProto* pc_) :
        uuid(uuid_),
        tp(tp_),
        pc(pc_)
    {
        gcomm::connect(tp, pc);
        gcomm::connect(pc, this);
    }
    
    void handle_up(const int cid, const ReadBuf* rb, const size_t roff,
                   const ProtoUpMeta* um)
    {
        const View* v = um->get_view();
        if (v)
        {
            LOG_INFO(v->to_string());
            fail_unless(v->get_type() == View::V_PRIM ||
                        v->get_type() == View::V_NON_PRIM);
            views.push_back(View(*v));
        }
    }
    
};

void get_msg(ReadBuf* rb, PCMessage* msg, bool release = true)
{
    assert(msg != 0);
    if (rb == 0)
    {
        LOG_INFO("get_msg: (null)");
    }
    else
    {
        fail_unless(msg->read(rb->get_buf(), rb->get_len(), 0) != 0);
        LOG_INFO("get_msg: " + msg->to_string());
        if (release)
            rb->release();
    }

}

void single_boot(PCUser* pu1)
{
    
    ProtoUpMeta sum1(pu1->uuid);

    View vt0(View::V_TRANS, ViewId(pu1->uuid, 0));
    vt0.add_member(pu1->uuid, "n1");
    ProtoUpMeta um1(&vt0);
    pu1->pc->shift_to(PCProto::S_JOINING);
    pu1->pc->handle_up(0, 0, 0, &um1);
    fail_unless(pu1->pc->get_state() == PCProto::S_JOINING);
    
    View vr1(View::V_REG, ViewId(pu1->uuid, 1));
    vr1.add_member(pu1->uuid, "n1");
    ProtoUpMeta um2(&vr1);
    pu1->pc->handle_up(0, 0, 0, &um2);
    fail_unless(pu1->pc->get_state() == PCProto::S_STATES_EXCH);
    
    ReadBuf* rb = pu1->tp->get_out();
    fail_unless(rb != 0);
    PCMessage sm1;
    get_msg(rb, &sm1);
    fail_unless(sm1.get_type() == PCMessage::T_STATE);
    fail_unless(sm1.has_inst_map() == true);
    fail_unless(sm1.get_inst_map().length() == 1);
    {
        const PCInst& pi1 = PCInstMap::get_instance(sm1.get_inst_map().begin());
        fail_unless(pi1.get_prim() == true);
        fail_unless(pi1.get_last_prim() == ViewId(pu1->uuid, 0));
    }
    pu1->pc->handle_msg(sm1, 0, 0, &sum1);
    fail_unless(pu1->pc->get_state() == PCProto::S_RTR);
    
    rb = pu1->tp->get_out();
    fail_unless(rb != 0);
    PCMessage im1;
    get_msg(rb, &im1);
    fail_unless(im1.get_type() == PCMessage::T_INSTALL);
    fail_unless(im1.has_inst_map() == true);
    fail_unless(im1.get_inst_map().length() == 1);
    {
        const PCInst& pi1 = PCInstMap::get_instance(im1.get_inst_map().begin());
        fail_unless(pi1.get_prim() == true);
        fail_unless(pi1.get_last_prim() == ViewId(pu1->uuid, 0));
    }
    pu1->pc->handle_msg(im1, 0, 0, &sum1);
    fail_unless(pu1->pc->get_state() == PCProto::S_PRIM);
}

START_TEST(test_pc_view_changes_single)
{
    UUID uuid1(0, 0);

    PCProto pc1(uuid1, 0, 0, true);
    DummyTransport tp1;
    PCUser pu1(uuid1, &tp1, &pc1);    
    single_boot(&pu1);

}
END_TEST

START_TEST(test_pc_view_changes_double)
{
    UUID uuid1(0, 0);
    ProtoUpMeta pum1(uuid1);
    PCProto pc1(uuid1, 0, 0, true);
    DummyTransport tp1;
    PCUser pu1(uuid1, &tp1, &pc1);
    single_boot(&pu1);
    
    UUID uuid2(0, 0);
    ProtoUpMeta pum2(uuid2);
    PCProto pc2(uuid2, 0, 0, false);
    DummyTransport tp2;
    PCUser pu2(uuid2, &tp2, &pc2);
    
    View t11(View::V_TRANS, pu1.pc->get_current_view().get_id());
    t11.add_member(uuid1, "n1");
    pu1.pc->handle_view(t11);
    fail_unless(pu1.pc->get_state() == PCProto::S_PRIM);
    
    View t12(View::V_TRANS, ViewId(uuid2, 0));
    t12.add_member(uuid2, "n2");
    pu2.pc->shift_to(PCProto::S_JOINING);
    pu2.pc->handle_view(t12);
    fail_unless(pu2.pc->get_state() == PCProto::S_JOINING);

    View r1(View::V_REG, ViewId(uuid1, pu1.pc->get_current_view().get_id().get_seq() + 1));
    r1.add_member(uuid1, "n1");
    r1.add_member(uuid2, "n2");
    pu1.pc->handle_view(r1);
    fail_unless(pu1.pc->get_state() == PCProto::S_STATES_EXCH);

    pu2.pc->handle_view(r1);
    fail_unless(pu2.pc->get_state() == PCProto::S_STATES_EXCH);

    ReadBuf* rb = pu1.tp->get_out();
    fail_unless(rb != 0);
    PCMessage sm1;
    get_msg(rb, &sm1);
    fail_unless(sm1.get_type() == PCMessage::T_STATE);

    rb = pu2.tp->get_out();
    fail_unless(rb != 0);
    PCMessage sm2;
    get_msg(rb, &sm2);
    fail_unless(sm2.get_type() == PCMessage::T_STATE);

    rb = pu1.tp->get_out();
    fail_unless(rb == 0);
    rb = pu2.tp->get_out();
    fail_unless(rb == 0);

    pu1.pc->handle_msg(sm1, 0, 0, &pum1);
    rb = pu1.tp->get_out();
    fail_unless(rb == 0);
    fail_unless(pu1.pc->get_state() == PCProto::S_STATES_EXCH);
    pu1.pc->handle_msg(sm2, 0, 0, &pum2);
    fail_unless(pu1.pc->get_state() == PCProto::S_RTR);

    pu2.pc->handle_msg(sm1, 0, 0, &pum1);
    rb = pu2.tp->get_out();
    fail_unless(rb == 0);
    fail_unless(pu2.pc->get_state() == PCProto::S_STATES_EXCH);
    pu2.pc->handle_msg(sm2, 0, 0, &pum2);
    fail_unless(pu2.pc->get_state() == PCProto::S_RTR);

    PCMessage im1;
    UUID imsrc;
    if (uuid1 < uuid2)
    {
        rb = pu1.tp->get_out();
        imsrc = uuid1;
    }
    else
    {
        rb = pu2.tp->get_out();
        imsrc = uuid2;
    }

    fail_unless(rb != 0);
    get_msg(rb, &im1);
    fail_unless(im1.get_type() == PCMessage::T_INSTALL);
    
    fail_unless(pu1.tp->get_out() == 0);
    fail_unless(pu2.tp->get_out() == 0);

    ProtoUpMeta ipum(imsrc);
    pu1.pc->handle_msg(im1, 0, 0, &ipum);
    fail_unless(pu1.pc->get_state() == PCProto::S_PRIM);

    pu2.pc->handle_msg(im1, 0, 0, &ipum);
    fail_unless(pu2.pc->get_state() == PCProto::S_PRIM);

}
END_TEST

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

    return s;
}
