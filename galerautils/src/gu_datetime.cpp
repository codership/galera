/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gu_datetime.hpp"
#include "gu_logger.hpp"
#include "gu_utils.hpp"
#include "gu_throw.hpp"

#include <regex>

namespace
{

    const char* const period_regex =
      "^(P)(([0-9]+)Y)?(([0-9]+)M)?(([0-9]+)D)?"
/*      1  23          45          67                             */
      "((T)?(([0-9]+)H)?(([0-9]+)M)?(([0-9]+)(\\.([0-9]+))?S)?)?";
/*     89    11          13          15      16                   */

  std::regex const regex(period_regex);

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

  struct regex_group
  {
      int index;
      long long multiplier;
  };

  const struct regex_group regex_groups[]
  {
      { GU_YEAR,  gu::datetime::Year  },
      { GU_MONTH, gu::datetime::Month },
      { GU_DAY,   gu::datetime::Day   },
      { GU_HOUR,  gu::datetime::Hour  },
      { GU_MIN,   gu::datetime::Min   },
      { GU_SEC,   gu::datetime::Sec   },
      { GU_SEC_D, gu::datetime::Sec   }
  };

  long long iso8601_duration_to_nsecs(const std::string& str)
  {
      long long nsecs = 0;
      std::smatch parts;
      if (std::regex_match(str, parts, regex))
      {
          for (auto g : regex_groups)
          {
              const std::string& part_string(parts[g.index].str());
              if (not part_string.empty())
              {
                  const double max(std::numeric_limits<long long>::max());
                  const double d(gu::from_string<double>(part_string) * g.multiplier);
                  if ((d > max) || (nsecs > (max - d)))
                  {
                      // addition would overflow
                      throw gu::NotFound();
                  }
                  nsecs += static_cast<long long>(d);
              }
          }
      }
      else
      {
          throw gu::NotFound();
      }
      return nsecs;
  }
}

long long gu::datetime::SimClock::counter_(0);
bool gu::datetime::SimClock::initialized_(false);

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


void gu::datetime::Period::parse(const std::string& str)
{
    try
    {
        nsecs = ::iso8601_duration_to_nsecs(str);
    }
    catch (NotFound& e)
    {
        double d(gu::from_string<double>(str));
        nsecs = static_cast<long long>(d * Sec);
    }
}
