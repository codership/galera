
#include "check_gcomm.hpp"

#include "gcomm/monitor.hpp"

#include <cstdlib>
#include "check.h"

using namespace gcomm;

struct thd_arg {
    Monitor *m;
    int *valp;
    thd_arg(Monitor *mm, int *va) : m(mm), valp(va) {}
};

void *run_thd(void *argp)
{
    thd_arg *arg = reinterpret_cast<thd_arg *>(argp);
    Monitor *m = arg->m;
    int *val = arg->valp;

    for (int i = 0; i < 10000; i++) {
        int rn = rand()%1000;
        m->enter();
        (*val) += rn;
        pthread_yield();
        fail_unless(*val == rn);
        (*val) -= rn;
        m->leave();
    }
    return 0;
}

START_TEST(test_monitor)
{
    pthread_t thds[8];
    int val = 0;
    Monitor m;
    thd_arg arg(&m, &val);
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
    Monitor *m = arg->m;
    int *val = arg->valp;

    for (int i = 0; i < 10000; i++) {
        int rn = rand()%1000;
        {
            Critical crit(m);
            (*val) += rn;
            pthread_yield();
            fail_unless(*val == rn);
            (*val) -= rn;
        }
    }
    return 0;
}

START_TEST(test_critical)
{
    pthread_t thds[8];
    int val = 0;
    Monitor m;
    thd_arg arg(&m, &val);
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

START_TEST(test_recursive)
{

    Monitor mon;
    
    Critical crit(&mon);
    {
        Critical crit(&mon);
        {
            Critical crit(&mon);
            fail_unless(mon.get_refcnt() == 3);
        }
    }

}
END_TEST


Suite* concurrent_suite()
{
    Suite* s = suite_create("concurrent");
    TCase* tc;

    tc = tcase_create("test_monitor");
    tcase_add_test(tc, test_monitor);
    tcase_set_timeout(tc, 30);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_critical");
    tcase_add_test(tc, test_critical);
    tcase_set_timeout(tc, 30);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_recursive");
    tcase_add_test(tc, test_recursive);
    suite_add_tcase(s, tc);
    
    return s;
}
