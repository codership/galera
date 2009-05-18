#include "check_gcomm.hpp"

#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <check.h>

using std::string;
using std::vector;

typedef Suite* (*suite_creator_f)();

struct GCommSuite
{
    string name;
    suite_creator_f suite;
};

static GCommSuite suites[] = {
    {"util", util_suite},
    {"types", types_suite},
    {"buffer", buffer_suite},
    {"event", event_suite},
    {"concurrent", concurrent_suite},
    {"tcp", tcp_suite},
    {"gmcast", gmcast_suite},
    {"evs", evs_suite},
    {"vs", vs_suite},
    {"", 0}
};

static vector<string> strsplit(const string& s, const int c)
{
    vector<string> ret;
    
    size_t pos, prev_pos = 0;
    while ((pos = s.find_first_of(c, prev_pos)) != string::npos)
    {
        ret.push_back(s.substr(prev_pos, pos - prev_pos));
        prev_pos = pos + 1;
    }
    ret.push_back(s.substr(prev_pos, s.length() - prev_pos));
    return ret;
}

int main(int argc, char* argv[])
{
    SRunner* sr = srunner_create(0);
    vector<string>* suits = 0;

    if (::getenv("CHECK_GCOMM_SUITES"))
    {
        suits = new vector<string>(strsplit(::getenv("CHECK_GCOMM_SUITES"), ','));
    }
    
    for (size_t i = 0; suites[i].suite != 0; ++i)
    {
        if (suits == 0 || 
            find(suits->begin(), suits->end(), suites[i].name) != suits->end())
        {
            srunner_add_suite(sr, suites[i].suite());
        }
    }
    delete suits;
    suits = 0;

    srunner_run_all(sr, CK_NORMAL);
    int n_fail = srunner_ntests_failed(sr);
    srunner_free(sr);
    return n_fail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
