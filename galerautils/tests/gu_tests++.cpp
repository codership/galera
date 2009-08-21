#include "gu_logger.hpp"
#include "gu_network.hpp"
#include "gu_lock.hpp"
#include "gu_prodcons.hpp"

#include <vector>
#include <deque>
#include <algorithm>
#include <functional>
#include <stdexcept>

#include <cstdlib>
#include <cstring>
#include <cassert>

#include <check.h>

using std::vector;
using std::string;
using std::deque;
using std::mem_fun;
using std::for_each;
using namespace gu;
using namespace gu::net;
using namespace gu::prodcons;

static void log_foo()
{
    log_debug << "foo func";
}

static void log_bar()
{
    log_debug << "bar func";
}

static void debug_logger_checked_setup()
{
    gu_log_max_level = GU_LOG_DEBUG;
}

static void debug_logger_checked_teardown()
{
    gu_log_max_level = GU_LOG_INFO;
}



START_TEST(test_debug_logger)
{
    Logger::set_debug_filter("log_foo");
    log_foo();
    log_bar();
}
END_TEST


START_TEST(test_network_listen)
{
    Network net;
    Socket* listener = net.listen("localhost:2112");
    listener->close();
    delete listener;
}
END_TEST

struct listener_thd_args
{
    Network* net;
    int conns;
    const byte_t* buf;
    const size_t buflen;
};

void* listener_thd(void* arg)
{
    listener_thd_args* larg = reinterpret_cast<listener_thd_args*>(arg);
    Network* net = larg->net;
    int conns = larg->conns;
    uint64_t bytes = 0;
    const byte_t* buf = larg->buf;
    const size_t buflen = larg->buflen;
    
    while (conns > 0)
    {
        NetworkEvent ev = net->wait_event(-1);
        
        Socket* sock = ev.get_socket();
        const int em = ev.get_event_mask();
        // log_info << sock << " " << em;

        if (em & NetworkEvent::E_ACCEPTED)
        {
            log_info << "socket accepted";
        }
        else if (em & NetworkEvent::E_ERROR)
        {
            fail_unless(sock != 0);
            if (sock->get_state() == Socket::S_CLOSED)
            {
                log_info << "socket closed";
                delete sock;
                conns--;
            }
            else
            {
                fail_unless(sock->get_state() == Socket::S_FAILED);
                fail_unless(sock->get_errno() != 0);
                log_info << "socket read failed: " << sock->get_errstr();
                sock->close();
                delete sock;
                conns--;
            }
        }
        else if (em & NetworkEvent::E_IN)
        {
            const Datagram* dm = sock->recv();
            fail_unless(dm != 0);
            bytes += dm->get_len();
            if (buf != 0)
            {
                fail_unless(dm->get_len() <= buflen);
                fail_unless(memcmp(dm->get_buf(), buf, dm->get_len()) == 0);
            }
        }
        else if (em & NetworkEvent::E_CLOSED)
        {
            delete sock;
            conns--;
        }
        else if (em & NetworkEvent::E_EMPTY)
        {

        }
        else if (sock == 0)
        {
            log_error << "wut?: " << em;
            return reinterpret_cast<void*>(1);
        }
        else
        {
            log_error <<  "socket " << sock->get_fd() << " event mask: " << ev.get_event_mask();
            return reinterpret_cast<void*>(1);
        }
    }
    log_info << "received " << bytes/(1 << 20) << "MB + " << bytes%(1 << 20) << "B";
    return 0;
}

START_TEST(test_network_connect)
{
    Network* net = new Network;
    Socket* listener = net->listen("localhost:2112");
    listener_thd_args args = {net, 2, 0, 0};
    pthread_t th;
    pthread_create(&th, 0, &listener_thd, &args);
    
    Network* net2 = new Network;
    Socket* conn = net2->connect("localhost:2112");
    
    fail_unless(conn != 0);
    fail_unless(conn->get_state() == Socket::S_CONNECTED);

    Socket* conn2 = net2->connect("localhost:2112");
    fail_unless(conn2 != 0);
    fail_unless(conn2->get_state() == Socket::S_CONNECTED);

    conn->close();
    delete conn;

    log_info << "conn closed";

    conn2->close();
    delete conn2;

    log_info << "conn2 closed";

    void* rval = 0;
    pthread_join(th, &rval);

    listener->close();
    delete listener;

    log_info << "test connect end";

    delete net;
    delete net2;

}
END_TEST

START_TEST(test_network_send)
{
    const size_t bufsize = 1 << 24;
    byte_t* buf = new byte_t[bufsize];
    for (size_t i = 0; i < bufsize; ++i)
    {
        buf[i] = i & 255;
    }

    Network* net = new Network;
    Socket* listener = net->listen("localhost:2112");
    listener_thd_args args = {net, 2, buf, bufsize};
    pthread_t th;
    pthread_create(&th, 0, &listener_thd, &args);
    
    Network* net2 = new Network;
    Socket* conn = net2->connect("localhost:2112");
    
    fail_unless(conn != 0);
    fail_unless(conn->get_state() == Socket::S_CONNECTED);

    Socket* conn2 = net2->connect("localhost:2112");
    fail_unless(conn2 != 0);
    fail_unless(conn2->get_state() == Socket::S_CONNECTED);



    for (int i = 0; i < 100; ++i)
    {
        size_t dlen = std::min(bufsize, static_cast<size_t>(1 + i*1023*170));
        Datagram dm(buf, dlen);
        if (i % 100 == 0)
        {
            log_debug << "sending " << dlen;
        }
        conn->send(&dm);

        dm.reset(buf, ::rand() % 1023 + 1);
    }

    
    conn->close();
    delete conn;
    
    conn2->close();
    delete conn2;
    
    void* rval = 0;
    pthread_join(th, &rval);
    
    listener->close();
    delete listener;
    
    delete net;
    delete net2;
    
    delete[] buf;

}
END_TEST

void* interrupt_thd(void* arg)
{
    Network* net = reinterpret_cast<Network*>(arg);
    NetworkEvent ev = net->wait_event(-1);
    fail_unless(ev.get_event_mask() & NetworkEvent::E_EMPTY);
    return 0;
}

START_TEST(test_network_interrupt)
{
    
    Network net;
    pthread_t th;
    pthread_create(&th, 0, &interrupt_thd, &net);
    
    sleep(1);

    net.interrupt();

    pthread_join(th, 0);

}
END_TEST

static void make_connections(Network& net, 
                             vector<Socket*>& cl,
                             vector<Socket*>& sr,
                             size_t n)
{
    cl.resize(n);
    sr.resize(n);
    for (size_t i = 0; i < n; ++i)
    {
        cl[i] = net.connect("tcp://localhost:2112?socket.non_blocking=1");
    }
    size_t sr_cnt = 0;
    size_t cl_cnt = 0;
    do
    {
        NetworkEvent ev = net.wait_event(-1);
        const int em = ev.get_event_mask();
        if (em & NetworkEvent::E_ACCEPTED)
        {
            log_debug << "accepted";
            sr[sr_cnt++] = ev.get_socket();
        }
        else if (em & NetworkEvent::E_CONNECTED)
        {
            log_debug << "connected";
            cl_cnt++;
        }
        else
        {
            log_warn << "unhandled event " << em;
        }
    }
    while (sr_cnt != n || cl_cnt != n);
}


static void close_connections(Network& net, 
                              vector<Socket*> cl,
                              vector<Socket*> sr)
{
    for_each(cl.begin(), cl.end(), mem_fun(&Socket::close));
    size_t cnt = 0;
    while (cnt != sr.size())
    {
        NetworkEvent ev = net.wait_event(-1);
        const int em = ev.get_event_mask();
        Socket* sock = ev.get_socket();
        if (em & NetworkEvent::E_CLOSED)
        {
            cnt++;
        }
        else if (em & NetworkEvent::E_ERROR)
        {
            log_warn << "error: " << sock->get_errstr();
            cnt++;
        }
        else
        {
            log_debug << "unhandled events: " << em;
        }
    }
}

struct delete_object
{
    template <class T> void operator ()(T* ptr)
    {
        delete ptr;
    }
};

START_TEST(test_network_nonblocking)
{
    Network net;
    
    Socket* listener = net.listen("tcp://localhost:2112?socket.non_blocking=1");
    
    vector<Socket*> cl;
    vector<Socket*> sr;
    
    make_connections(net, cl, sr, 3);
    
    close_connections(net, cl, sr);
    for_each(cl.begin(), cl.end(), delete_object());
    for_each(sr.begin(), sr.end(), delete_object());

    listener->close();
    delete listener;
}
END_TEST


class Thread
{
    volatile bool interrupted;
    pthread_t self;
protected:
    virtual void run() = 0;
public:
    Thread() : 
        interrupted(false),
        self()
    {
    }
    
    virtual ~Thread()
    {
    }
    

    
    virtual void interrupt()
    {
        interrupted = true;
        pthread_cancel(self);
    }
    
    bool is_interrupted() const
    {
        return interrupted;
    }
    
    static void start_fn(Thread* thd)
    {
        thd->run();
        pthread_exit(0);
    }
    
    void start()
    {
        int err = pthread_create(&self, 0, 
                                 reinterpret_cast<void* (*)(void*)>(&Thread::start_fn), this);
        if (err != 0)
        {
            log_error << "could not start thread: " << strerror(err);
            throw std::runtime_error("could not start thread");
        }
    }
    
    void stop()
    {
        interrupt();
        int err = pthread_join(self, 0);
        if (err != 0)
        {
            log_error << "could not join thread: " << strerror(err);
            throw std::runtime_error("could not join thread");
        }
    }
};


class NetConsumer : public Consumer, public Thread
{
    Network net;
    Socket* listener;
    Socket* send_sock;

    NetConsumer(const NetConsumer&);
    void operator=(const NetConsumer&);

public:

    void connect(const string& url)
    {
        send_sock = net.connect(url);
        while (true)
        {
            NetworkEvent ev = net.wait_event(-1);
            const int em = ev.get_event_mask();
            Socket* sock = ev.get_socket();
            if (em & NetworkEvent::E_ACCEPTED)
            {
            }
            else if (em & NetworkEvent::E_CONNECTED)
            {
                fail_unless(sock == send_sock);
                log_info << "connected";
                break;
            }
            else
            {
                throw std::runtime_error("");
            }
        }
    }

    void close()
    {
        send_sock->close();
    }
    
    NetConsumer(const string& url) :
        net(),
        listener(net.listen(url)),
        send_sock(0)
    {
    }

    ~NetConsumer()
    {
        delete listener;
        delete send_sock;
    }
    
    void notify()
    {
        net.interrupt();
    }

    void run()
    {
        size_t sent = 0;
        size_t recvd = 0;
        while (is_interrupted() == false)
        {
            const Message* msg = get_next_msg();

            if (msg != 0)
            {
                const Datagram* dg = reinterpret_cast<const Datagram*>(msg->get_data());
                
                int err = send_sock->send(dg);
                if (err != 0)
                {
                    log_warn << "send: " << strerror(err);
                }
                sent += dg->get_len();
                Message ack(msg->get_producer(), 0, err);
                return_ack(ack);

            }
            
            NetworkEvent ev = net.wait_event(-1);
            const int em = ev.get_event_mask();
            Socket* sock = ev.get_socket();
            if (em & NetworkEvent::E_IN)
            {
                const Datagram* dg = sock->recv();
                fail_unless(dg != 0);
                recvd += dg->get_len();
                fail_unless(recvd <= sent);
            }
            else if (em & NetworkEvent::E_CLOSED)
            {
                delete sock;
            }
            else if (em & NetworkEvent::E_ERROR)
            {
                sock->close();
                delete sock;
            }
            else if (em & NetworkEvent::E_EMPTY)
            {
                /* */
            }
            else
            {
                log_warn << "unhandled event: " << em;
            }
        }
    }

};

START_TEST(test_net_consumer)
{
    string url("tcp://localhost:2112?socket.non_blocking=1");
    NetConsumer cons(url);
    cons.connect(url);
    
    cons.start();
    
    Producer prod(cons);
    byte_t buf[128];
    memset(buf, 0xab, sizeof(buf));
    for (size_t i = 0; i < 1000; ++i)
    {
        if (i % 100 == 0)
        {
            log_debug << "iter " << i;
        }
        Datagram dg(buf, sizeof(buf));
        Message msg(&prod, &dg);
        Message ack;
        prod.send(msg, &ack);
        fail_unless(ack.get_val() == 0 || ack.get_val() == EAGAIN);
    }
    log_debug << "stopping";
    cons.stop();
}
END_TEST

struct producer_thd_args
{
    Consumer& cons;
    size_t n_events;
    pthread_barrier_t barrier;
    producer_thd_args(Consumer& cons_, size_t n_events_, size_t n_thds_) :
        cons(cons_),
        n_events(n_events_),
        barrier()
    {
        if (pthread_barrier_init(&barrier, 0, n_thds_))
        {
            throw std::runtime_error("could not initialize barrier");
        }
    }
};

void* producer_thd(void* arg)
{
    producer_thd_args* pargs = reinterpret_cast<producer_thd_args*>(arg);

    byte_t buf[128];
    memset(buf, 0xab, sizeof(buf));    
    Producer prod(pargs->cons);
    int ret = pthread_barrier_wait(&pargs->barrier);
    if (ret != 0 && ret != PTHREAD_BARRIER_SERIAL_THREAD)
    {
        abort();
    }
    for (size_t i = 0; i < pargs->n_events; ++i)
    {
        Datagram dg(buf, sizeof(buf));
        Message msg(&prod, &dg);
        Message ack;
        prod.send(msg, &ack);
        fail_unless(ack.get_val() == 0 || ack.get_val() == EAGAIN);        
    }
    return 0;
}

START_TEST(test_net_consumer_nto1)
{
    string url("tcp://localhost:2112?socket.non_blocking=1");
    NetConsumer cons(url);
    cons.connect(url);

    cons.start();

    pthread_t thds[8];
    
    producer_thd_args pargs(cons, 1000, 8);
    for (size_t i = 0; i < 8; ++i)
    {
        pthread_create(&thds[i], 0, &producer_thd, &pargs);
    }
    
    for (size_t i = 0; i < 8; ++i)
    {
        pthread_join(thds[i], 0);
    }

    log_debug << "stopping";
    cons.stop();
}
END_TEST

Suite* get_suite()
{
    Suite* s = suite_create("galerautils++");
    TCase* tc;

    tc = tcase_create("test_debug_logger");
    tcase_add_checked_fixture(tc, 
                              &debug_logger_checked_setup,
                              &debug_logger_checked_teardown);
    tcase_add_test(tc, test_debug_logger);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_network_listen");
    tcase_add_test(tc, test_network_listen);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_network_connect");
    tcase_add_checked_fixture(tc, 
                              &debug_logger_checked_setup,
                              &debug_logger_checked_teardown);
    tcase_add_test(tc, test_network_connect);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_network_send");
    tcase_add_checked_fixture(tc, 
                              &debug_logger_checked_setup,
                              &debug_logger_checked_teardown);
    tcase_add_test(tc, test_network_send);
    tcase_set_timeout(tc, 10);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_network_interrupt");
    tcase_add_checked_fixture(tc, 
                              &debug_logger_checked_setup,
                              &debug_logger_checked_teardown);
    tcase_add_test(tc, test_network_interrupt);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_network_nonblocking");
    tcase_add_checked_fixture(tc, 
                              &debug_logger_checked_setup,
                              &debug_logger_checked_teardown);
    tcase_add_test(tc, test_network_nonblocking);
    tcase_set_timeout(tc, 10);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_net_consumer");
    tcase_add_checked_fixture(tc, 
                              &debug_logger_checked_setup,
                              &debug_logger_checked_teardown);
    tcase_add_test(tc, test_net_consumer);
    tcase_set_timeout(tc, 10);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_net_consumer_nto1");
    tcase_add_checked_fixture(tc, 
                              &debug_logger_checked_setup,
                              &debug_logger_checked_teardown);
    tcase_add_test(tc, test_net_consumer_nto1);
    tcase_set_timeout(tc, 10);
    suite_add_tcase(s, tc);

    return s;
}

int main(int argc, char* argv[])
{
    FILE* log_file = fopen ("gu_tests++.log", "w");
    if (!log_file) return EXIT_FAILURE;
    gu_conf_set_log_file (log_file);

    SRunner* sr = srunner_create(get_suite());
    srunner_run_all(sr, CK_NORMAL);
    int n_fail = srunner_ntests_failed(sr);
    srunner_free(sr);
    fclose(log_file);
    return n_fail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
