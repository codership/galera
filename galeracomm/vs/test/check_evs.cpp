#include "gcomm/logger.hpp"
#define EVS_SEQNO_MAX 0x800U
#include "../src/evs_proto.cpp"
#include "../src/evs_input_map.cpp"
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
    fail_unless(seqno_eq(im.get_aru_seq(), SEQNO_MAX) && 
                seqno_eq(im.get_safe_seq(), SEQNO_MAX));
    im.insert(EVSInputMapItem(sa1,
		  EVSMessage(EVSMessage::USER, sa1, SAFE, 0, 0, SEQNO_MAX, vid, 0),
                              0, 0));
    fail_unless(seqno_eq(im.get_aru_seq(), 0));
    
    
    
    im.insert(EVSInputMapItem(sa1, 
			      EVSMessage(EVSMessage::USER, sa1, SAFE, 2, 0, 
                                         SEQNO_MAX, vid, 0), 0, 0));
    fail_unless(seqno_eq(im.get_aru_seq(), 0));
    im.insert(EVSInputMapItem(sa1, 
			      EVSMessage(EVSMessage::USER, sa1, SAFE, 1, 0, SEQNO_MAX, vid, 0),
			      0, 0));
    fail_unless(seqno_eq(im.get_aru_seq(), 2));


    // Messge out of allowed window (approx aru_seq +- SEQNO_MAX/4)
    // must dropped:
    EVSRange gap = im.insert(
	EVSInputMapItem(sa1, 
			EVSMessage(EVSMessage::USER, sa1, SAFE, 
				   seqno_add(2, SEQNO_MAX/4 + 1), 0, SEQNO_MAX, vid, 0),
			0, 0));
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
				  0, 0));
    fail_unless(seqno_eq(im.get_aru_seq(), SEQNO_MAX));   
    
    for (uint32_t i = 0; i < 3; i++) {
	im.insert(EVSInputMapItem(sa2,
				  EVSMessage(EVSMessage::USER, sa2, SAFE, i, 0, SEQNO_MAX, vid, 0),
				  0, 0));
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
                                      0, 0));	
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
                     else if (seqno_eq(mi->get_evs_message().get_seq(), seqto)
&&
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
    std::cerr << "Msg rate " << n_msg/(stop.to_double() - start.to_double()) <<
"\n";
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
        msgs[i] = EVSMessage(EVSMessage::USER, 
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
	    im.insert(EVSInputMapItem(Address(i, 0, 0), msgs[n], 0, 0));
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



START_TEST(check_evs_proto_single_boot)
{
    Address a1(1, 0, 0);
    DummyTransport* tp = static_cast<DummyTransport*>(Transport::create("dummy:", 0, 0));
    EVSProto* ep = new EVSProto(tp, Address(1, 0, 0));
    tp->set_up_context(ep);


    // Initial state is joining
    ep->shift_to(EVSProto::JOINING);
    ep->send_join();
    ReadBuf* rb = tp->get_out();
    fail_unless(rb != 0);    
    
    // Handle own join message in JOINING state, must produce emitted 
    // join message along with state change to RECOVERY
    EVSMessage jm;
    fail_unless(jm.read(rb->get_buf(), rb->get_len(), 0));
    rb->release();

    ep->handle_join(jm, a1);
    
    rb = tp->get_out();
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


START_TEST(check_evs_proto_double_boot)
{
    EVSPid p1(1, 0, 0);
    EVSPid p2(2, 0, 0);


    DummyTransport* tp1 = new DummyTransport(0);
    EVSProto* ep1 = new EVSProto(tp1, p1);
    
    DummyTransport* tp2 = new DummyTransport(0);
    EVSProto* ep2 = new EVSProto(tp2, p2);


    ep1->shift_to(EVSProto::JOINING);
    ep2->shift_to(EVSProto::JOINING);

    // 
    ep1->send_join();
    ReadBuf* rb = tp1->get_out();
    fail_unless(rb != 0);
    
    EVSMessage jm1;
    fail_unless(jm1.read(rb->get_buf(), rb->get_len(), 0));
    rb->release();
    fail_unless(jm1.get_type() == EVSMessage::JOIN);
    
    // after this 1 known by 1 and 2
    ep1->handle_join(jm1, jm1.get_source());
    fail_unless(ep1->get_state() == EVSProto::RECOVERY);
    ep2->handle_join(jm1, jm1.get_source());
    fail_unless(ep2->get_state() == EVSProto::RECOVERY);
    
    rb = tp1->get_out();
    fail_unless(rb != 0);
    fail_unless(jm1.read(rb->get_buf(), rb->get_len(), 0));
    rb->release();
    fail_unless(jm1.get_type() == EVSMessage::JOIN);
    
    // after this 1 known by 1 and 2 (and 2 knows that 1 does not know 2)
    // note: no jm1 for ep1 to avoid reaching consensus before handling 
    // jm2 on ep1
    // ep1->handle_join(jm1, jm1.get_source());
    // fail_unless(ep1->get_state() == EVSProto::RECOVERY);
    ep2->handle_join(jm1, jm1.get_source());
    fail_unless(ep2->get_state() == EVSProto::RECOVERY);
    
    EVSMessage jm2;
    
    rb = tp2->get_out();
    fail_unless(rb != 0);
    
    fail_unless(jm2.read(rb->get_buf(), rb->get_len(), 0));
    rb->release();
    fail_unless(jm2.get_type() == EVSMessage::JOIN);
    
    // after this 1 and 2 know 1 and 2 (but 2 thinks that 1 does not know 2)
    ep1->handle_join(jm2, jm2.get_source());
    fail_unless(ep1->get_state() == EVSProto::RECOVERY);
    // ep2->handle_join(jm2, jm2.get_source());
    // fail_unless(ep2->get_state() == EVSProto::RECOVERY);
    
    rb = tp1->get_out();
    fail_unless(rb != 0);
    fail_unless(jm1.read(rb->get_buf(), rb->get_len(), 0));
    rb->release();
    fail_unless(jm1.get_type() == EVSMessage::JOIN);
    
    // 2 learns that 1 knows about 2
    ep2->handle_join(jm1, jm1.get_source());
    fail_unless(ep2->get_state() == EVSProto::RECOVERY);
    
    rb = tp2->get_out();
    fail_unless(rb != 0);
    
    fail_unless(jm2.read(rb->get_buf(), rb->get_len(), 0));
    rb->release();
    fail_unless(jm2.get_type() == EVSMessage::JOIN);    
    
    // 1 learns that 2 knows that 1 knows about 2
    ep1->handle_join(jm2, jm2.get_source());
    // 2 receives join that matches its current view, should not emit 
    // join anymore
    ep2->handle_join(jm2, jm2.get_source());
    fail_unless(tp2->get_out() == 0);

    rb = tp1->get_out();
    fail_unless(rb != 0);



    // This should already be install message
    EVSMessage im;
    fail_unless(im.read(rb->get_buf(), rb->get_len(), 0));
    rb->release();
    fail_unless(im.get_type() == EVSMessage::INSTALL);
    
    ep1->handle_install(im, im.get_source());
    ep2->handle_install(im, im.get_source());
    
    fail_unless(ep1->get_state() == EVSProto::RECOVERY);
    fail_unless(ep2->get_state() == EVSProto::RECOVERY);
    
    // Now both should have outgoing gap messages 
    rb = tp1->get_out();
    fail_unless(rb != 0);
    EVSMessage gm1;
    fail_unless(gm1.read(rb->get_buf(), rb->get_len(), 0));
    rb->release();
    fail_unless(gm1.get_type() == EVSMessage::GAP);
    
    ep1->handle_gap(gm1, gm1.get_source());
    ep2->handle_gap(gm1, gm1.get_source());
    
    fail_unless(ep1->get_state() == EVSProto::RECOVERY);
    fail_unless(ep2->get_state() == EVSProto::RECOVERY);

    rb = tp2->get_out();
    fail_unless(rb != 0);
    EVSMessage gm2;
    fail_unless(gm2.read(rb->get_buf(), rb->get_len(), 0));
    rb->release();
    fail_unless(gm2.get_type() == EVSMessage::GAP);
    
    ep1->handle_gap(gm2, gm2.get_source());
    ep2->handle_gap(gm2, gm2.get_source());

    // And finally operational states should have been reached
    fail_unless(ep1->get_state() == EVSProto::OPERATIONAL);
    fail_unless(ep2->get_state() == EVSProto::OPERATIONAL);

    delete ep1;
    delete ep2;
    delete tp1;
    delete tp2;

}
END_TEST


struct Inst : public Toplay {
    DummyTransport* tp;
    EVSProto* ep;
    Inst(DummyTransport* tp_, EVSProto* ep_) : tp(tp_), ep(ep_) {}
    void handle_up(const int ctx, const ReadBuf* rb, const size_t roff,
                   const ProtoUpMeta* um)
    {
        // std::cerr << "delivery: " << rb << " " << rb->get_len() << " " << roff << "\n";
        // std::cout << (char*)rb->get_buf(roff) << "\n";
        // const EVSProtoUpMeta* eum = static_cast<const EVSProtoUpMeta*>(um);
        // std::cout << "msg from " << eum->source.to_string() << " " << (char*)rb->get_buf(roff) << "\n";
    }
};

static bool all_operational(const std::vector<Inst*>* pvec)
{
    for (std::vector<Inst*>::const_iterator i = pvec->begin(); 
	 i != pvec->end(); ++i) {
	if ((*i)->ep->get_state() != EVSProto::OPERATIONAL &&
            (*i)->ep->get_state() != EVSProto::CLOSED)
	    return false;
    }
    return true;
}

struct Stats {
    uint64_t sent_msgs;
    uint64_t total_msgs;
    std::vector<uint64_t> msgs;

    Stats() : sent_msgs(0), total_msgs(0) {
        msgs.resize(6, 0);
    }

    ~Stats() {
        // if (total_msgs) {
        //  print();
        // }
    }

    static double fraction_of(const uint64_t a, const uint64_t b) {
        if (b == 0)
            return 0;
        else 
            return double(a)/double(b);
    }

    void print() {
        LOG_INFO("Sent messages: " + ::to_string(sent_msgs));
        LOG_INFO("Total messages: " + ::to_string(total_msgs));
        for (size_t i = 0; i < 6; ++i) {
            LOG_INFO("Type " + EVSMessage::to_string(static_cast<EVSMessage::Type>(i)) 
                     + " messages: " + ::to_string(msgs[i]) 
                     + " fraction of sent: " + ::to_string(fraction_of(msgs[i], sent_msgs)) 
                     + " fraction of total: " + ::to_string(fraction_of(msgs[i], total_msgs)));
        }
    }

    void clear() {
        sent_msgs = 0;
        total_msgs = 0;
        for (size_t i = 0; i < 6; ++i)
            msgs[i] = 0;
    }

    void acc_mcast(const int type) {
        total_msgs++;
        msgs[type]++;
    }

    void acc_sent() {
        sent_msgs++;
    }

};

Stats stats;

static void multicast(std::vector<Inst*>* pvec, const ReadBuf* rb, const int ploss)
{
    EVSMessage msg;
    fail_unless(msg.read(rb->get_buf(), rb->get_len(), 0));
    LOG_DEBUG(std::string("msg: ") + ::to_string(msg.get_type()));
    stats.acc_mcast(msg.get_type());
    for (std::vector<Inst*>::iterator j = pvec->begin();
	 j != pvec->end(); ++j) {

        if ((*j)->ep->get_state() == EVSProto::CLOSED)
            continue;
        
        if (::rand() % 10000 < ploss) {
            if (::rand() % 3 == 0) {
                LOG_INFO("dropping " + EVSMessage::to_string(msg.get_type()) + 
                         " from " + msg.get_source().to_string() + " to " + 
                         (*j)->ep->my_addr.to_string() + " seq " + ::to_string(msg.get_seq()) );
                continue;
            } else {
                LOG_INFO("dropping " + EVSMessage::to_string(msg.get_type()) + " from " + msg.get_source().to_string() + " to all");
                break;
            }
        }
        
        
	switch (msg.get_type()) {
	case EVSMessage::USER:
	    (*j)->ep->handle_user(msg, msg.get_source(), rb, 0);
	    break;
	case EVSMessage::DELEGATE:
	    (*j)->ep->handle_delegate(msg, msg.get_source(), rb, 0);
	    break;
	case EVSMessage::GAP:
	    (*j)->ep->handle_gap(msg, msg.get_source());
	    break;
	case EVSMessage::JOIN:
	    (*j)->ep->handle_join(msg, msg.get_source());
	    break;
	case EVSMessage::LEAVE:
	    (*j)->ep->handle_leave(msg, msg.get_source());
	    break;
	case EVSMessage::INSTALL:
	    (*j)->ep->handle_install(msg, msg.get_source());
	    break;
	}
    }
}

static void reach_operational(std::vector<Inst*>* pvec)
{
    while (all_operational(pvec) == false) {
	for (std::vector<Inst*>::iterator i = pvec->begin();
	     i != pvec->end(); ++i) {
	    ReadBuf* rb = (*i)->tp->get_out();
	    if (rb) {
		multicast(pvec, rb, 0);
		rb->release();
	    }
	}
    }
}


START_TEST(check_evs_proto_converge)
{
    size_t n = 8;
    std::vector<Inst*> vec(n);
    
    for (size_t i = 0; i < n; ++i) {
	DummyTransport* tp = new DummyTransport(0);
	vec[i] = new Inst(tp, new EVSProto(tp, EVSPid(i + 1, 0, 0)));
	vec[i]->ep->shift_to(EVSProto::JOINING);
	vec[i]->ep->send_join();
    }
    reach_operational(&vec);
}
END_TEST

START_TEST(check_evs_proto_converge_1by1)
{
    std::vector<Inst*> vec;
    for (size_t n = 0; n < 8; ++n) {
	vec.resize(n + 1);
	DummyTransport* tp = new DummyTransport(0);
	vec[n] = new Inst(tp, new EVSProto(tp, EVSPid(n + 1, 0, 0)));
	vec[n]->ep->shift_to(EVSProto::JOINING);
	vec[n]->ep->send_join();
	reach_operational(&vec);
    }
}
END_TEST


static void send_msgs(std::vector<Inst*>* vec)
{
    for (std::vector<Inst*>::iterator i = vec->begin(); i != vec->end(); 
	 ++i) {
	char buf[8] = {'1', '2', '3', '4', '5', '6', '7', '\0'};
	WriteBuf wb(buf, sizeof(buf));
	(*i)->ep->handle_down(&wb, 0); 
        stats.acc_sent();
    }
}

static void send_msgs_rnd(std::vector<Inst*>* vec, size_t max_n)
{
    static uint32_t seq = 0;
    for (std::vector<Inst*>::iterator i = vec->begin(); i != vec->end(); 
	 ++i) {
        for (size_t n = ::rand() % (max_n + 1); n > 0; --n) { 
            char buf[10];
            memset(buf, 0, sizeof(buf));
            sprintf(buf, "%x", seq);
            WriteBuf wb(buf, sizeof(buf));
            (*i)->ep->handle_down(&wb, 0); 
            ++seq;
            stats.acc_sent();
        }
    }
}

#if 0
static void send_msgs_rnd_single(std::vector<Inst*>* vec, size_t max_n)
{
    static uint32_t seq = 0;
    for (std::vector<Inst*>::iterator i = vec->begin(); i != vec->end(); 
	 ++i) {
        size_t n_to_send = ::rand() % (max_n + 1);
        for (size_t n = n_to_send; n > 0; --n) { 
            char buf[10];
            memset(buf, 0, sizeof(buf));
            sprintf(buf, "%x", seq);
            WriteBuf wb(buf, sizeof(buf));
            (*i)->ep->handle_down(&wb, 0); 
            ++seq;
            stats.acc_sent();
        }
        if (n_to_send)
            break;
    }
}
#endif

static void deliver_msgs(std::vector<Inst*>* pvec)
{
    bool empty;
    do {
	empty = true;
	for (std::vector<Inst*>::iterator i = pvec->begin();
	     i != pvec->end(); ++i) {
	    ReadBuf* rb = (*i)->tp->get_out();
	    if (rb) {
		empty = false;
		multicast(pvec, rb, 0);
		rb->release();
	    }
	}
    } while (empty == false);
}

static void deliver_msgs_lossy(std::vector<Inst*>* pvec, int ploss)
{
    bool empty;
    do {
	empty = true;
	for (std::vector<Inst*>::iterator i = pvec->begin();
	     i != pvec->end(); ++i) {
	    ReadBuf* rb = (*i)->tp->get_out();
	    if (rb) {
		empty = false;
		multicast(pvec, rb, ploss);
		rb->release();
	    }
	}
    } while (empty == false);
}




START_TEST(check_evs_proto_user_msg)
{
    stats.clear();
    std::vector<Inst*> vec;
    for (size_t n = 0; n < 8; ++n) {
        vec.resize(n + 1);
        DummyTransport* tp = new DummyTransport(0);
        vec[n] = new Inst(tp, new EVSProto(tp, EVSPid(n + 1, 0, 0)));
        vec[n]->ep->set_up_context(vec[n]);
        vec[n]->ep->shift_to(EVSProto::JOINING);
        vec[n]->ep->send_join();
        reach_operational(&vec);
        send_msgs(&vec);
        deliver_msgs(&vec);

        send_msgs(&vec);
        send_msgs(&vec);
        deliver_msgs(&vec);
        stats.print();
        stats.clear();
    }


    LOG_INFO("random sending 1");

    for (int i = 0; i < 50; ++i) {
        send_msgs_rnd(&vec, 1);
        deliver_msgs(&vec);
    }

    stats.print();
    stats.clear();


    LOG_INFO("random sending 3");

    for (int i = 0; i < 50; ++i) {
        send_msgs_rnd(&vec, 3);
        deliver_msgs(&vec);
    }

    stats.print();
    stats.clear();


    LOG_INFO("random sending 5");

    for (int i = 0; i < 50; ++i) {
        send_msgs_rnd(&vec, 5);
        deliver_msgs(&vec);
    }

    stats.print();
    stats.clear();


    LOG_INFO("random sending 7");

    for (int i = 0; i < 50; ++i) {
        send_msgs_rnd(&vec, 7);
        deliver_msgs(&vec);
    }

    stats.print();
    stats.clear();

    LOG_INFO("random sending 16");

    for (int i = 0; i < 50; ++i) {
        send_msgs_rnd(&vec, 16);
        deliver_msgs(&vec);
    }

    stats.print();
    stats.clear();
}
END_TEST

START_TEST(check_evs_proto_consensus_with_user_msg)
{
    stats.clear();
    std::vector<Inst*> vec;

    for (size_t n = 0; n < 8; ++n) {
        send_msgs_rnd(&vec, 8);
        vec.resize(n + 1);
        DummyTransport* tp = new DummyTransport(0);
        vec[n] = new Inst(tp, new EVSProto(tp, EVSPid(n + 1, 0, 0)));
        vec[n]->ep->set_up_context(vec[n]);
        vec[n]->ep->shift_to(EVSProto::JOINING);
        vec[n]->ep->send_join();
        reach_operational(&vec);
        stats.print();
        stats.clear();
    }
}
END_TEST

START_TEST(check_evs_proto_msg_loss)
{
    stats.clear();
    std::vector<Inst*> vec;
    for (size_t n = 0; n < 8; ++n) {
        vec.resize(n + 1);
        DummyTransport* tp = new DummyTransport(0);
        vec[n] = new Inst(tp, new EVSProto(tp, EVSPid(n + 1, 0, 0)));
        vec[n]->ep->set_up_context(vec[n]);
        vec[n]->ep->shift_to(EVSProto::JOINING);
        vec[n]->ep->send_join();
    }
    reach_operational(&vec);

    for (int i = 0; i < 50; ++i) {
        send_msgs_rnd(&vec, 8);
        deliver_msgs_lossy(&vec, 50);
    }
    stats.print();
    stats.clear();

}
END_TEST


START_TEST(check_evs_proto_leave)
{
    stats.clear();
    std::vector<Inst*> vec;

    for (size_t n = 0; n < 8; ++n) {
        send_msgs_rnd(&vec, 8);
        vec.resize(n + 1);
        DummyTransport* tp = new DummyTransport(0);
        vec[n] = new Inst(tp, new EVSProto(tp, EVSPid(n + 1, 0, 0)));
        vec[n]->ep->set_up_context(vec[n]);
        vec[n]->ep->shift_to(EVSProto::JOINING);
        vec[n]->ep->send_join();
        reach_operational(&vec);
        stats.print();
        stats.clear();
    }

    for (size_t n = 8; n > 0; --n) {
        send_msgs_rnd(&vec, 8);
        vec[n - 1]->ep->shift_to(EVSProto::LEAVING);
        vec[n - 1]->ep->send_leave();
        reach_operational(&vec);
    }

}
END_TEST

static void join_inst(std::list<std::vector<Inst* >* >& vlist, 
               std::vector<Inst*>& vec, 
               size_t *n)
{
    std::cout << *n << "\n";
    vec.resize(*n + 1);
    DummyTransport* tp = new DummyTransport(0);
    vec[*n] = new Inst(tp, new EVSProto(tp, EVSPid(*n + 1, 0, 0)));
    vec[*n]->ep->set_up_context(vec[*n]);
    vec[*n]->ep->shift_to(EVSProto::JOINING);
    vec[*n]->ep->send_join();

    std::list<std::vector<Inst* >* >::iterator li;
    if (vlist.size() == 0) {
        vlist.push_back(new std::vector<Inst*>);
    }
    li = vlist.begin();
    (*li)->push_back(vec[*n]);
    *n = *n + 1;
}

static void leave_inst(std::vector<Inst*>& vec)
{
    for (std::vector<Inst*>::iterator i = vec.begin();
         i != vec.end(); ++i) {
        if ((*i)->ep->get_state() == EVSProto::OPERATIONAL ||
            (*i)->ep->get_state() == EVSProto::RECOVERY) {
            (*i)->ep->shift_to(EVSProto::LEAVING);
            (*i)->ep->send_leave();
        }
    }
}


static void split_inst(std::list<std::vector<Inst* >* >& vlist)
{

}


static void merge_inst(std::list<std::vector<Inst* >* > *vlist)
{

}



START_TEST(check_evs_proto_full)
{
    size_t n = 0;
    std::vector<Inst*> vec;
    std::list<std::vector<Inst* >* > vlist;

    stats.clear();

    for (size_t i = 0; i < 1000; ++i) {
        send_msgs_rnd(&vec, 8);
        if (::rand() % 70 == 0)
            join_inst(vlist, vec, &n);
        if (::rand() % 200 == 0)
            leave_inst(vec);
        if (::rand() % 400 == 0)
            split_inst(vlist);
        else if (::rand() % 50 == 0)
            merge_inst(&vlist);
        
        send_msgs_rnd(&vec, 8);
        for (std::list<std::vector<Inst* >* >::iterator li = vlist.begin();
             li != vlist.end(); ++li)
            deliver_msgs_lossy(*li, 50);
    }
    
    stats.print();

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

    tc = tcase_create("check_evs_proto_single_boot");
    tcase_add_test(tc, check_evs_proto_single_boot);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_evs_proto_double_boot");
    tcase_add_test(tc, check_evs_proto_double_boot);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_evs_proto_converge");
    tcase_add_test(tc, check_evs_proto_converge);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_evs_proto_converge_1by1");
    tcase_add_test(tc, check_evs_proto_converge_1by1);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_evs_proto_user_msg");
    tcase_set_timeout(tc, 30);
    tcase_add_test(tc, check_evs_proto_user_msg);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_evs_proto_consensus_with_user_msg");
    tcase_set_timeout(tc, 30);
    tcase_add_test(tc, check_evs_proto_consensus_with_user_msg);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_evs_proto_msg_loss");
    tcase_add_test(tc, check_evs_proto_msg_loss);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_evs_proto_leave");
    tcase_add_test(tc, check_evs_proto_leave);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_evs_proto_full");
    tcase_set_timeout(tc, 30);
    tcase_add_test(tc, check_evs_proto_full);
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
