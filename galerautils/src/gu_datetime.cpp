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
    os << "";
    return os;
}

ostream& gu::datetime::operator<<(ostream& os, const Period& p)
{
    os << "P";
    int64_t utc(p.get_utc());
    if (utc/Year > 0) {  os << (utc/Year) << "Y"; utc %= Year; }
    if (utc/Month > 0) { os << (utc/Month) << "M"; utc %= Month; }
    if (utc/Day > 0)  { os << (utc/Day) << "D"; utc %= Day; }
    if (utc > 0) { os << "T"; }
    if (utc/Hour > 0) { os << (utc/Hour) << "H"; utc %= Hour; }
    if (utc/Min > 0) { os << (utc/Min) << "M"; utc %= Min; }
    if (utc/Sec > 0) os << (double(utc)/Sec) << "S";
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
        utc += from_string<int64_t>(parts[YEAR].str())*Year;
    }
    if (parts[MONTH].is_set())
    {
        utc += from_string<int64_t>(parts[MONTH].str())*Month;
    }
    if (parts[DAY].is_set())
    {
        utc += from_string<int64_t>(parts[DAY].str())*Day;
    }
    if (parts[HOUR].is_set())
    {
        utc += from_string<int64_t>(parts[HOUR].str())*Hour;
    }
    if (parts[MIN].is_set())
    {
        utc += from_string<int64_t>(parts[MIN].str())*Min;
    }
    if (parts[SEC].is_set())
    {
        int64_t s(from_string<int64_t>(parts[SEC].str()));
        utc += s*Sec;
    }
    if (parts[SEC_D].is_set())
    {
        double d(from_string<double>(parts[SEC_D].str()));
        utc += static_cast<int64_t>(d*Sec);
    }
}


