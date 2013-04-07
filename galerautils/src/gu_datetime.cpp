/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gu_datetime.hpp"
#include "gu_logger.hpp"
#include "gu_utils.hpp"

extern "C"
{
#include "gu_time.h"
}

std::ostream& gu::datetime::operator<<(std::ostream& os, const Date& d)
{
    os << d.get_utc();
    return os;
}

std::ostream& gu::datetime::operator<<(std::ostream& os, const Period& p)
{
    os << "P";

    int64_t nsecs(p.get_nsecs());

    if (nsecs/Year  > 0) { os << (nsecs/Year)  << "Y"; nsecs %= Year;  }
    if (nsecs/Month > 0) { os << (nsecs/Month) << "M"; nsecs %= Month; }
    if (nsecs/Day   > 0) { os << (nsecs/Day)   << "D"; nsecs %= Day;   }
    if (nsecs       > 0) { os << "T";                                  }
    if (nsecs/Hour  > 0) { os << (nsecs/Hour)  << "H"; nsecs %= Hour;  }
    if (nsecs/Min   > 0) { os << (nsecs/Min)   << "M"; nsecs %= Min;   }

    if (double(nsecs)/Sec >= 1.e-9) { os << (double(nsecs)/Sec) << "S"; }

    return os;
}

void gu::datetime::Date::parse(const std::string& str)
{
    if (str == "")
    {
        return;
    }
    gu_throw_fatal << "not implemented";
}

const char* const gu::datetime::Period::period_regex =
         "^(P)(([0-9]+)Y)?(([0-9]+)M)?(([0-9]+)D)?"
/*         1  23          45          67                             */
         "((T)?(([0-9]+)H)?(([0-9]+)M)?(([0-9]+)(\\.([0-9]+))?S)?)?";
/*        89    11          13          15      16                   */

enum
{
    GU_P     = 1,
    GU_YEAR  = 3,
    GU_MONTH = 5,
    GU_DAY   = 7,
    GU_HOUR  = 10,
    GU_MIN   = 12,
    GU_SEC   = 15,
    GU_SEC_D = 16,
    GU_NUM_PARTS = 17
};

gu::RegEx const gu::datetime::Period::regex(period_regex);

void gu::datetime::Period::parse(const std::string& str)
{
    std::vector<RegEx::Match> parts = regex.match(str, GU_NUM_PARTS);

    if (parts[GU_P].is_set() == false)
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

    if (parts[GU_YEAR].is_set())
    {
        nsecs += from_string<long long>(parts[GU_YEAR].str())*Year;
    }

    if (parts[GU_MONTH].is_set())
    {
        nsecs += from_string<long long>(parts[GU_MONTH].str())*Month;
    }

    if (parts[GU_DAY].is_set())
    {
        nsecs += from_string<long long>(parts[GU_DAY].str())*Day;
    }

    if (parts[GU_HOUR].is_set())
    {
        nsecs += from_string<long long>(parts[GU_HOUR].str())*Hour;
    }

    if (parts[GU_MIN].is_set())
    {
        nsecs += from_string<long long>(parts[GU_MIN].str())*Min;
    }

    if (parts[GU_SEC].is_set())
    {
        long long s(from_string<long long>(parts[GU_SEC].str()));
        nsecs += s*Sec;
    }

    if (parts[GU_SEC_D].is_set())
    {
        double d(from_string<double>(parts[GU_SEC_D].str()));
        nsecs += static_cast<long long>(d*Sec);
    }
}


