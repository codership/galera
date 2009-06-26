#include "gu_logger.hpp"
#include "gu_network.hpp"

#include <cstdlib>
#include <cstring>

#include <check.h>

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
    gu::Logger::set_debug_filter("log_foo");
    log_foo();
    log_bar();
}
END_TEST


START_TEST(test_network_listen)
{
    gu::Network net;
    gu::Socket* listener = net.listen("localhost:2112");
    listener->close();
    delete listener;
}
END_TEST

struct listener_thd_args
{
    gu::Network* net;
    int conns;
    const gu::byte_t* buf;
    const size_t buflen;
};

void* listener_thd(void* arg)
{
    listener_thd_args* larg = reinterpret_cast<listener_thd_args*>(arg);
    gu::Network* net = larg->net;
    int conns = larg->conns;
    uint64_t bytes = 0;
    const gu::byte_t* buf = larg->buf;
    const size_t buflen = larg->buflen;
    
    while (conns > 0)
    {
        gu::NetworkEvent ev = net->wait_event(-1);
        
        gu::Socket* sock = ev.get_socket();
        const int em = ev.get_event_mask();
        // log_info << sock << " " << em;

        if (em & gu::NetworkEvent::E_ACCEPTED)
        {
            log_info << "socket accepted";
        }
        else if (em & gu::NetworkEvent::E_ERROR)
        {
            fail_unless(sock != 0);
            if (sock->get_state() == gu::Socket::S_CLOSED)
            {
                log_info << "socket closed";
                delete sock;
                conns--;
            }
            else
            {
                fail_unless(sock->get_state() == gu::Socket::S_FAILED);
                fail_unless(sock->get_errno() != 0);
                log_info << "socket read failed: " << sock->get_errstr();
                sock->close();
                delete sock;
                conns--;
            }
        }
        else if (em & gu::NetworkEvent::E_IN)
        {
            const gu::Datagram* dm = sock->recv();
            if (dm == 0)
            {
                switch(sock->get_state())
                {
                case gu::Socket::S_FAILED:
                    log_info << "socket recv failed " << sock->get_fd() << ": " 
                             << sock->get_errstr();
                    sock->close();
                case gu::Socket::S_CLOSED:
                    delete sock;
                    conns--;
                    break;
                case gu::Socket::S_CONNECTED:
                    // log_info << "incomplete dgram";
                    break;
                default:
                    fail("unexpected state");
                    break;
                }
            }
            else
            {
                bytes += dm->get_len();
                if (buf != 0)
                {
                    fail_unless(dm->get_len() <= buflen);
                    fail_unless(memcmp(dm->get_buf(), buf, dm->get_len()) == 0);
                }
            }
        }
        else if (sock == 0)
        {
            log_error << "wut?: " << em;
            return (void*)1;
        }
        else
        {
            log_error <<  "socket " << sock->get_fd() << " event mask: " << ev.get_event_mask();
            return (void*)1;
        }
    }
    log_info << "received " << bytes/(1 << 20) << "MB + " << bytes%(1 << 20) << "B";
    return 0;
}

START_TEST(test_network_connect)
{
    gu::Network* net = new gu::Network;
    gu::Socket* listener = net->listen("localhost:2112");
    listener_thd_args args = {net, 2, 0, 0};
    pthread_t th;
    pthread_create(&th, 0, &listener_thd, &args);
    
    gu::Network* net2 = new gu::Network;
    gu::Socket* conn = net2->connect("localhost:2112");
    
    fail_unless(conn != 0);
    fail_unless(conn->get_state() == gu::Socket::S_CONNECTED);

    gu::Socket* conn2 = net2->connect("localhost:2112");
    fail_unless(conn2 != 0);
    fail_unless(conn2->get_state() == gu::Socket::S_CONNECTED);

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
    gu::byte_t* buf = new gu::byte_t[bufsize];
    for (size_t i = 0; i < bufsize; ++i)
    {
        buf[i] = i & 255;
    }

    gu::Network* net = new gu::Network;
    gu::Socket* listener = net->listen("localhost:2112");
    listener_thd_args args = {net, 2, buf, bufsize};
    pthread_t th;
    pthread_create(&th, 0, &listener_thd, &args);
    
    gu::Network* net2 = new gu::Network;
    gu::Socket* conn = net2->connect("localhost:2112");
    
    fail_unless(conn != 0);
    fail_unless(conn->get_state() == gu::Socket::S_CONNECTED);

    gu::Socket* conn2 = net2->connect("localhost:2112");
    fail_unless(conn2 != 0);
    fail_unless(conn2->get_state() == gu::Socket::S_CONNECTED);



    for (int i = 0; i < 100; ++i)
    {
        size_t dlen = std::min(bufsize, static_cast<size_t>(1 + i*1023*170));
        gu::Datagram dm(buf, dlen);
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

Suite* get_suite()
{
    Suite* s = suite_create("galerautils++");
    TCase* tc = tcase_create("test_debug_logger");
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
    tcase_set_timeout(tc, 60);
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
