
#include "check_gcomm.hpp"

#include "gcomm/event.hpp"
#include "gcomm/pseudofd.hpp"

#include <vector>

#include <cstdlib>
#include <sys/types.h>
#include <sys/socket.h>

#include <check.h>

using namespace std;
using namespace gcomm;


START_TEST(test_protolay)
{
    // TODO
}
END_TEST


START_TEST(test_pseudofd)
{
    {
        PseudoFd pfd1;
        PseudoFd pfd2;
        fail_unless(pfd1.get() == numeric_limits<int>::min());
        fail_unless(pfd2.get() == numeric_limits<int>::min() + 1);
    }
    PseudoFd pfd3;
    fail_unless(pfd3.get() == numeric_limits<int>::min());
}
END_TEST


struct UserContext : EventContext
{
    PseudoFd pfd;
    EventLoop& el;
    UserContext(EventLoop& el_) : 
        pfd(),
        el(el_)
    {
        el.insert(pfd.get(), this);
    }
    
    ~UserContext()
    {
        el.erase(pfd.get());
    }

    void handle_event(const int efd, const Event& e)
    {
        fail_unless(efd == pfd.get());
        
        if (e.get_cause() == Event::E_IN)
        {
            char buf[16];
            fail_unless(::recv(pfd.get(), buf, sizeof(buf), 0) == sizeof(buf));
            fail_unless(::recv(pfd.get(), buf, sizeof(buf), MSG_DONTWAIT) == -1);
        }
        if (e.get_cause() == Event::E_OUT)
        {
            
        }
    }
};


START_TEST(test_eventloop_basic)
{
    // TODO
    EventLoop el;
    UserContext uc(el);
}
END_TEST

START_TEST(test_eventloop_user)
{
    // TODO
}
END_TEST

START_TEST(test_eventloop_signal)
{
    // TODO
}
END_TEST

class SelfDestruct : public Protolay, EventContext
{
    PseudoFd fd;
    EventLoop& el;
    SelfDestruct(const SelfDestruct&);
    void operator=(const SelfDestruct&);
public:
    SelfDestruct(EventLoop& el_) : 
        fd(), 
        el(el_){
        el.insert(fd.get(), this);
        el.queue_event(fd.get(), Event(Event::E_USER, Time::now(), 0));
    }
    
    ~SelfDestruct()
    {
    }
    
    int handle_down(WriteBuf* wb, const ProtoDownMeta& dm)
    {
        return EAGAIN;
    }
    
    void handle_up(int cid, const ReadBuf* rb, size_t s,
                   const ProtoUpMeta& um)
    {
        
    }
    
    void handle_event(const int cid, const Event& pe)
    {
        log_info << "self destruct";
        el.erase(fd.get());
        el.release_protolay(this);
    }
};

START_TEST(test_eventloop_gc)
{
    EventLoop el;
    SelfDestruct* sd = new SelfDestruct(el);
    (void)sd;
    el.poll(100);
}
END_TEST

class SelfInterrupt : public Protolay, EventContext
{
    PseudoFd fd;
    EventLoop& el;
    SelfInterrupt(const SelfInterrupt&);
    void operator=(const SelfInterrupt&);
public:
    SelfInterrupt(EventLoop& el_) : 
        fd(), 
        el(el_)
    {
        el.insert(fd.get(), this);
        el.queue_event(fd.get(), Event(Event::E_USER, Time::now(), 0));
    }
    
    
    int handle_down(WriteBuf* wb, const ProtoDownMeta& dm)
    {
        return EAGAIN;
    }
    
    void handle_up(int cid, const ReadBuf* rb, size_t s,
                   const ProtoUpMeta& um)
    {
        
    }
    
    void handle_event(const int cid, const Event& pe)
    {
        log_info << "self interrupt";
        el.interrupt();
    }
};

START_TEST(test_eventloop_interrupt)
{
    EventLoop el;

    SelfInterrupt si(el);
    
    while (el.poll(10) >= 0)
    {
        
    }

    fail_unless(el.is_interrupted() == true);
    
}
END_TEST

Suite* event_suite()
{
    Suite* s = suite_create("event");
    TCase* tc;

    tc = tcase_create("test_protolay");
    tcase_add_test(tc, test_protolay);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_pseudofd");
    tcase_add_test(tc, test_pseudofd);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_eventloop_basic");
    tcase_add_test(tc, test_eventloop_basic);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_eventloop_user");
    tcase_add_test(tc, test_eventloop_user);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_eventloop_signal");
    tcase_add_test(tc, test_eventloop_signal);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_eventloop_gc");
    tcase_add_test(tc, test_eventloop_gc);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_eventloop_interrupt");
    tcase_add_test(tc, test_eventloop_interrupt);
    suite_add_tcase(s, tc);

    return s;
}
