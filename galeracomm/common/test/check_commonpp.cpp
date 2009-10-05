
#include <galerautils.hpp>

#include "galeracomm/poll.hpp"
#include "galeracomm/readbuf.hpp"
#include "galeracomm/writebuf.hpp"
#include "galeracomm/address.hpp"
#include "galeracomm/protolay.hpp"
#include "galeracomm/fifo.hpp"
#include "galeracomm/logger.hpp"
#include "galeracomm/thread.hpp"

#include <cstdlib>
#include <cerrno>
#include <exception>
#include <iostream>
#include <map>
#include <deque>

#include <check.h>
#include <unistd.h>

START_TEST(check_address)
{
    Address ainv1;
    Address ainv2(0xffff, 1, 2);
    Address ainv3(1, 0xff, 2);
    Address ainv4(1, 2, 0xff);
    
    fail_unless(ainv1.is_invalid());
    fail_unless(ainv2.is_invalid());
    fail_unless(ainv3.is_invalid());
    fail_unless(ainv4.is_invalid());

    Address a(1, 2, 3);
    fail_if(a.is_invalid());
    fail_if(a.is_any());
    fail_if(a.is_any_proc());
    fail_if(a.is_any_service());
    fail_if(a.is_any_segment());
    

    Address ap(0, 2, 3);
    fail_if(ap.is_invalid());
    fail_if(ap.is_any());
    fail_unless(ap.is_any_proc());
    fail_if(ap.is_any_service());
    fail_if(ap.is_any_segment());

    Address aser(1, 0, 3);
    fail_if(aser.is_invalid());
    fail_if(aser.is_any());
    fail_if(aser.is_any_proc());
    fail_unless(aser.is_any_service());
    fail_if(aser.is_any_segment());

    Address aseg(1, 2, 0);
    fail_if(aseg.is_invalid());
    fail_if(aseg.is_any());
    fail_if(aseg.is_any_proc());
    fail_if(aseg.is_any_service());
    fail_unless(aseg.is_any_segment());

    Address aany(0, 0, 0);
    fail_if(aany.is_invalid());
    fail_unless(aany.is_any());
    fail_unless(aany.is_any_proc());
    fail_unless(aany.is_any_service());
    fail_unless(aany.is_any_segment());


    char buf[64];

    fail_unless(a.size() == 4);
    fail_unless(a.write(buf, a.size(), 0) != 0);
    
    Address aread;
    fail_unless(aread.read(buf, aread.size(), 0) != 0);

    fail_unless(aread == a);


    Address *avec[2][2][2];
    for (size_t i = 0; i < 2; i++) {
	for (size_t j = 0; j < 2; j++) {
	    for (size_t k = 0; k < 2; k++) {
		avec[i][j][k] = new Address(static_cast<uint16_t>(i + 1), 
                                            static_cast<uint8_t>(j + 1),
                                            static_cast<uint8_t>(k + 1));
	    }
	}
    }
    
    for (size_t i = 0; i < 8; i++) {
	for (size_t j = 0; j < 8; j++) {
	    Address& ai = *avec[i & 1][(i >> 1) & 1][(i >> 2) & 1];
	    Address& aj = *avec[j & 1][(j >> 1) & 1][(j >> 2) & 1];
	    if ((i & 1) == (j & 1)) {
		fail_unless(ai.is_same_proc(aj), "%x,%x", i, j);
	    } else {
		fail_if(ai.is_same_proc(aj), "%x,%x", i, j);
	    }
	    if ((i & 2) == (j & 2)) {
		fail_unless(ai.is_same_service(aj));
	    } else {
		fail_if(ai.is_same_service(aj));
	    }
	    if ((i & 4) == (j & 4)) {
		fail_unless(ai.is_same_segment(aj));
	    } else {
		fail_if(ai.is_same_segment(aj));
	    }
	}
    }

    
    for (size_t i = 0; i < 8; i++) {
	for (size_t j = 0; j < 8; j++) {
	    Address& ai = *avec[i & 1][(i >> 1) & 1][(i >> 2) & 1];
	    Address& aj = *avec[j & 1][(j >> 1) & 1][(j >> 2) & 1];
	    if (i < j)
		fail_unless(ai < aj);
	    else if (i > j)
		fail_unless(aj < ai);
	    else
		fail_unless(ai == aj);
	}
    }
    
    for (size_t i = 0; i < 2; i++) {
	for (size_t j = 0; j < 2; j++) {
	    for (size_t k = 0; k < 2; k++) {
		delete avec[i][j][k];
	    }
	}
    }
    
}
END_TEST

START_TEST(check_readbuf)
{
    const size_t bufsize = 128;
    unsigned char buf[bufsize];
    const unsigned char *ptr;
    ReadBuf *rb = new ReadBuf(buf, bufsize);
 
    fail_unless(rb->get_refcnt() == 1);
    for (size_t i = 0; i < bufsize; i++)
    {
	buf[i] = static_cast<unsigned char>(i & 0xff);
    }
    ReadBuf *rb_copy = rb->copy();
    fail_unless(rb->get_refcnt() == 2);
    rb->release();
    fail_unless(rb_copy->get_refcnt() == 1);    
    memset(buf, 0, bufsize);
    
    ptr = reinterpret_cast<const unsigned char *>(rb_copy->get_buf(0));
    for (size_t i = 0; i < bufsize; i++)
	fail_unless(ptr[i] == i);
 
    ReadBuf *rb_trunc = rb_copy->copy(64);
    
    rb_copy->release();

    ptr = reinterpret_cast<const unsigned char *>(rb_trunc->get_buf(0));
    for (size_t i = 0; i < 64; i++)
	fail_unless(ptr[i] == i + 64);
    rb_trunc->release();


    const void* bufs[3] = {buf, buf + 17, buf + 17 + 45};
    size_t buflens[3] = {17, 45, bufsize - 17 - 45};
    ReadBuf* mrb = new ReadBuf(bufs, buflens, 3, bufsize);
    fail_unless(mrb->get_len() == bufsize);
    fail_unless(memcmp(buf, mrb->get_buf(), bufsize) == 0);
    mrb->release();
}
END_TEST

START_TEST(check_writebuf)
{
    const size_t buflen = 128;
    unsigned char buf[buflen];
    const unsigned char *ptr;

    for (size_t i = 0; i < buflen; i++)
    {
	buf[i] = static_cast<unsigned char>(i & 0xff);
    }
    WriteBuf *wb_copy;
    {
	WriteBuf wb(buf, buflen);    
	wb_copy = wb.copy();
    }
    
    ptr = reinterpret_cast<const unsigned char *>(wb_copy->get_buf());
    for (size_t i = 0; i < buflen; i++)
	fail_unless(ptr[i] == i);
    delete wb_copy;

    WriteBuf *wbp = new WriteBuf(buf, buflen);    

    wb_copy = wbp->copy();
    delete wbp;

    memset(buf, 0, buflen);
    ptr = reinterpret_cast<const unsigned char *>(wb_copy->get_buf());
    for (size_t i = 0; i < buflen; i++)
	fail_unless(ptr[i] == i);
    delete wb_copy;
    

    wbp = new WriteBuf(0, 0);

    unsigned char hdr1[3] = {1, 2, 3};
    unsigned char hdr2[2] = {4, 5};

    wbp->prepend_hdr(hdr1, 3);
    fail_unless(wbp->get_hdrlen() == 3);
    wbp->prepend_hdr(hdr2, 2);
    fail_unless(wbp->get_hdrlen() == 5);
    ptr = reinterpret_cast<const unsigned char *>(wbp->get_hdr());
    fail_unless(ptr[0] == 4);
    fail_unless(ptr[1] == 5);
    fail_unless(ptr[2] == 1);
    fail_unless(ptr[3] == 2);
    fail_unless(ptr[4] == 3);
    wbp->rollback_hdr(2);
    fail_unless(wbp->get_hdrlen() == 3);
    ptr = reinterpret_cast<const unsigned char *>(wbp->get_hdr());
    fail_unless(ptr[0] == 1);
    fail_unless(ptr[1] == 2);
    fail_unless(ptr[2] == 3);
    delete wbp;

    for (size_t i = 0; i < buflen; i++)
    {
	buf[i] = static_cast<unsigned char>(i & 0xff);
    }

    wbp = new WriteBuf(buf, buflen);
    wbp->prepend_hdr(hdr1, 3);
    
    fail_unless(wbp->get_totlen() == buflen + 3);
    
    wb_copy = wbp->copy();
    wbp->rollback_hdr(2);

    fail_unless(wbp->get_hdrlen() == 1);
    ptr = reinterpret_cast<const unsigned char *>(wbp->get_hdr());
    fail_unless(ptr[0] == 3);
    

    fail_unless(wb_copy->get_hdrlen() == 3);
    ptr = reinterpret_cast<const unsigned char *>(wb_copy->get_hdr());
    fail_unless(ptr[0] == 1);
    fail_unless(ptr[1] == 2);
    fail_unless(ptr[2] == 3);

    delete wbp;
    memset(buf, 0, buflen);
    
    fail_unless(wb_copy->get_len() == buflen);
    ptr = reinterpret_cast<const unsigned char *>(wb_copy->get_buf());
    for (size_t i = 0; i < buflen; i++)
	fail_unless(ptr[i] == i);
    delete wb_copy;
}
END_TEST


START_TEST(check_poll)
{
    Poll *p = 0;
    class MyContext : public PollContext {
	int fd;
        long long last_in;
        long long last_out;
    public:
	MyContext() : fd(-1), last_in(0), last_out(0) {}
	MyContext(const int fd_) : fd(fd_), last_in(0), last_out(0) {}
	~MyContext() {}
	void handle(int efd, const PollEnum e, long long tstamp) {
	    char buf[32];
	    std::cout << "Received event: ";
	    std::cout << (e & PollEvent::POLL_IN    ? "POLL_IN "    : "");
	    std::cout << (e & PollEvent::POLL_OUT   ? "POLL_OUT "   : "");
	    std::cout << (e & PollEvent::POLL_ERR   ? "POLL_ERR "   : "");
	    std::cout << (e & PollEvent::POLL_HUP   ? "POLL_HUP "   : "");
	    std::cout << (e & PollEvent::POLL_INVAL ? "POLL_INVAL " : "");
	    std::cout << "\n";
	    if ((e & PollEvent::POLL_IN) && read(efd, buf, 32) <= 0)
		std::cerr << "Error reading fd: " << strerror(errno) << "\n";

            if (e & PollEvent::POLL_IN)  last_in  = tstamp;
            if (e & PollEvent::POLL_OUT) last_out = tstamp;
	}
	int get_fd() const {return fd;}
    };

    // Check ctor 
    try {
	p = Poll::create("Def");
	delete p;
    }
    catch (gu::Exception e) {
	fail(e.what());
    }

    try {
	p = Poll::create("unDef");
	fail("");
    }
    catch (gu::Exception e) {
    }

    // Check others

    try {
	p = Poll::create("Def");	
    }
    catch (gu::Exception e) {
	fail(e.what());
    }

    MyContext c1(-1), c2(4), c3(4);

    try {
	p->insert(c1.get_fd(), &c1);
    }
    catch (gu::Exception e) {
	fail(e.what());
    }

    try {
	p->insert(c2.get_fd(), &c2);
    }
    catch (gu::Exception e) {
	fail(e.what());
    }

    try {
	p->insert(c3.get_fd(), &c3);
	fail("");
    }
    catch (gu::Exception e) {
	std::cout << "Expected exception: " << e.what() << "\n";
    }
    
    try {
	p->set(c1.get_fd(), PollEvent::POLL_IN);
	p->unset(c1.get_fd(), PollEvent::POLL_IN);
	
	p->set(c1.get_fd(), PollEvent::POLL_IN | PollEvent::POLL_OUT);
	p->set(c2.get_fd(), PollEvent::POLL_OUT);
	p->unset(c1.get_fd(), PollEvent::POLL_OUT);
	p->erase(c1.get_fd());
	p->erase(c2.get_fd());
    }
    catch (gu::Exception e) {
	fail(e.what());
    }

    try {
	int fds[2];
	char buf[1] = {0};
	fail_unless(pipe(fds) == 0);
	MyContext ctx;
	p->insert(fds[0], &ctx);
	p->set(fds[0], PollEvent::POLL_IN);
	fail_unless(write(fds[1], buf, 1) == 1);
	p->poll(1);
	
	p->insert(fds[1], &ctx);
	p->set(fds[1], PollEvent::POLL_OUT);
	p->poll(1);
	close(fds[1]);
	
	p->poll(100);
	
	close(fds[0]);
	p->erase(fds[1]);
	
	p->poll(100);
	
	p->erase(fds[0]);
    }
    catch (gu::Exception e) {
	fail(e.what());
    }
    delete p;

}
END_TEST

#if 0
//
// Moved this code into fifo and poll files but keeping
// code here as reference in case it is needed
//
static void release(ReadBuf *rb) 
{
    rb->release();
}

class QTrans {
    std::deque<ReadBuf *> mque;
    int read_fd;
    int write_fd;
public:
    QTrans(const int rfd, const int wfd) : read_fd(rfd), write_fd(wfd) {}
    ~QTrans() {
	for_each(mque.begin(), mque.end(), release);
    }
    int get_read_fd() const {return read_fd;}
    int get_write_fd() const {return write_fd;}
    
    int push_back(const WriteBuf *wb) {
	if (is_full() == false) {
	    mque.push_back(wb->to_readbuf());
	    return 0;
	} else {
	    return EAGAIN;
	}
    }
	
    ReadBuf *pop_front() {
	ReadBuf *rb = 0;
	if (mque.size() > 0) {
	    rb = *mque.begin();
	    mque.pop_front();
	}
	return rb;
    }

    bool is_empty() const {
	return mque.size() == 0;
    }

    bool is_full() const {
	return mque.size() == 2;
    }
};

class QPoll : public Poll {
    std::map<const int, PollContext *> ctx_map;
    std::map<const int, std::pair<PollEnum, QTrans *> > que_map;
public:
    void insert(const int fd, PollContext *ctx) {
	ctx_map.insert(std::pair<const int, PollContext *>(fd, ctx));
    }
    void erase(const int fd) {
	unset(fd, PollEvent::POLL_ALL);
	ctx_map.erase(fd);
    }
    void set(const int fd, const PollEnum e) {
	std::map<const int, PollContext *>::iterator ctxi = ctx_map.find(fd);
	if (ctxi == ctx_map.end())
	    throw Exception("Invalid fd");
	std::map<const int, std::pair<PollEnum, QTrans *> >::iterator p = que_map.find(fd);
	if (p == que_map.end())
	    throw Exception("Invalid fd");
	p->second.first |= e;
    }
    void unset(const int fd, const PollEnum e) {
	std::map<const int, std::pair<PollEnum, QTrans *> >::iterator p = que_map.find(fd);
	if (p == que_map.end())
	    throw Exception("Invalid fd");    
	p->second.first &= ~e;
    }

    int poll(int tout) {
	int n = 0;
	std::map<const int, std::pair<PollEnum, QTrans *> >::iterator i;
	std::map<const int, PollContext *>::iterator pi;
	for (i = que_map.begin(); i != que_map.end(); ++i) {
	    if ((i->second.first & PollEvent::POLL_IN) && (i->second.second->is_empty() == false)) {
		pi = ctx_map.find(i->second.second->get_read_fd());
		pi->second->handle(pi->first, PollEvent::POLL_IN);
		n++;
	    }
	    if ((i->second.first & PollEvent::POLL_OUT) && (i->second.second->is_full() == false)) {
		pi = ctx_map.find(i->second.second->get_write_fd());
		pi->second->handle(pi->first, PollEvent::POLL_OUT);
		n++;
	    }
	}
	return n;
    }
    
    void reg_qtrans(QTrans *qt) {
	std::pair<PollEnum, QTrans *> p(PollEvent::POLL_NONE, qt);
	int fd = qt->get_read_fd();
	std::pair<const int, std::pair<PollEnum, QTrans*> > pread(fd, p);
	if (que_map.insert(pread).second == false)
	    throw std::exception();
	fd = qt->get_write_fd();
	std::pair<const int, std::pair<PollEnum, QTrans*> > pwrite(fd, p);
	if (que_map.insert(pwrite).second == false)
	    throw std::exception();
    }
};

#endif // 0

START_TEST(check_protolay)
{
    

    class Proto1 : public Toplay {
    public:
	Proto1() {}
	void handle_up(const int cid, const ReadBuf *rb, const size_t off, const ProtoUpMeta *up_meta) {
	    
	    std::cout << "Proto1::handle_up(): "
                      << (reinterpret_cast<const char*>(rb->get_buf()))
                      << "\n";
	}
    };
    
    
    class Proto2 : public Protolay {
    public:
	Proto2() {}
	void handle_up(const int cid, const ReadBuf *rb, const size_t off, const ProtoUpMeta *up_meta) {
	    
	    std::cout << "Proto2::handle_up()\n";
	    pass_up(rb, off, 0);
	}
	int handle_down(WriteBuf *wb, const ProtoDownMeta *down_meta) {
	    return pass_down(wb, down_meta);
	}
    };
    
    class Proto3 : public Bottomlay, public PollContext {
	Poll *poll;
	Fifo *qtrans;
	bool writable;

        long long last_in;
        long long last_out;

        Proto3 (const Proto3&);
        void operator= (const Proto3&);

    public:

	Proto3(Poll *p, Fifo *qt) : poll(p),
				    qtrans(qt),
				    writable(true),
                                    last_in(0),
                                    last_out(0)
        {
	    poll->insert(qtrans->get_read_fd(), this);
	    poll->insert(qtrans->get_write_fd(), this);
	    poll->set(qtrans->get_read_fd(), PollEvent::POLL_IN);
	}
	
	void handle(const int fd, PollEnum e, long long tstamp)
        {
	    if (e & PollEvent::POLL_IN)
            {
                last_in = tstamp;

		if (fd != qtrans->get_read_fd()) throw std::exception();

		ReadBuf *rb = qtrans->pop_front();
		pass_up(rb, 0, 0);
		rb->release();
	    }

	    if (e & PollEvent::POLL_OUT)
            {
                last_out = tstamp;

		if (fd != qtrans->get_write_fd()) throw std::exception();

		writable = true;
		poll->unset(qtrans->get_write_fd(), PollEvent::POLL_OUT);
	    }
	}
	
	int handle_down(WriteBuf *wb, const ProtoDownMeta *down_meta) {
	    int ret = qtrans->push_back(wb);
	    if (ret == EAGAIN) {
		writable = false;
		poll->set(qtrans->get_write_fd(), PollEvent::POLL_OUT);
	    }
	    return ret;
	}
	
	bool get_writable() const {return writable;}
    };

    Fifo qtrans(2);
    Poll *qpoll = Poll::create("fifo");
    
    Proto1 p1;
    Proto2 p2;
    Proto3 p3(qpoll, &qtrans);
    
    p1.set_down_context(&p2);
    p2.set_up_context(&p1);
    
    p2.set_down_context(&p3);
    p3.set_up_context(&p2);
    
    unsigned char buf[8] = {'1', '2' ,'3' ,'4' ,'5' ,'6' ,'7', '\0'};
    WriteBuf *wb = new WriteBuf(buf, 8);
    p1.pass_down(wb, 0);
    qpoll->poll(0);
    
    p1.pass_down(wb, 0);
    p1.pass_down(wb, 0);
    fail_unless(p1.pass_down(wb, 0) == EAGAIN);
    fail_unless(p3.get_writable() == false);
    qpoll->poll(0);
    fail_unless(p3.get_writable() == true);
    
    delete wb;
    delete qpoll;

}
END_TEST

struct thd_arg {
    const gu::Monitor& m;
    int *valp;
    thd_arg(const gu::Monitor& mm, int *va) : m(mm), valp(va) {}
};

void *run_thd(void *argp)
{
    thd_arg *arg = reinterpret_cast<thd_arg *>(argp);
    const gu::Monitor& m = arg->m;
    int *val = arg->valp;

    for (int i = 0; i < 10000; i++) {
	int rn = rand()%1000;
	m.enter();
	(*val) += rn;
	pthread_yield();
	fail_unless(*val == rn);
	(*val) -= rn;
	m.leave();
    }
    return 0;
}

START_TEST(check_monitor)
{
    pthread_t thds[8];
    int val = 0;
    gu::Monitor m;
    thd_arg arg(m, &val);
    m.enter();
    for (int i = 0; i < 8; i++) {
	pthread_create(&thds[i], 0, &run_thd, &arg);
    }
    m.leave();
    
    for (int i = 0; i < 8; i++) {
	pthread_join(thds[i], 0);
    }

}
END_TEST


void *run_thd_crit(void *argp)
{
    thd_arg *arg = reinterpret_cast<thd_arg *>(argp);
    const gu::Monitor& m = arg->m;
    int *val = arg->valp;

    for (int i = 0; i < 10000; i++) {
	int rn = rand()%1000;
	{
	    gu::Critical crit(m);
	    (*val) += rn;
	    pthread_yield();
	    fail_unless(*val == rn);
	    (*val) -= rn;
	}
    }
    return 0;
}


START_TEST(check_critical)
{
    pthread_t thds[8];
    int val = 0;
    gu::Monitor m;
    thd_arg arg(m, &val);
    m.enter();
    for (int i = 0; i < 8; i++) {
	pthread_create(&thds[i], 0, &run_thd_crit, &arg);
    }
    m.leave();
    
    for (int i = 0; i < 8; i++) {
	pthread_join(thds[i], 0);
    }
}
END_TEST

class ThreadImpl : public Thread {
    bool yield;
public:

    ThreadImpl() : yield(false) {}
    
    void run() {
	if (yield)
	    pthread_yield();
	// std::cerr << "Thread test: ";
	if (yield)
	    pthread_yield();
	for (int i = 0; i < 10; i++) {
	    if (yield)
		pthread_yield();
	    // std::cerr << i;
	}
	// std::cerr << " sleeping" << std::endl;
	::sleep(1000);
    }

    void set_yield() {
	yield = true;
    }

};

START_TEST(check_thread)
{
    ThreadImpl thd;

    thd.start();
    ::sleep(1);
    thd.stop();

    thd.start();
    thd.stop();

    try {
	thd.stop();
	fail("Should have thrown");
    } catch (FatalException e) {

    }

    thd.start();
    
    try {
	thd.start();
	fail("Should have thrown");
    } catch (FatalException e) {

    }
    thd.stop();


    thd.set_yield();
    for (int i = 0; i < 2000; i++) {
	thd.start();
	thd.stop();
    }


}
END_TEST

#if 0
START_TEST(check_logger)
{
    ::setenv("LOGGER_LEVEL", "0", 1);
    // TODO: Make better check
    Logger& logger = Logger::instance();
    
    logger.trace("this is da trace");
    logger.debug("this is da debug");
    logger.info("this is da info");
    logger.warning("this is da warning");
    logger.error("this is da error");
    logger.fatal("this is da fatal");
}
END_TEST
#endif

static Suite *suite()
{
    Suite *s;
    TCase *tc;

    s = suite_create("commonpp");

#if 0
    tc = tcase_create("check_logger");
    tcase_add_test(tc, check_logger);
    suite_add_tcase(s, tc);
#endif

    tc = tcase_create("check_address");
    tcase_add_test(tc, check_address);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_poll");
    tcase_add_test(tc, check_poll);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_readbuf");
    tcase_add_test(tc, check_readbuf);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_writebuf");
    tcase_add_test(tc, check_writebuf);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_protolay");
    tcase_add_test(tc, check_protolay);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_monitor");
    tcase_add_test(tc, check_monitor);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_critical");
    tcase_add_test(tc, check_critical);
    suite_add_tcase(s, tc);

    tc = tcase_create("check_thread");
    tcase_add_test(tc, check_thread);
    suite_add_tcase(s, tc);



    return s;
}

int main(int argc, char *argv[])
{
    int n_fail = 0;
    Suite *s;
    SRunner *sr;

    s = suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    n_fail = srunner_ntests_failed(sr);
    srunner_free(sr);
    
    return n_fail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
