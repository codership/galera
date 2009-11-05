
#include "gcomm/util.hpp"
#include "histogram.hpp"
#include "gcomm/logger.hpp"


#include "check_gcomm.hpp"

#include <gu_string.hpp>

#include <vector>
#include <limits>
#include <cstdlib>
#include <check.h>

using std::vector;
using std::numeric_limits;
using std::string;

using namespace gcomm;

using namespace gu::net;





START_TEST(test_histogram)
{

    Histogram hs("0.0,0.0005,0.001,0.002,0.005,0.01,0.02,0.05,0.1,0.5,1.,5.");
    
    hs.insert(0.001);
    log_info << hs;

    for (size_t i = 0; i < 1000; ++i)
    {
        hs.insert(double(::rand())/RAND_MAX);
    }
    
    log_info << hs;
    
    hs.clear();
    
    log_info << hs;
}
END_TEST



Suite* util_suite()
{
    Suite* s = suite_create("util");
    TCase* tc;

    tc = tcase_create("test_histogram");
    tcase_add_test(tc, test_histogram);
    suite_add_tcase(s, tc);



    return s;
}
