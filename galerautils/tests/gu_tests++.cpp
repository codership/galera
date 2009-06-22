
#include "gu_logger.hpp"

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

Suite* get_suite()
{
    Suite* s = suite_create("galerautil++");
    TCase* tc = tcase_create("test_debug_logger");
    tcase_add_checked_fixture(tc, 
                              &debug_logger_checked_setup,
                              &debug_logger_checked_teardown);
    tcase_add_test(tc, test_debug_logger);
    suite_add_tcase(s, tc);
    return s;
}

int main(int argc, char* argv[])
{
    SRunner* sr = srunner_create(get_suite());
    srunner_run_all(sr, CK_NORMAL);
    int n_fail = srunner_ntests_failed(sr);
    srunner_free(sr);
    return n_fail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
