
#include "check_gcomm.hpp"


#include "gcomm/transport.hpp"
#include "gcomm/pseudofd.hpp"
#include "vs.hpp"
#include "vs_message.hpp"

#include <check.h>


using namespace gcomm;

class VSUser : public Toplay, EventContext
{
    Transport* vs;
    EventLoop* el;
    enum State
    {
        CLOSED,
        JOINING,
        OPERATIONAL,
        LEAVING
    };
    State state;
    int fd;
    uint64_t recvd;
public:
    VSUser(const char* uri_str, EventLoop* el_) :
        vs(0),
        el(el_),
        state(CLOSED),
        recvd(0)
    {
        URI uri(uri_str);
        LOG_INFO(uri.to_string());
        vs = Transport::create(URI(uri_str), el);
        connect(vs, this);
        fd = PseudoFd::alloc_fd();
        el->insert(fd, this);
    }

    ~VSUser()
    {
        el->erase(fd);
        PseudoFd::release_fd(fd);
        delete vs;
    }

    void start()
    {
        state = JOINING;
        vs->connect();
    }

    void stop()
    {
        state = LEAVING;
        vs->close();
        state = CLOSED;
    }
    
    void handle_up(const int cid, const ReadBuf* rb, const size_t roff,
                   const ProtoUpMeta* um)
    {
        fail_unless(um != 0);
        if (rb)
        {
            LOG_DEBUG("regular message from " + um->get_source().to_string());
            recvd++;
        }
        else
        {
            
            fail_unless(um->get_view() != 0);
            LOG_INFO("view message: " + um->get_view()->to_string());
            if (state == JOINING)
            {
                fail_unless(um->get_view()->get_type() == View::V_TRANS);
                fail_unless(um->get_view()->get_members().length() == 1);
                state = OPERATIONAL;
                el->queue_event(fd, Event(Event::E_USER, Time(Time::now() + Time(0, 50))));
            }
            LOG_INFO("received in prev view: " + make_int(recvd).to_string());
            recvd = 0;
        }
    }

    void handle_event(const int fd, const Event& ev)
    {
        LOG_TRACE(string("event, state = ") + make_int(state).to_string());
        fail_unless(ev.get_cause() == Event::E_USER);
        if (state == OPERATIONAL)
        {
            char buf[6] = "evsms";
            WriteBuf wb(buf, sizeof(buf));
            int ret = pass_down(&wb, 0);
            Time next;
            if (ret != 0)
            {
                LOG_DEBUG(string("return: ") + strerror(ret));
                next = Time(Time::now() + Time(0, 50000));
            }
            else
            {
                next = Time(Time::now() + Time(0, 50000)); // ::rand()%1000));
            }
            el->queue_event(fd, Event(Event::E_USER, next));
        }
    }

};

START_TEST(test_vs)
{
    EventLoop el;

    VSUser u1("gcomm+vs://127.0.0.1:10001?gmcast.group=evs&node.name=n1&evs.join_wait=500", &el);
    VSUser u2("gcomm+vs://127.0.0.1:10002?gmcast.group=evs&gmcast.node=gcomm+tcp://127.0.0.1:10001&node.name=n2", &el);
    VSUser u3("gcomm+vs://127.0.0.1:10003?gmcast.group=evs&gmcast.node=gcomm+tcp://127.0.0.1:10001&node.name=n3", &el);
    
    u1.start();

    Time stop = Time(Time::now() + Time(5, 0));
    do
    {
        el.poll(100);
    }
    while (Time::now() < stop);

    u2.start();
    stop = Time(Time::now() + Time(5, 0));
    do
    {
        el.poll(100);
    }
    while (Time::now() < stop);


    u3.start();
    stop = Time(Time::now() + Time(5, 0));
    do
    {
        el.poll(100);
    }
    while (Time::now() < stop);

    u1.stop();
    stop = Time(Time::now() + Time(5, 0));
    do
    {
        el.poll(100);
    }
    while (Time::now() < stop);
    u3.stop();

    stop = Time(Time::now() + Time(5, 0));
    do
    {
        el.poll(100);
    }
    while (Time::now() < stop);
    u2.stop();
}
END_TEST


Suite* vs_suite()
{

    Suite* s = suite_create("vs");
    TCase* tc;

    tc = tcase_create("test_vs");
    tcase_add_test(tc, test_vs);
    tcase_set_timeout(tc, 60);
    suite_add_tcase(s, tc);
    
    return s;
}
