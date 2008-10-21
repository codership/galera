#define EVS_SEQNO_MAX 0x800U
#include "../src/evs_seqno.hpp"
#include "../src/evs_input_map.hpp"
#include "../src/evs.cpp"
#include "../../transport/src/transport_dummy.hpp"

#include <check.h>
#include <cstdlib>
#include <vector>

START_TEST(check_seqno)
{
    fail_unless(seqno_eq(0, 0));
    fail_unless(seqno_eq(SEQNO_MAX, SEQNO_MAX));
    fail_if(seqno_eq(5, SEQNO_MAX));
    fail_if(seqno_eq(SEQNO_MAX, 7));

    fail_unless(seqno_lt(2, 4));
    fail_unless(seqno_lt(SEQNO_MAX - 5, SEQNO_MAX - 2));
    fail_unless(seqno_lt(SEQNO_MAX - 5, 1));
    fail_if(seqno_lt(5, 5));
    fail_if(seqno_lt(SEQNO_MAX - 5, SEQNO_MAX - 5));
    fail_if(seqno_lt(5, 5 + SEQNO_MAX/2));

    fail_unless(seqno_gt(4, 2));
    fail_unless(seqno_gt(SEQNO_MAX - 2, SEQNO_MAX - 5));
    fail_unless(seqno_gt(1, SEQNO_MAX - 5));
    fail_if(seqno_gt(5, 5));
    fail_if(seqno_gt(SEQNO_MAX - 5, SEQNO_MAX - 5));
    fail_unless(seqno_gt(5, 5 + SEQNO_MAX/2));


    fail_unless(seqno_eq(seqno_add(1, 5), 6));
    fail_unless(seqno_eq(seqno_add(SEQNO_MAX - 5, 6), 1));
    fail_unless(seqno_eq(seqno_add(7, SEQNO_MAX - 5), 2));
    // fail_unless(seqno_eq(seqno_add(7, SEQNO_MAX), 7));
    // fail_unless(seqno_eq(seqno_add(SEQNO_MAX, SEQNO_MAX), 0));

    fail_unless(seqno_eq(seqno_dec(0, 1), SEQNO_MAX - 1));
    fail_unless(seqno_eq(seqno_dec(7, SEQNO_MAX - 5), 12));
    fail_unless(seqno_eq(seqno_dec(42, 5), 37));

    fail_unless(seqno_eq(seqno_next(SEQNO_MAX - 1), 0));


}
END_TEST

START_TEST(check_msg)
{
    EVSPid pid(1, 2, 3);
    EVSMessage umsg(EVSMessage::USER, pid, SAFE, 0x037b137bU, 0x17U,
		    0x0534555,
		    EVSViewId(Address(7, 0, 0), 0x7373b173U), EVSMessage::F_MSG_MORE);
    
    size_t buflen = umsg.size();
    uint8_t* buf = new uint8_t[buflen];
    
    
    fail_unless(umsg.write(buf, buflen, 1) == 0);
    fail_unless(umsg.write(buf, buflen, 0) == buflen);
    
    
    EVSMessage umsg2;
    fail_unless(umsg2.read(buf, buflen, 1) == 0);
    fail_unless(umsg2.read(buf, buflen, 0) == buflen);
    
    fail_unless(umsg.get_type() == umsg2.get_type());
    fail_unless(umsg.get_source() == umsg2.get_source());
    fail_unless(umsg.get_safety_prefix() == umsg2.get_safety_prefix());
    fail_unless(umsg.get_seq() == umsg2.get_seq());
    fail_unless(umsg.get_seq_range() == umsg2.get_seq_range());
    fail_unless(umsg.get_aru_seq() == umsg2.get_aru_seq());
    fail_unless(umsg.get_flags() == umsg2.get_flags());
    fail_unless(umsg.get_source_view() == umsg2.get_source_view());

    delete[] buf;
}
END_TEST

START_TEST(check_input_map_basic)
{
    EVSInputMap im;

    // Test adding and removing instances
    im.insert_sa(Address(1, 0, 0));
    im.insert_sa(Address(2, 0, 0));
    im.insert_sa(Address(3, 0, 0));

    try {
	im.insert_sa(Address(2, 0, 0));
	fail();
    } catch (FatalException e) {

    }

    im.erase_sa(Address(2, 0, 0));

    try {
	im.erase_sa(Address(2, 0, 0));
	fail();
    } catch (FatalException e) {

    }    
    im.clear();

    // Test message insert with one instance
    EVSViewId vid(Address(0, 0, 0), 1);
    Address sa1(1, 0, 0);
    im.insert_sa(sa1);
    fail_unless(seqno_eq(im.get_aru_seq(), SEQNO_MAX) && seqno_eq(im.get_safe_seq(), SEQNO_MAX));
    im.insert(EVSInputMapItem(
		  sa1, 
		  EVSMessage(EVSMessage::USER, sa1, SAFE, 0, 0, SEQNO_MAX, vid, 0),
		  0));
    fail_unless(seqno_eq(im.get_aru_seq(), 0));
    
    im.insert(EVSInputMapItem(sa1, 
			      EVSMessage(EVSMessage::USER, sa1, SAFE, 2, 0, SEQNO_MAX, vid, 0),
			      0));
    fail_unless(seqno_eq(im.get_aru_seq(), 0));
    im.insert(EVSInputMapItem(sa1, 
			      EVSMessage(EVSMessage::USER, sa1, SAFE, 1, 0, SEQNO_MAX, vid, 0),
			      0));
    fail_unless(seqno_eq(im.get_aru_seq(), 2));


    // Messge out of allowed window (approx aru_seq +- SEQNO_MAX/4)
    // must dropped:
    EVSRange gap = im.insert(
	EVSInputMapItem(sa1, 
			EVSMessage(EVSMessage::USER, sa1, SAFE, 
				   seqno_add(2, SEQNO_MAX/4 + 1), 0, SEQNO_MAX, vid, 0),
			0));
    fail_unless(seqno_eq(gap.low, 3) && seqno_eq(gap.high, 2));
    fail_unless(seqno_eq(im.get_aru_seq(), 2));
    
    // Must not allow insertin second instance before clear()
    try {
	im.insert_sa(Address(2, 0, 0));
	fail();
    } catch (FatalException e) {
	
    }
    
    im.clear();
    
    // Simple two instance case
    Address sa2(2, 0, 0);
    
    im.insert_sa(sa1);
    im.insert_sa(sa2);
    
    for (uint32_t i = 0; i < 3; i++)
	im.insert(EVSInputMapItem(sa1,
				  EVSMessage(EVSMessage::USER, sa1, SAFE, i, 0, SEQNO_MAX, vid, 0),
				  0));
    fail_unless(seqno_eq(im.get_aru_seq(), SEQNO_MAX));   
    
    for (uint32_t i = 0; i < 3; i++) {
	im.insert(EVSInputMapItem(sa2,
				  EVSMessage(EVSMessage::USER, sa2, SAFE, i, 0, SEQNO_MAX, vid, 0),
				  0));
	fail_unless(seqno_eq(im.get_aru_seq(), i));
    }
    
    fail_unless(seqno_eq(im.get_safe_seq(), SEQNO_MAX));
    
    im.set_safe(sa1, 1);
    im.set_safe(sa2, 2);
    fail_unless(seqno_eq(im.get_safe_seq(), 1));
    
    im.set_safe(sa1, 2);
    fail_unless(seqno_eq(im.get_safe_seq(), 2));
    
    
    for (EVSInputMap::iterator i = im.begin(); i != im.end(); 
	 ++i) {
	// std::cerr << i->get_sockaddr().to_string() << " " << i->get_evs_message().get_seq() << "\n";
    }
    
    EVSInputMap::iterator i_next;
    for (EVSInputMap::iterator i = im.begin(); i != im.end(); 
	 i = i_next) {
	i_next = i;
	++i_next;
	// std::cerr << i->get_sockaddr().to_string() << " " << i->get_evs_message().get_seq() << "\n";
	im.erase(i);
    }
    

    im.clear();

}
END_TEST


START_TEST(check_input_map_overwrap)
{
    EVSInputMap im;
    EVSViewId vid(Address(0, 0, 0), 3);
    static const size_t nodes = 16;
    static const size_t qlen = 8;
    Address sas[nodes];
    for (size_t i = 0; i < nodes; ++i) {
	sas[i] = Address(i + 1, 0, 0);
	im.insert_sa(sas[i]);
    }
    
    Time start(Time::now());
    
    size_t n_msg = 0;
    for (uint32_t seqi = 0; seqi < 2*SEQNO_MAX; seqi++) {
	uint32_t seq = seqi % SEQNO_MAX;
// #define aru_seq SEQNO_MAX
#define aru_seq (seqi < 7 ? SEQNO_MAX : seqno_dec(im.get_aru_seq(), ::rand()%3))
	for (size_t j = 0; j < nodes; j++) {
	    im.insert(EVSInputMapItem(sas[j],
				      EVSMessage(EVSMessage::USER, 
						 sas[j],
						 SAFE, 
						 seq, 0, aru_seq, vid, 0),
				      0));	
	    n_msg++;
	}
	if (seqi > 0 && seqi % qlen == 0) {
	    uint32_t seqto = seqno_dec(seq, (::rand() % qlen + 1)*2);
	    EVSInputMap::iterator mi_next;
	    for (EVSInputMap::iterator mi = im.begin(); mi != im.end();
		 mi = mi_next) {
		mi_next = mi;
		++mi_next;
		if (seqno_lt(mi->get_evs_message().get_seq(), seqto))
		    im.erase(mi);
		else if (seqno_eq(mi->get_evs_message().get_seq(), seqto) &&
			 ::rand() % 8 != 0)
		    im.erase(mi);
		else
		    break;
	    }
	}
    }
    EVSInputMap::iterator mi_next;
    for (EVSInputMap::iterator mi = im.begin(); mi != im.end();
	 mi = mi_next) {
	mi_next = mi;
	++mi_next;
	im.erase(mi);
    }
    Time stop(Time::now());
    std::cerr << "Msg rate " << n_msg/(stop.to_double() - start.to_double()) << "\n";
}
END_TEST

START_TEST(check_input_map_random)
{
    // Construct set of messages to be inserted 

    // Fetch messages randomly and insert to input map

    // Iterate over input map - outcome must be 
    EVSPid pid(1, 2, 3);
    EVSViewId vid(pid, 3);    
    std::vector<EVSMessage> msgs(SEQNO_MAX/4);
    
    for (uint32_t i = 0; i < SEQNO_MAX/4; ++i)
	msgs[i] = EVSMessage(
			EVSMessage::USER, 
			pid,
			SAFE, 
			i,
			0,
			SEQNO_MAX,
			vid,
			0);
    
    EVSInputMap im;
    im.insert_sa(Address(1, 0, 0));
    im.insert_sa(Address(2, 0, 0));
    im.insert_sa(Address(3, 0, 0));
    im.insert_sa(Address(4, 0, 0));
    
    for (size_t i = 1; i <= 4; ++i) {
	for (size_t j = msgs.size(); j > 0; --j) {
	    size_t n = ::rand() % j;
	    im.insert(EVSInputMapItem(Address(i, 0, 0), msgs[n], 0));
	    std::swap(msgs[n], msgs[j - 1]);
	}
    }

    size_t cnt = 0;
    for (EVSInputMap::iterator i = im.begin();
	 i != im.end(); ++i) {
	fail_unless(i->get_sockaddr() == Address(cnt % 4 + 1, 0, 0));
	fail_unless(seqno_eq(i->get_evs_message().get_seq(), cnt/4));
	++cnt;
    }

    
}
END_TEST



START_TEST(check_evs_proto)
{
    Address a1(1, 0, 0);
    DummyTransport* tp = static_cast<DummyTransport*>(Transport::create("dummy:", 0, 0));
    EVSProto* ep = new EVSProto(tp, Address(1, 0, 0));
    tp->set_up_context(ep);


    // Initial state is joining
    ep->shift_to(EVSProto::JOINING);
    
    // Handle own join message in JOINING state, must produce emitted 
    // join message along with state change to RECOVERY
    EVSViewId vid(a1, 0);
    EVSMessage jm(EVSMessage::JOIN, a1, vid, SEQNO_MAX, SEQNO_MAX);
    jm.add_instance(a1, true, true, vid, EVSRange());
    ep->handle_join(jm, a1);
    
    ReadBuf* rb = tp->get_out();
    fail_unless(rb != 0);
    
    EVSMessage ojm;
    fail_unless(ojm.read(rb->get_buf(), rb->get_len(), 0));
    rb->release();
    
    fail_unless(ojm.get_type() == EVSMessage::JOIN);
    fail_unless(ep->get_state() == EVSProto::RECOVERY);

    // Handle own join message in RECOVERY state, must reach consensus
    // and emit install message
    ep->handle_join(ojm, ojm.get_source());
    
    rb = tp->get_out();
    fail_unless(rb != 0);
    
    EVSMessage im;
    fail_unless(im.read(rb->get_buf(), rb->get_len(), 0));
    rb->release();
    
    fail_unless(im.get_type() == EVSMessage::INSTALL);
    fail_unless(ep->get_state() == EVSProto::RECOVERY);
    
    // Handle install message, must emit gap message
    ep->handle_install(im, im.get_source());
    
    rb = tp->get_out();
    fail_unless(rb != 0);
    
    EVSMessage gm;
    fail_unless(gm.read(rb->get_buf(), rb->get_len(), 0));
    rb->release();
    fail_unless(gm.get_type() == EVSMessage::GAP);
    fail_unless(ep->get_state() == EVSProto::RECOVERY);
    
    // Handle gap message, state must shift to OPERATIONAL
    ep->handle_gap(gm, gm.get_source());

    fail_unless(ep->get_state() == EVSProto::OPERATIONAL);
    
    rb = tp->get_out();
    fail_unless(rb == 0);

    delete ep;
    delete tp;
    
}
END_TEST

static Suite* suite()
{
    Suite* s = suite_create("evs");
    TCase* tc;

    tc = tcase_create("check_seqno");
    tcase_add_test(tc, check_seqno);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_msg");
    tcase_add_test(tc, check_msg);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_input_map_basic");
    tcase_add_test(tc, check_input_map_basic);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_input_map_overwrap");
    tcase_add_test(tc, check_input_map_overwrap);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_input_map_random");
    tcase_add_test(tc, check_input_map_random);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_evs_proto");
    tcase_add_test(tc, check_evs_proto);
    suite_add_tcase(s, tc);



    return s;
}


int main()
{
    Suite* s;
    SRunner* sr;


    s = suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int n_fail = srunner_ntests_failed(sr);
    srunner_free(sr);
    return n_fail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
