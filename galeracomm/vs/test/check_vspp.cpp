
#include "gcomm/vs.hpp"
#include "gcomm/monitor.hpp"
#include "gcomm/thread.hpp"
#include "vsbes.hpp"

#include <iostream>
#include <check.h>
#include <cassert>
#include <csignal>
#include <list>


static Logger& logger = Logger::instance();

START_TEST(check_vsviewid)
{
    Address a1(1, 0, 0), a2(2, 0, 0);
    VSViewId v1(1, a1), v2(1, a2), v3(2, a1), v4(2, a2);
    

    fail_unless(a1 < a2);
    fail_unless(v1 == v1);
    fail_unless(v1 < v2 && v1 < v3 && v1 < v4);
    fail_unless(v2 < v3 && v2 < v4);
    fail_unless(v3 < v4);


    char *buf = new char[v1.size()];
    fail_unless(v1.write(buf, v1.size(), 0) == v1.size());
    VSViewId v1_copy;

    fail_unless(v1_copy.read(buf, v1.size(), 0) == v1.size());
    delete[] buf;
    fail_unless(v1_copy == v1);
    

}
END_TEST

START_TEST(check_vsview)
{
    VSView view1(true, VSViewId(1, Address(1, 0, 0)));
    VSView view2(false, VSViewId(2, Address(1, 0, 0)));


    view1.addr_insert(Address(1, 0, 0));
    view1.addr_insert(Address(3, 0, 0));
    view1.addr_insert(Address(5, 0, 0));

    view1.joined_insert(Address(5, 0, 0));

    view1.left_insert(Address(7, 0, 0));

    view1.partitioned_insert(Address(11, 0, 0));

    char *buf = new char[view1.size()];
    fail_unless(view1.write(buf, view1.size(), 0) == view1.size());
    VSView view1_copy;
    view1_copy.read(buf, view1.size(), 0);
    delete[] buf;

    fail_unless(view1_copy.is_trans() == true);
    fail_unless(view1_copy.get_view_id() == VSViewId(1, Address(1, 0, 0)));
    fail_unless(view1_copy.get_addr() == view1.get_addr());
    fail_unless(view1_copy.get_joined() == view1.get_joined());
    fail_unless(view1_copy.get_left() == view1.get_left());
    fail_unless(view1_copy.get_partitioned() == view1.get_partitioned());
    

    std::set<Address> all;
    all.insert(Address(5, 0, 0));
    all.insert(Address(7, 0, 0));
    all.insert(Address(11, 0, 0));
    
    for (std::set<Address>::iterator i = all.begin(); i != all.end(); ++i) {
	try {
	    view1.addr_insert(*i);
	    fail("add_insert");
	}
	catch (Exception e) {
	    
	}
	try {
	    view1.joined_insert(*i);
	    fail("joined_insert");
	}
	catch (Exception e) {
	    
	}
	try {
	    view1.left_insert(*i);
	    fail("left_insert");
	}
	catch (Exception e) {
	    
	}
	try {
	    view1.partitioned_insert(*i);
	    fail("partitioned_insert");
	}
	catch (Exception e) {
	    
	}
    }



}
END_TEST

START_TEST(check_vsmessage)
{
    VSView view(true, VSViewId(4, Address(1, 4, 7)));
    view.addr_insert(Address(1, 4, 7));
    view.addr_insert(Address(4, 4, 7));
    view.addr_insert(Address(7, 4, 7));
    view.joined_insert(Address(7, 4, 7));
    view.left_insert(Address(11, 4, 7));
    view.partitioned_insert(Address(13, 4, 7));


    //
    // Conf message
    //
    VSMessage conf_msg(0, &view);
    fail_unless(conf_msg.get_type() == VSMessage::CONF);
    char *buf = new char[conf_msg.size()];
    conf_msg.write(buf, conf_msg.size(), 0);
    VSMessage conf_msg2;
    conf_msg2.read(buf, conf_msg.size(), 0);
    delete[] buf;

    fail_unless(conf_msg2.get_version() == conf_msg.get_version());
    fail_unless(conf_msg2.get_type() == conf_msg.get_type());
    fail_unless(*conf_msg.get_view() == *conf_msg.get_view());
    fail_unless(conf_msg2.get_source() == conf_msg.get_source());
    fail_unless(conf_msg2.get_source_view() == conf_msg.get_source_view());
    fail_unless(conf_msg2.get_seq() == conf_msg.get_seq());
    
    // 
    // State message
    //
    try {
	// And exactly why this should fail?
	VSMessage state_msg_inv(
	    Address(1, 1, 0), VSViewId(6, Address(1, 5, 7)), &view, 0, 0);
	fail("");
    } catch (Exception e) {

    }
    Address state_msg_user_state(12356, 212, 111);
    
    VSMessage state_msg(Address(1, 0, 0), VSViewId(6, Address(1, 5, 7)), 
			&view, 0, &state_msg_user_state);
    fail_unless(state_msg.get_type() == VSMessage::STATE);
    buf = new char[state_msg.size()];
    state_msg.write(buf, state_msg.size(), 0);
    VSMessage state_msg2;
    state_msg2.read(buf, state_msg.size(), 0);

    
    fail_unless(state_msg2.get_version() == state_msg.get_version());
    fail_unless(state_msg2.get_type() == state_msg.get_type());
    fail_unless(*state_msg.get_view() == *state_msg.get_view());
    fail_unless(state_msg2.get_source() == state_msg.get_source());
    fail_unless(state_msg2.get_source_view() == state_msg.get_source_view());
    fail_unless(state_msg2.get_seq() == state_msg.get_seq());
    const ReadBuf *smus_buf = state_msg2.get_user_state_buf();
    Address smusa;
    fail_if(smusa.read(smus_buf->get_buf(), smus_buf->get_len(), 0) == 0);
    fail_unless(smusa == state_msg_user_state);

    delete[] buf;

    //
    // Data message
    //
    VSMessage data_msg(Address(1, 5, 7), VSViewId(6, Address(1, 5, 7)), 5);
    fail_unless(data_msg.get_type() == VSMessage::DATA);

    buf = new char[128];
    for (int i = 0; i < 128; i++)
	buf[i] = (i % 128);
    WriteBuf *wb = new WriteBuf(buf, 128);

    wb->prepend_hdr(data_msg.get_hdr(), data_msg.get_hdrlen());
    ReadBuf *rb = wb->to_readbuf();
    delete wb;
    delete[] buf;

    VSMessage data_msg2;
    fail_if(data_msg2.read(rb->get_buf(), rb->get_len(), 0) == 0);
    
    fail_unless(data_msg2.get_type() == VSMessage::DATA);
    fail_unless(data_msg2.get_view() == 0);
    fail_unless(data_msg2.get_source() == data_msg.get_source());
    fail_unless(data_msg2.get_source_view() == data_msg.get_source_view());
    fail_unless(data_msg2.get_seq() == data_msg.get_seq());
    fail_unless(data_msg2.get_data_offset() > 0 && 
		data_msg2.get_data_offset() == data_msg2.get_hdrlen());

    const char *ptr = reinterpret_cast<const char*>(rb->get_buf(data_msg2.get_data_offset()));
    for (int i = 0; i < 128; i++)
	fail_unless(ptr[i] == (i % 128));
    
    rb->release();

}
END_TEST

typedef std::pair<VSView *, VSMessage *> Event;

class Session : public Toplay {
    VS *vs;
    bool connected;
    bool leaving;
    const char *data_b;
public:
    std::deque<Event> events;


    Session(const char *be_addr, Poll *p, Monitor *m = 0) : 
	connected(false), leaving(false), 
	data_b("0123456789012345678901234567890123456789"){
	vs = VS::create(be_addr, p, m);
	set_down_context(vs);
    }
    
    static void release(Event& e) {
	delete e.first;
	delete e.second;
    }
    ~Session() {
	for_each(events.begin(), events.end(), release);
	delete vs;
    }
    void handle_up(const int cid, const ReadBuf *rb, const size_t roff, 
		   const ProtoUpMeta *um) {
	const VSUpMeta *vum = static_cast<const VSUpMeta *>(um);
	// fail_unless(!!vum->view  || !!vum->msg);
	if (connected == false) {
	    fail_unless(!!vum->view);
	    if (vum->view->is_trans())
		return;
	}
	
	if (vum && vum->view) {
//	    if (vum->view)
//		std::cerr << *vum->view << "\n";
	    if (vum->view->get_addr().find(vs->get_self()) != vum->view->get_addr().end()) {
		if (connected == false && vum->view->is_trans())
		    throw DException("");
		connected = true;
	    } else {
		connected = false;
	    }
	    // events.push_back(Event(new VSView(*vum->view), 0));
	} else if (rb) {
	    // fail_unless(connected);
	    // std::cerr << "Received: ";
	    // std::cerr <<  << "\n";
	    // events.push_back(Event(0, new VSMessage(*vum->msg)));
	    const char *recv_b = reinterpret_cast<const char *>(rb->get_buf(roff));
	    fail_unless(rb->get_len() == strlen(data_b) + roff + 1);
	    fail_unless(strcmp(recv_b, data_b) == 0);
	}
    }
    bool is_connected() const {
	return connected;
    }
    bool is_leaving() const {
	return leaving;
    }

    void connect() {
	vs->connect();
    }
    void join() {
	leaving = false;
	vs->join(0, this);
    }
    void leave() {
	vs->leave(0);
	leaving = true;
    }
    void close() {
	vs->close();
    }
    int send() {
	const char *data_b = "0123456789012345678901234567890123456789";
	WriteBuf wb(data_b, strlen(data_b) + 1);
	return pass_down(&wb, 0);
    }
    
};

void release(Session *s)
{
    delete s;
}

struct CompleteView {
    VSView *reg_view;
    std::deque<VSMessage *> reg_msgs;
    VSView *trans_view;
    std::deque<VSMessage *> trans_msgs;
    static void release(VSMessage *m) {
	delete m;
    }
    CompleteView() : reg_view(0), reg_msgs(), trans_view(0), trans_msgs() {}
    ~CompleteView() {
	for_each(reg_msgs.begin(), reg_msgs.end(), release);
	for_each(trans_msgs.begin(), trans_msgs.end(), release);

	delete reg_view;
	delete trans_view;

    }
    struct less_str {
	bool operator()(const CompleteView *a, const CompleteView *b) {
	    if (a->reg_view->get_view_id() < b->reg_view->get_view_id())
		return true;
	    else if (b->reg_view->get_view_id() < a->reg_view->get_view_id())
		return false;
	    return a->trans_view->get_view_id() < b->trans_view->get_view_id();
	}
    };
};

static CompleteView *get_complete_view(Session *s)
{
    CompleteView *cv = 0;
    // TODO
    std::deque<Event>::iterator i, ri, ti, ri2;
    std::deque<Event>& events(s->events);
    
    ri = events.begin();
    if (ri == events.end())
	return 0;
    
    fail_unless(ri->first != 0 && ri->second == 0 && ri->first->get_addr().size() != 0);
    
    ti = events.end();
    for (i = ri, ++i; i != events.end() && ti == events.end(); ++i) {
	if (i->first) {
	    fail_unless(i->first->is_trans());
	    ti = i;
	} 
    }

    if (ti == events.end())
	return 0;

    ri2 = events.end();
    for (i = ti, ++i; i != events.end() && ri2 == events.end(); ++i) {
	if (i->first) {
	    fail_unless(i->first->is_trans() == false);
	    ri2 = i;
	}
    }

    if (ri2 == events.end() && ti->first->get_addr().size() != 0)
	return 0;
    
    cv = new CompleteView();
    
    cv->reg_view = ri->first;
    events.pop_front();
    
    for (i = events.begin(); i != ti; i = events.begin()) {
	cv->reg_msgs.push_back(i->second);
	events.pop_front();
    }
    
    cv->trans_view = ti->first;
    events.pop_front();
    
    for (i = events.begin(); i != ri2; i = events.begin()) {
	cv->trans_msgs.push_back(i->second);
	events.pop_front();
    }
    
    fail_unless(cv->reg_view != 0 && cv->trans_view != 0);
    
    return cv;
}

static void compare_complete_views(const CompleteView *a, const CompleteView *b)
{
    fail_unless(*a->reg_view == *b->reg_view);
    fail_unless(*a->trans_view == *b->trans_view);

    std::deque<VSMessage *>::const_iterator ai, bi;

    for (ai = a->reg_msgs.begin(), bi = b->reg_msgs.begin();
	 ai != a->reg_msgs.end() && bi != b->reg_msgs.end(); ++ai, ++bi) {
	fail_unless(**ai == **bi);
    }
    fail_unless(ai == a->reg_msgs.end() && bi == b->reg_msgs.end());

    for (ai = a->trans_msgs.begin(), bi = b->trans_msgs.begin();
	 ai != a->trans_msgs.end() && bi != b->trans_msgs.end(); ++ai, ++bi) {
	fail_unless(**ai == **bi);
    }
    fail_unless(ai == a->trans_msgs.end() && bi == b->trans_msgs.end());
    

    std::cerr << "Compared views, reg messages " << a->reg_msgs.size() << " ";
    std::cerr << "trans messages " << a->trans_msgs.size() << "\n";
}

void check_ortho(const CompleteView *a, const CompleteView *b)
{
    // Make sure that if transitional views originate from the same
    // reguar view, their intersection is empty
    
    if (a->reg_view->get_view_id() == b->reg_view->get_view_id()) {
	std::set<Address> inters;
	set_intersection(a->trans_view->get_addr().begin(),
			 a->trans_view->get_addr().end(),
			 b->trans_view->get_addr().begin(),
			 b->trans_view->get_addr().end(),
			 std::insert_iterator<std::set<Address> >(inters, inters.begin()));
	fail_if(inters.empty() == false);
    }

}


typedef std::set<CompleteView *, CompleteView::less_str> CVSet;

static void verify_views(std::deque<Session *>& ss)
{
    
    CVSet cviews;
    
    for (std::deque<Session *>::iterator i = ss.begin(); i != ss.end(); ++i) {
	CompleteView *cv;
	while ((cv = get_complete_view(*i))) {
	    CVSet::iterator cvi = cviews.find(cv);
	    if (cvi != cviews.end()) {
		compare_complete_views(*cvi, cv);
		delete cv;
	    } else {
		fail_if(cviews.insert(cv).second == false);
	    }
	}
    }

    for (CVSet::iterator i = cviews.begin(); i != cviews.end(); ++i) {
	CVSet::iterator j = i;
	for (++j; j != cviews.end(); ++j) {
	    check_ortho(*i, *j);
	}
    }


    for (CVSet::iterator i = cviews.begin(); i != cviews.end(); ++i) {
	delete *i;
    }

}

const char *poll_type = "fifo";
int poll_intval = 100;
const char *vsbe_addr = "fifo";

START_TEST(check_vs)
{
    VS *vs = 0;

    Poll *p = Poll::create(poll_type);
    


    try {
	vs = VS::create(vsbe_addr, p, 0);
	fail_unless(!!vs);
    } catch (Exception e) {
	fail(e.what());
    }

    Poll *p2 = Poll::create(poll_type);
    try {
	VS *vs2 = VS::create("asdfasdf", p2, 0);
	delete vs2;
	fail();
    } catch (Exception e) {
	std::cerr << "Expected exception: " << e.what() << "\n";
    }
    delete p2;
    delete vs;





    try {
	Session u1(vsbe_addr, p);
	u1.connect();
	u1.join();
	while (p->poll(poll_intval));
	fail_unless(u1.is_connected());
	u1.send();
	while (p->poll(poll_intval));
	Session u2(vsbe_addr, p);
	u2.connect();
	u2.join();
	while (p->poll(poll_intval));
	fail_unless(u2.is_connected());
	u1.leave();
	while (p->poll(poll_intval));
	fail_if(u1.is_connected());
	u2.send();
	while (p->poll(poll_intval));
	u2.leave();
	while (p->poll(poll_intval));	
	fail_if(u2.is_connected());
	std::deque<Session *> ss;
	ss.push_back(&u1);
	ss.push_back(&u2);
	verify_views(ss);
    } catch (Exception e) {
	std::cerr << e.what() << "\n";
	fail(e.what());
    }

    try {

	Session u1(vsbe_addr, p);
	u1.connect();
	Session u2(vsbe_addr, p);
	u2.connect();

	// 
	u1.join();
	while (p->poll(poll_intval));
	fail_unless(u1.is_connected());
	u1.send();
	u1.send();
	// Now there are two messages waiting

	
	u2.join();
	// Trans conf messages was put first in queue, reg conf msg as last
	u1.send();
	// Now we should survive...
	while (p->poll(poll_intval));
	fail_unless(u1.is_connected());
	u1.leave();
	u2.leave();
	std::deque<Session *> ss;
	ss.push_back(&u1);
	ss.push_back(&u2);
	verify_views(ss);
    } catch (Exception e) {
	std::cerr << e.what() << "\n";
	fail(e.what());
    }

    delete p;
}
END_TEST

class Random {
public:
    Random() {
        ::srand(12345678);
    }
    Random(long s) {
        ::srand(s);
    }

    static long max() {
        return RAND_MAX;
    }

    // Returns unsigned int between [0, m)
    static unsigned int udraw(unsigned int m) {
        return static_cast<unsigned int>(m*(double(::rand())/(double(max()) + 1.)));
    }
    
    // Returns double between [0., 1.)
    static double ddraw() {
        return double(::rand())/(double(max()) + 1.);
    }
};



START_TEST(check_vs_random)
{
    
    Monitor mon;
    std::deque<Session *> sessions;
    std::list<Session *> active;
    std::deque<Session *> passive;
    
    Poll *p = Poll::create(poll_type);
    
    for (int i = 0; i < 4; i++)
	sessions.push_back(new Session(vsbe_addr, p, &mon));
    
    for (std::deque<Session *>::iterator i = sessions.begin();
	 i != sessions.end(); ++i) {
	passive.push_back(*i);
    }
    
    for (int t = 0; t < 10000; t++) {

	for (std::list<Session *>::iterator i = active.begin();
	     i != active.end(); ++i) {
	    if ((*i)->is_connected() && (*i)->is_leaving() == false) {
		for (unsigned int k = Random::udraw(7); k > 0; k--)
		    (*i)->send();
	    }
	}
	
	if (passive.size() && Random::ddraw() < 0.01) {
	    Session *s = passive.front();
	    assert(s);
	    passive.pop_front();
	    assert(s);
	    active.push_back(s);
	    s->connect();
	    s->join();
	}
	if (active.size() && Random::ddraw() < 0.003) {
	    size_t n = ::rand() % active.size();
	    Session *s = 0;
	    for (std::list<Session *>::iterator i = active.begin(); 
		 i != active.end(); ++i) {
		if (n-- == 0) {
		    s = *i;
		    break;
		}
	    }
	    assert(s);
	    if (s->is_connected() && s->is_leaving() == false)
		s->leave();
	}
	
	
	while (p->poll(1));
	
	std::list<Session *>::iterator i_next;
	for (std::list<Session *>::iterator i = active.begin();
	     i != active.end(); i = i_next) {
	    i_next = i, ++i_next;
	    Session *s = *i;
	    if (s->is_leaving() && s->is_connected() == false){
		s->close();
		active.erase(i);
		passive.push_back(s);
	    }
	}

	if (t % 100 == 0) {
	    while (p->poll(poll_intval) > 0);
	    verify_views(sessions);
	}
    }
    verify_views(sessions);    

    for_each(sessions.begin(), sessions.end(), release);
    delete p;
}
END_TEST

const char* sync_addr = "tcp:127.0.0.1:4567";
const char* async_addr = "asynctcp:127.0.0.1:4567";

class ClientSender : public Thread {
    VS* vs;
public:
    ClientSender(VS* v) : vs(v) {}
    
    void run() {
	char msg[128];
	for (unsigned int i = 0; i < 128; i++) 
	    msg[i] = i % 256;
	while (true) {
	    WriteBuf wb(msg, 128);
	    if (vs->handle_down(&wb, 0))
		::usleep(20);
	}
    }
};

class ClientReceiver : public Thread, public Toplay {
    Poll* poll;
    VS* vs;
public:
    ClientReceiver(Poll *p, VS *v) : poll(p), vs(v) {}

    void handle_up(const int cid, const ReadBuf *rb, const size_t roff,
		   const ProtoUpMeta *um) {
	// Note: Ignores view events.
	if (rb) {
	    logger.debug(std::string("Received message"));
	    const unsigned char* buf = reinterpret_cast<const unsigned char *>(rb->get_buf(roff));
	    for (unsigned int i = 0; i < rb->get_len(roff); i++)
		if (buf[i] != i % 256)
		    abort();
	}
    }

    void run() {
	while (poll->poll(std::numeric_limits<int>::max()));
    }

};


class Client {
    Poll* poll;
    VS* vs;
    Monitor* mon;
    ClientSender* sen;
    ClientReceiver* rec;
public:
    Client() : sen(0), rec(0) {
	mon = new Monitor();
	poll = Poll::create("def");
	vs = VS::create(sync_addr, poll, mon);	
    }

    ~Client() {
	delete sen;
	delete rec;
	delete vs;
	delete poll;
	delete mon;
    }
    
    void start() {
	vs->connect();
	sen = new ClientSender(vs);
	rec = new ClientReceiver(poll, vs);
	vs->join(0, rec);
	// Poll for Trans View
	if (poll->poll(500) == 0)
	    throw FatalException("");
	// Poll for Reg View
	if (poll->poll(500) == 0)
	    throw FatalException("");
	rec->Thread::start();
	sen->Thread::start();
    }
    
    void stop() {
	sen->Thread::stop();
	rec->Thread::stop();
	vs->close();
    }
    
    
};

class Server : public Thread {
    VSServer* s;
public:
    
    Server() {
	s = new VSServer(async_addr);
    }
    
    void run() {
	s->run();
    }
    
    void start() {
	s->start();
	Thread::start();
    }
    
    void stop() {
	Thread::stop();
	s->stop();
    }    
};


START_TEST(check_vs_cliser)
{
    Server s;
    Client c1, c2;

    ::signal(SIGPIPE, SIG_IGN);

    s.start();
    c1.start();
    ::sleep(1);
    c2.start();
    ::sleep(10);
    c2.stop();
    c1.stop();
    s.stop();
    
}
END_TEST


static Suite *suite()
{
    Suite *s = suite_create("vspp");
    TCase *tc;

    tc = tcase_create("check_vsviewid");
    tcase_add_test(tc, check_vsviewid);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_vsview");
    tcase_add_test(tc, check_vsview);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_vsmessage");
    tcase_add_test(tc, check_vsmessage);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_vs");
    tcase_add_test(tc, check_vs);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_vs_random");
    tcase_set_timeout(tc, 15);
    tcase_add_test(tc, check_vs_random);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_vs_cliser");
    tcase_set_timeout(tc, 15);
    tcase_add_test(tc, check_vs_cliser);
    suite_add_tcase(s, tc);

    return s;
}

int main()
{
    Suite *s;
    SRunner *sr;


    if (getenv("POLL_TYPE"))
	poll_type = getenv("POLL_TYPE");
    if (getenv("VSBE_ADDR"))
	vsbe_addr = getenv("VSBE_ADDR");
    s = suite();
    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int n_fail = srunner_ntests_failed(sr);
    srunner_free(sr);
    return n_fail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

