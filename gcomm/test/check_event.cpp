
#include "check_gcomm.hpp"

#include "gcomm/event.hpp"
#include "gcomm/pseudofd.hpp"

#include <cstdlib>
#include <sys/types.h>
#include <sys/socket.h>

#include <check.h>

using namespace gcomm;

START_TEST(test_protolay)
{
    // TODO
}
END_TEST


START_TEST(test_pseudofd)
{
    std::set<int> fds;
    
    for (int i = 0; i < 100; ++i)
        fail_unless(fds.insert(PseudoFd::alloc_fd()).second == true);
    
    while (fds.empty() == false) {
        if (::rand() % 5 == 0)
            fail_unless(fds.insert(PseudoFd::alloc_fd()).second == true);
        int fd;
        if (::rand() % 3 == 0)
            fd = *fds.begin();
        else
            fd = *fds.rbegin();
        PseudoFd::release_fd(fd);
        fds.erase(fd);
    }
}
END_TEST

struct UserContext : EventContext
{
    int fd;
    
    UserContext(const int fd_) : 
        fd(fd_)
    {
    }
    
    void handle_event(const int efd, const Event& e)
    {
        fail_unless(efd == fd);
        
        if (e.get_cause() == Event::E_IN)
        {
            char buf[16];
            fail_unless(::recv(fd, buf, sizeof(buf), 0) == sizeof(buf));
            fail_unless(::recv(fd, buf, sizeof(buf), MSG_DONTWAIT) == -1);
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
    
    int fd = PseudoFd::alloc_fd();
    UserContext uc(fd);
    
    el.insert(fd, &uc);
    el.erase(fd);
    
    PseudoFd::release_fd(fd);
    
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
    int fd;
    EventLoop* el;
public:
    SelfDestruct(EventLoop* el_) : 
        fd(PseudoFd::alloc_fd()), 
        el(el_){
        el->insert(fd, this);
        el->queue_event(fd, Event(Event::E_USER, Time::now(), 0));
    }
    
    
    int handle_down(WriteBuf* wb, const ProtoDownMeta* dm)
    {
        return EAGAIN;
    }
    
    void handle_up(const int cid, const ReadBuf* rb, const size_t s,
                   const ProtoUpMeta* um)
    {
        
    }
    
    void handle_event(const int cid, const Event& pe)
    {
        LOG_INFO("self destruct");
        el->erase(fd);
        el->release_protolay(this);
    }
};

START_TEST(test_eventloop_gc)
{
    EventLoop el;
    SelfDestruct* sd = new SelfDestruct(&el);
    (void)sd;
    el.poll(100);
}
END_TEST

class SelfInterrupt : public Protolay, EventContext
{
    int fd;
    EventLoop* el;
public:
    SelfInterrupt(EventLoop* el_) : 
        fd(PseudoFd::alloc_fd()), 
        el(el_){
        el->insert(fd, this);
        el->queue_event(fd, Event(Event::E_USER, Time::now(), 0));
    }
    
    
    int handle_down(WriteBuf* wb, const ProtoDownMeta* dm)
    {
        return EAGAIN;
    }
    
    void handle_up(const int cid, const ReadBuf* rb, const size_t s,
                   const ProtoUpMeta* um)
    {
        
    }
    
    void handle_event(const int cid, const Event& pe)
    {
        LOG_INFO("self interrupt");
        el->interrupt();
    }
};

START_TEST(test_eventloop_interrupt)
{
    EventLoop el;

    SelfInterrupt si(&el);

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
