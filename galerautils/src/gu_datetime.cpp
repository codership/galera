/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gu_datetime.hpp"
#include "gu_regex.hpp"
#include "gu_logger.hpp"
#include "gu_utils.hpp"
extern "C"
{
#include "gu_time.h"
}

using namespace std;
using namespace gu;

ostream& gu::datetime::operator<<(ostream& os, const Date& d)
{
    os << d.get_utc();
    return os;
}

ostream& gu::datetime::operator<<(ostream& os, const Period& p)
{
    os << "P";
    int64_t nsecs(p.get_nsecs());
    if (nsecs/Year > 0) {  os << (nsecs/Year) << "Y"; nsecs %= Year; }
    if (nsecs/Month > 0) { os << (nsecs/Month) << "M"; nsecs %= Month; }
    if (nsecs/Day > 0)  { os << (nsecs/Day) << "D"; nsecs %= Day; }
    if (nsecs > 0) { os << "T"; }
    if (nsecs/Hour > 0) { os << (nsecs/Hour) << "H"; nsecs %= Hour; }
    if (nsecs/Min > 0) { os << (nsecs/Min) << "M"; nsecs %= Min; }
    if (double(nsecs)/Sec >= 1.e-9) os << (double(nsecs)/Sec) << "S";
    return os;
}


gu::datetime::Date gu::datetime::Date::now()
{
    return gu_time_calendar();
}

gu::datetime::Date gu::datetime::Date::max()
{
    return numeric_limits<int64_t>::max();
}

gu::datetime::Date gu::datetime::Date::zero()
{
    return 0;
}

void gu::datetime::Date::parse(const string& str)
    throw (gu::Exception)
{
    if (str == "")
    {
        return;
    }
    gu_throw_fatal << "not implemented";
}


void gu::datetime::Period::parse(const string& str)
    throw (gu::Exception)
{
    static const char* const period_regex = 
        "^(P)(([0-9]+)Y)?(([0-9]+)M)?(([0-9]+)D)?"
        //1  23          45          67 
        "((T)?(([0-9]+)H)?(([0-9]+)M)?(([0-9]+)(\\.([0-9]+))?S)?)?";
    //   89    11          13          15      16
    enum
    {
        P     = 1,
        YEAR  = 3,
        MONTH = 5,
        DAY   = 7,
        HOUR  = 10,
        MIN   = 12,
        SEC   = 15,
        SEC_D = 16,
        NUM_PARTS = 17
    };
    
    static const RegEx regex(period_regex);
    
    vector<RegEx::Match> parts = regex.match(str, NUM_PARTS);
    
    if (parts[P].is_set() == false)
    {
        if (str == "")
        {
            return;
        }
        else
        {
            gu_throw_error (EINVAL) << "Period " << str << " not valid";
        }
    }
    if (parts[YEAR].is_set())
    {
        nsecs += from_string<long long>(parts[YEAR].str())*Year;
    }
    if (parts[MONTH].is_set())
    {
        nsecs += from_string<long long>(parts[MONTH].str())*Month;
    }
    if (parts[DAY].is_set())
    {
        nsecs += from_string<long long>(parts[DAY].str())*Day;
    }
    if (parts[HOUR].is_set())
    {
        nsecs += from_string<long long>(parts[HOUR].str())*Hour;
    }
    if (parts[MIN].is_set())
    {
        nsecs += from_string<long long>(parts[MIN].str())*Min;
    }
    if (parts[SEC].is_set())
    {
        long long s(from_string<long long>(parts[SEC].str()));
        nsecs += s*Sec;
    }
    if (parts[SEC_D].is_set())
    {
        double d(from_string<double>(parts[SEC_D].str()));
        nsecs += static_cast<long long>(d*Sec);
    }
}


