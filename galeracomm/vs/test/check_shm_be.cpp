extern "C" {
#include "vs_backend.h"
#include "vs_backend_shm.h"
#include "gcomm/vs_msg.h"
#include "gcomm/vs_view.h"
#include "gcomm/protolay.h"
}

#include <check.h>


#include <deque>
#include <list>
#include <set>

#include <cstdlib>
#include <cstdio>

static readbuf_t *readbuf_from_writebuf(writebuf_t *wb)
{
    readbuf_t *rb, *ret;
    size_t buflen;
    char *buf;
    
    buflen = writebuf_get_hdrlen(wb) + writebuf_get_payloadlen(wb);
    buf = new char[buflen];
    
    memcpy(buf, writebuf_get_hdr(wb), writebuf_get_hdrlen(wb));
    memcpy(buf + writebuf_get_hdrlen(wb), writebuf_get_payload(wb),
	   writebuf_get_payloadlen(wb));

    rb = readbuf_new(buf, buflen);
    ret = readbuf_copy(rb);
    readbuf_free(rb);
    delete[] buf;
    return ret;
}

static bool msg_equal(const vs_msg_t *a, const vs_msg_t *b)
{
    if (vs_msg_get_type(a) != vs_msg_get_type(b))
	return false;
    if (vs_msg_get_source(a) != vs_msg_get_source(b))
	return false;
    if (vs_msg_get_seq(a) != vs_msg_get_seq(b))
	return false;
    if (vs_msg_get_safety(a) != vs_msg_get_safety(b))
	return false;

    if (vs_msg_get_payload_len(a) != 
	vs_msg_get_payload_len(b))
	return false;
    
    if (memcmp(vs_msg_get_payload(a), 
	       vs_msg_get_payload(b), 
	       vs_msg_get_payload_len(a)) != 0)
	return false;
    
    return true;
}

class CompleteView {
    vs_view_t *reg_view;
    std::deque<vs_msg_t *> reg_msgs;
    vs_view_t *trans_view;
    std::deque<vs_msg_t *> trans_msgs;
public:

    CompleteView(const vs_view_t *rv, const vs_view_t *tv) {
        reg_view = vs_view_copy(rv);
	trans_view = vs_view_copy(tv);
    }
    
    ~CompleteView() {
	vs_view_free(reg_view);
	vs_view_free(trans_view);
    }

    void addRegMsg(const vs_msg_t *msg) {
	reg_msgs.push_back(vs_msg_copy(msg));
    }

    void addTransMsg(const vs_msg_t *msg) {
	trans_msgs.push_back(vs_msg_copy(msg));
    }

    struct PtrLessThan {
	bool operator()(const CompleteView *a, const CompleteView *b) const {
	    addr_t a_addr;
	    addr_t b_addr;
	    addr_set_t *intersection;
	    if (vs_view_get_id(a->reg_view) < vs_view_get_id(b->reg_view))
		return false;

	    fail_unless(addr_set_equal(vs_view_get_addr(a->reg_view),
				       vs_view_get_addr(b->reg_view)));
	    
	    a_addr = addr_cast(addr_set_first(vs_view_get_addr(a->trans_view)));
	    b_addr = addr_cast(addr_set_first(vs_view_get_addr(b->trans_view)));

	    if (a_addr != b_addr) {
		intersection = addr_set_intersection(
		    vs_view_get_addr(a->trans_view),
		    vs_view_get_addr(b->trans_view));
		fail_unless(addr_set_size(intersection) == 0);
		addr_set_free(intersection);
	    } else {
		fail_unless(addr_set_equal(
				vs_view_get_addr(a->trans_view),
				vs_view_get_addr(b->trans_view)));
	    }
	    return a_addr < b_addr;
	}
    };
};


class BeContext {
    protolay_t *pl;
    vs_backend_t *be;
    seq_t send_seq;
    bool connected;
public:

    std::deque<vs_msg_t *> recv_msgs;
    std::deque<vs_msg_t *> sent_msgs;
    
    static void passUpCb(protolay_t *pl, const readbuf_t *rb, 
			 const size_t roff, const up_meta_t *up_meta) {
	BeContext *bctx = reinterpret_cast<BeContext*>(protolay_get_priv(pl));
	vs_msg_t *msg;
	vs_view_t *view;
	msg = vs_msg_read(rb, roff);
	bctx->recv_msgs.push_back(vs_msg_copy(msg));
	if (vs_msg_get_type(msg) == VS_MSG_REG_CONF) {
	    fail_unless(vs_view_read(vs_msg_get_payload(msg), 
				     vs_msg_get_payload_len(msg),
				     0, &view));
	    fail_unless(vs_view_get_type(view) == VS_VIEW_REG);
	    fail_unless(vs_view_find_addr(view, 
					  vs_backend_get_self_addr(bctx->be)));
	    bctx->connected = true;
	} else if (vs_msg_get_type(msg) == VS_MSG_TRANS_CONF) {
	    fail_unless(vs_view_read(vs_msg_get_payload(msg), 
				     vs_msg_get_payload_len(msg),
				     0, &view));
	    fail_unless(vs_view_get_type(view) == VS_VIEW_TRANS);
	    if (vs_view_get_size(view) == 0) {
		bctx->connected = false;
	    } else {
		fail_unless(vs_view_find_addr(
				view, vs_backend_get_self_addr(bctx->be)));
	    }
	    vs_view_free(view);
	}
	vs_msg_free(msg);
    }

    BeContext() : pl(0), be(0), send_seq(0), connected(false) {
	fail_unless(!!(pl = protolay_new(this, NULL)));
	fail_unless(!!(be = vs_backend_new("shm:", 
					   NULL, pl, &passUpCb)));
    }
    
    ~BeContext() {
	vs_backend_free(be);
	std::deque<vs_msg_t *>::iterator i;
	for (i = recv_msgs.begin(); i != recv_msgs.end(); ++i)
	    vs_msg_free(*i);
	for (i = sent_msgs.begin(); i != sent_msgs.end(); ++i)
	    vs_msg_free(*i);
	protolay_free(pl);
    }

    bool canJoin() {
	return connected == false;
    }
    
    bool canLeave() {
	return connected;
    }

    void connect() {
	fail_unless(vs_backend_connect(be, 0) == 0);
    }
    
    void close() {
	vs_backend_close(be);
    }
    
    addr_t getSelfAddr() {
	return vs_backend_get_self_addr(be);
    }
    
    void genMessage() {
	vs_msg_t *msg;
	writebuf_t *wb;
	readbuf_t *rb;
	size_t buflen;
	char buf[128];	
	// char buf2[256];
	msg = vs_msg_new(VS_MSG_DATA, getSelfAddr(), 0, send_seq++, 
			 VS_MSG_SAFETY_SAFE);
	buflen = 64 + rand()%64;
	for (size_t i = 0; i < buflen; i++)
	    buf[i] = rand()%256;
	wb = writebuf_new(buf, buflen);
	writebuf_prepend_hdr(wb, vs_msg_get_hdr(msg), vs_msg_get_hdrlen(msg));
	protolay_pass_down(pl, wb, NULL);
	vs_msg_free(msg);
	rb = readbuf_from_writebuf(wb);
	writebuf_free(wb);
	
	msg = vs_msg_read(rb, 0);
	sent_msgs.push_back(vs_msg_copy(msg));
	vs_msg_free(msg);
	readbuf_free(rb);

    }

    void poll() {
	fail_unless(protolay_poll_up(pl) == 0);
    }

};

START_TEST(testBeSimple)
{
    BeContext *bctx;

    bctx = new BeContext();

    bctx->connect();
    bctx->poll();
    bctx->getSelfAddr();
    bctx->genMessage();
    bctx->poll();
    bctx->close();
    bctx->poll();
    
    std::deque<vs_msg_t *>::iterator i = bctx->recv_msgs.begin();
    fail_if(i == bctx->recv_msgs.end());
    fail_unless(vs_msg_get_type(*i) == VS_MSG_TRANS_CONF);
    fail_if(++i == bctx->recv_msgs.end());
    fail_unless(vs_msg_get_type(*i) == VS_MSG_REG_CONF);
    fail_if(++i == bctx->recv_msgs.end());    
    fail_unless(vs_msg_get_type(*i) == VS_MSG_DATA);
    fail_unless(msg_equal(*i, *bctx->sent_msgs.begin()));
    fail_if(++i == bctx->recv_msgs.end());    
    fail_unless(vs_msg_get_type(*i) == VS_MSG_TRANS_CONF);
    fail_unless(++i == bctx->recv_msgs.end());

    delete bctx;

}
END_TEST

static CompleteView *extract_complete_view(std::deque<vs_msg_t *>& msgs)
{
    CompleteView *ret = 0;
    vs_view_t *trans_view = 0;
    vs_view_t *reg_view = 0;
    vs_view_t *view;
    bool complete = false;
    std::deque<vs_msg_t *>::iterator trans_msg = msgs.end();
    std::deque<vs_msg_t *>::iterator reg_msg = msgs.end();

    for (std::deque<vs_msg_t *>::iterator i = msgs.begin();
	 i != msgs.end() && complete == false;) {
	std::deque<vs_msg_t *>::iterator to_del = msgs.end();
	if (vs_msg_is_conf(*i)) {
	    
	    fail_unless(vs_view_read(vs_msg_get_payload(*i), 
				     vs_msg_get_payload_len(*i),
				     0, &view));
	    if (vs_view_get_type(view) == VS_VIEW_TRANS) {
		if (reg_msg == msgs.end()) {
		    fail_unless(vs_view_get_size(view) == 0);
		    to_del = i;
		} else {
		    trans_msg = i;
		    if (vs_view_get_size(view) == 0)
			complete = true;
		}
	    } else {
		fail_unless(reg_msg != msgs.end() || trans_msg == msgs.end());
		if (reg_msg == msgs.end()) {
		    reg_msg = i;
		} else {
		    complete = true;
		}
	    }
	    vs_view_free(view);
	    
	}
	i++;
	if (to_del != msgs.end()) {
	    vs_msg_free(*to_del);
	    msgs.erase(to_del);
	}
    }

    if (complete) {
	fail_unless(vs_view_read(vs_msg_get_payload(*trans_msg), 
				 vs_msg_get_payload_len(*trans_msg), 
				 0,
				 &trans_view));
	fail_unless(vs_view_read(vs_msg_get_payload(*reg_msg),
				 vs_msg_get_payload_len(*reg_msg),
				 0,
				 &reg_view));
    }


    vs_view_free(trans_view);
    vs_view_free(reg_view);

    return ret;
}

static void verify_be_view(const CompleteView *cv, BeContext *be)
{


}

static void verify_view(const CompleteView *cv, std::list<BeContext *>& bees)
{
    for (std::list<BeContext *>::iterator i = bees.begin(); 
	 i != bees.end(); i++)
	verify_be_view(cv, *i);
}

static void checkpoint(std::list<BeContext *>& bees)
{
    CompleteView *cv;
    std::set<CompleteView *> complete;
    unsigned int n_complete;
    
    do {
	for (std::list<BeContext *>::iterator i = bees.begin(); 
	     i != bees.end(); i++) {
	    if ((cv = extract_complete_view((*i)->recv_msgs))) {
		if (complete.insert(cv).second == false)
		    delete cv;
	    }
	}
	for (std::set<CompleteView *>::iterator i = complete.begin(); 
	     i != complete.end(); i++) {
	    verify_view(*i, bees);
	    delete *i;
	}
	complete.erase(complete.begin(), complete.end());
    } while (n_complete != 0);
}

START_TEST(testBeRandom)
{
    std::list<BeContext *> bees;
    std::list<BeContext *> active;
    std::list<BeContext *> inactive;
    
    int n_backends = 32;
    double join_prob = 0.02, 
	leave_prob = 0.008;
    
    for (int i = 0; i < n_backends; i++) {
	BeContext *be = new BeContext();
	bees.push_back(be);
	inactive.push_back(be);
    }
    
    for (int t = 0; t < 10000; t++) {
	if ((double)rand()/RAND_MAX < join_prob &&
	    inactive.size()) {
	    BeContext *be = *inactive.begin();
	    if (be->canJoin()) {
		inactive.pop_front();
		be->connect();
		active.push_back(be);
	    }
	}
	if ((double)rand()/RAND_MAX < leave_prob &&
	    active.size()) {
	    BeContext *be = *active.begin();
	    if (be->canLeave()) {
		active.pop_front();
		be->close();
		inactive.push_back(be);
	    }
	}

	for (std::list<BeContext *>::iterator i = active.begin();
	     i != active.end(); i++) {
	    for (int m = rand()%3; m > 0; m--)
		(*i)->genMessage();
	}
	

	if (rand() % 11 == 0 && active.size()) {
	    (*active.begin())->poll();
	}
	if (t % 10 == 0)
	    printf("t = %i\n", t);

	if (t % 50 == 0)
	    checkpoint(bees);

	if (rand() % 300 == 0)
	    vs_backend_shm_split();
	if (rand() % 150 == 0)
	    vs_backend_shm_merge();
	
    }

}
END_TEST

static Suite *suite()
{
    Suite *s;
    TCase *tc;

    s = suite_create("shm backend");
    tc = tcase_create("testBeSimple");
    tcase_add_test(tc, testBeSimple);
    suite_add_tcase(s, tc);

    tc = tcase_create("testBeRandom");
    tcase_add_test(tc, testBeRandom);
    tcase_set_timeout(tc, 360);
    suite_add_tcase(s, tc);

    return s;
}

int main(int argc, char *argv[])
{
    int nfail;
    Suite *s;
    SRunner *sr;
    s = suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    nfail = srunner_ntests_failed(sr);
    srunner_free(sr);
    
    return nfail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;;
}
