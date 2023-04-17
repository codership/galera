/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gu_datetime.hpp"
#include "gu_logger.hpp"
#include "gu_utils.hpp"
#include "gu_throw.hpp"
#include "gu_regex.hpp"

#include <functional>

namespace
{
    /*
     * Parser for real numbers without loss of precision. Returns long long.
     */

    /* Regular expression for reals. Allowed formats:
     * 1
     * 1.1
     * .1
     */
    const char* real_regex_str = "^([0-9]*)?\\.?([0-9]*)?$";
    enum RealParts
    {
        integer = 1,
        decimal = 2
    };
    constexpr size_t num_real_parts = 3;
    gu::RegEx real_regex(real_regex_str);

    /* Helper to compute powers of 10 without floating point arithmetic.
     * The exponents must be integer in range [0, 9). */
    long long pow_10(int exponent)
    {
        if (exponent < 0 || exponent >= 9)
        {
            throw gu::NotFound();
        }
        long long result = 1;
        while (exponent != 0)
        {
            result *= 10;
            --exponent;
        }
        return result;
    }

    /* Real number representation with integer and decimal parts
       separated. Decimal part is represented in nanounits. */
    struct Real
    {
        long long integer{0}; // Integer part
        long long decimal{0}; // Decimal part in nanounits
    };

    /* Parse real number frrom string. */
    Real real_from_string(const std::string& str) try
    {
        Real ret;
        std::vector<gu::RegEx::Match> parts(
            real_regex.match(str, num_real_parts));
        if (parts.size() != 3)
        {
            throw gu::NotFound();
        }
        if (parts[RealParts::integer].is_set())
        {
            const auto& str = parts[RealParts::integer].str();
            if (str.size())
            {
                ret.integer = std::stoll(str);
            }
        }
        if (parts[RealParts::decimal].is_set())
        {
            const auto& str = parts[RealParts::decimal].str();
            if (str.size())
            {
                const auto n_decis = str.size();
                if (n_decis > 9)
                {
                    throw gu::NotFound();
                }
                const int exponent = 9 - n_decis;
                const long long multiplier = pow_10(exponent);
                ret.decimal = std::stoll(str) * multiplier;
            }
        }
        return ret;
    }
    catch (...)
    {
        throw gu::NotFound();
    }

    /* Parse seconds from string, return long long. */
    long long seconds_from_string(const std::string& str)
    {
        auto real = real_from_string(str);
        const auto max = std::numeric_limits<long long>::max();
        if (max/gu::datetime::Sec < real.integer)
        {
            /* Multiplication would overflow */
            throw gu::NotFound();
        }
        if (real.integer * gu::datetime::Sec > max - real.decimal)
        {
            /* Addition would overflow */
            throw gu::NotFound();
        }
        return real.integer * gu::datetime::Sec + real.decimal;
    }

    /* Parse seconds from string with multiplier. It is assumed that the
     * str argument contains integer. */
    template <long long Mult>
    long long seconds_from_string_mult(const std::string& str) try
    {
        const auto val = std::stoll(str);
        const auto max = std::numeric_limits<long long>::max();
        if (max/Mult < val)
        {
            /* Multiplication would overflow */
            throw gu::NotFound();
        }
        return (val * Mult);
    }
    catch(...)
    {
        throw gu::NotFound();
    }

    const char* const period_regex =
      "^(P)(([0-9]+)Y)?(([0-9]+)M)?(([0-9]+)D)?"
/*      1  23          45          67                             */
      "((T)?(([0-9]+)H)?(([0-9]+)M)?(([0-9]+(\\.?[0-9]*))?S)?)?$";
/*     89    11          13          15                           */

  gu::RegEx regex(period_regex);

  enum
  {
      GU_P     = 1,
      GU_YEAR  = 3,
      GU_MONTH = 5,
      GU_DAY   = 7,
      GU_HOUR  = 10,
      GU_MIN   = 12,
      GU_SEC   = 15,
      GU_NUM_PARTS = 17
  };

  struct regex_group
  {
      int index;
      std::function<long long(const std::string& str)> parse;
  };

  const struct regex_group regex_groups[]
  {
      { GU_YEAR, seconds_from_string_mult<gu::datetime::Year> },
      { GU_MONTH, seconds_from_string_mult<gu::datetime::Month> },
      { GU_DAY, seconds_from_string_mult<gu::datetime::Day> },
      { GU_HOUR, seconds_from_string_mult<gu::datetime::Hour> },
      { GU_MIN, seconds_from_string_mult<gu::datetime::Min> },
      { GU_SEC, seconds_from_string },
  };

  long long iso8601_duration_to_nsecs(const std::string& str)
  {
      long long nsecs = 0;
      std::vector<gu::RegEx::Match> parts;
      try
      {
          parts = regex.match(str, GU_NUM_PARTS);
      }
      catch (...) {
          throw gu::NotFound();
      }

      for (auto g : regex_groups)
      {
          if (parts[g.index].is_set())
          {
              const long long val(g.parse(parts[g.index].str()));
              const long long max(std::numeric_limits<long long>::max());
              if (nsecs > max - val)
              {
                  // addition would overflow
                  throw gu::NotFound();
              }
              nsecs += val;
          }
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
    catch (...)
    {
        nsecs = seconds_from_string(str);
    }
}
