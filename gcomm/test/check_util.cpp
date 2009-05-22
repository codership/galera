
#include "gcomm/util.hpp"
#include "histogram.hpp"
#include "gcomm/logger.hpp"

#include "check_gcomm.hpp"

#include <vector>
#include <limits>
#include <cstdlib>
#include <check.h>

using std::vector;
using std::numeric_limits;

using namespace gcomm;

START_TEST(test_read)
{
    fail_unless(read_bool("1") == true);
    fail_unless(read_bool("0") == false);
    fail_unless(read_int("345") == 345);
    fail_unless(read_long("123456789") == 123456789);
}
END_TEST


START_TEST(test_cstring_rw)
{
    const char* valid[5] = {"ab", "cdf", "efgh", "ijklm", "opqrst"};
    char buf[25];

    size_t off = 0;
    size_t exp_off = 0;
    for (size_t i = 0; i < 5; ++i) {
        fail_unless((off = write_string(valid[i], buf, sizeof(buf), off))
== exp_off + strlen(valid[i]) + 1);
        exp_off = off;
    }

    off = 0;
    exp_off = 0;
    char* str = 0;

    for (size_t i = 0; i < 5; ++i) {
        fail_unless((off = read_string(buf, sizeof(buf), off, &str)) == exp_off + strlen(valid[i]) + 1);
        fail_unless(strlen(str) == strlen(valid[i]));
        fail_unless(strcmp(str, valid[i]) == 0);
        free(str);
        exp_off = off;
    }
    fail_unless(off == 25);

    memset(buf, 'f', 25);

    fail_unless(read_string(buf, sizeof(buf), 0, &str) == 0);

}
END_TEST

START_TEST(test_stringutil)
{
    string splstr = "1&adfhg&cvc";
    vector<string> strvec = strsplit(splstr, '&');
    fail_unless(strvec.size() == 3);
    fail_unless(strvec[0] == "1");
    fail_unless(strvec[1] == "adfhg");
    fail_unless(strvec[2] == "cvc");
}
END_TEST


START_TEST(test_histogram)
{

    Histogram hs("0.0,0.0005,0.001,0.002,0.005,0.01,0.02,0.05,0.1,0.5,1.,5.");
    
    hs.insert(0.001);
    LOG_INFO(hs.to_string());

    for (size_t i = 0; i < 1000; ++i)
    {
        hs.insert(double(::rand())/RAND_MAX);
    }

    LOG_INFO(hs.to_string());

    hs.clear();

    LOG_INFO(hs.to_string());
}
END_TEST

Suite* util_suite()
{
    Suite* s = suite_create("util");
    TCase* tc;

    tc = tcase_create("test_read");
    tcase_add_test(tc, test_read);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_cstring_rw");
    tcase_add_test(tc, test_cstring_rw);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_stringutil");
    tcase_add_test(tc, test_stringutil);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_histogram");
    tcase_add_test(tc, test_histogram);
    suite_add_tcase(s, tc);

    return s;
}
