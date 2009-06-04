
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

    sim.insert(make_pair(UUID(0,0), PCInst(6, ViewId(UUID(0, 0), 9), 42)));
    sim.insert(make_pair(UUID(0,0), PCInst(88, ViewId(UUID(0, 0), 3), 472)));
    sim.insert(make_pair(UUID(0,0), PCInst(78, ViewId(UUID(0, 0), 87), 52)));
    
    size_t expt_size = 4 // hdr
        + 4              // seq
        + 4 + 3*(UUID::size() + 4 + 20 + 8); // PCInstMap
    check_serialization(pcs, expt_size, PCStateMessage());
                       
    PCInstallMessage pci;
    PCInstMap& iim = pci.get_inst_map();

    iim.insert(make_pair(UUID(0,0), PCInst(6, ViewId(UUID(0, 0), 9), 42)));
    iim.insert(make_pair(UUID(0,0), PCInst(88, ViewId(UUID(0, 0), 3), 472)));
    iim.insert(make_pair(UUID(0,0), PCInst(78, ViewId(UUID(0, 0), 87), 52)));
    iim.insert(make_pair(UUID(0,0), PCInst(457, ViewId(UUID(0, 0), 37), 56)));

    expt_size = 4 // hdr
        + 4              // seq
        + 4 + 4*(UUID::size() + 4 + 20 + 8); // PCInstMap
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
    PCUser()
    {
    }
    
    void handle_up(const int cid, const ReadBuf* rb, const size_t roff,
                   const ProtoUpMeta* um)
    {
        const View* v = um->get_view();
        if (v)
        {
            fail_unless(v->get_type() == View::V_PRIM ||
                        v->get_type() == View::V_NON_PRIM);
            views.push_back(View(*v));
        }
    }
    
};

START_TEST(test_pc_view_changes)
{
    UUID uuid1(0, 0);
    PCUser pu1;
    PCProto pc1(uuid1, 0, 0, true);
    DummyTransport tp1;

    gcomm::connect(&tp1, &pc1);
    gcomm::connect(&pc1, &pu1);
    

    View v1(View::V_TRANS, ViewId(uuid1, 0));
    v1.add_member(uuid1, "n1");
    
    ProtoUpMeta um1(&v1);
    pc1.handle_up(0, 0, 0, &um1);
    
}
END_TEST


Suite* pc_suite()
{
    Suite* s = suite_create("pc");
    TCase* tc;

    tc = tcase_create("test_pc_messages");
    tcase_add_test(tc, test_pc_messages);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_pc_view_changes");
    tcase_add_test(tc, test_pc_view_changes);
    suite_add_tcase(s, tc);

    return s;
}
