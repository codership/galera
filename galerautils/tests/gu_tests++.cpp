#include "gu_logger.hpp"
#include "gu_network.hpp"

#include <cstdlib>

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
    gu::Logger::set_debug_filter("log_foo");
}

static void debug_logger_checked_teardown()
{
    gu_log_max_level = GU_LOG_INFO;
}



START_TEST(test_debug_logger)
{
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

void* listener_thd(void* arg)
{

    // gu::Socket* listener = reinterpret_cast<gu::Socket*>(arg);
    gu::Network* net = reinterpret_cast<gu::Network*>(arg);
    
    gu::NetworkEvent ev = net->wait_event(-1);

    if (ev.get_event_mask() & gu::NetworkEvent::E_ACCEPTED)
    {
        log_info << "accepted socket";
    }
    else
    {
        log_error << "event mask: " << ev.get_event_mask();
        return (void*)1;
    }
    return 0;
}

START_TEST(test_network_connect)
{
    gu::Network net;
    gu::Socket* listener = net.listen("localhost:2112");
    
    pthread_t th;
    pthread_create(&th, 0, &listener_thd, &net);
    
    gu::Network net2;
    gu::Socket* conn = net.connect("localhost:2112");
    
    fail_unless(conn != 0);
    fail_unless(conn->get_state() == gu::Socket::S_CONNECTED);
    
    conn->close();
    delete conn;

    void* rval = 0;
    pthread_join(th, &rval);

    listener->close();
    delete listener;
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
    tcase_add_test(tc, test_network_connect);
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
    return n_fail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
