/*
 * Copyright (C) 2014 Codership Oy <info@codership.com>
 */

#include "gu_histogram.hpp"
#include "gu_logger.hpp"
#include "gu_throw.hpp"
#include "gu_string_utils.hpp" // strsplit()

#include <cmath>

#include <sstream>
#include <limits>
#include <vector>

gu::Histogram::Histogram(const std::string& vals)
    :
    cnt_()
{
    std::vector<std::string> varr = gu::strsplit(vals, ',');

    for (std::vector<std::string>::const_iterator
             i = varr.begin(); i != varr.end(); ++i)
    {
        double val;

        std::istringstream is(*i);
        is >> val;

        if (is.fail())
        {
            gu_throw_fatal << "Parse error";
        }

        if (cnt_.insert(std::make_pair(val, 0)).second == false)
        {
            gu_throw_fatal << "Failed to insert value: " << val;
        }
    }
}

void gu::Histogram::insert(const double val)
{
    if (val < 0.0)
    {
        log_warn << "Negative value (" << val << "), discarding";
        return;
    }

    // Returns element that has key greater to val,
    // the correct bin is one below that
    std::map<double, long long>::iterator i(cnt_.upper_bound(val));

    if (i == cnt_.end())
    {
        ++cnt_.rbegin()->second;
    }
    else if (i == cnt_.begin())
    {
        log_warn << "value " << val << " below histogram range, discarding";
    }
    else
    {
        --i;
        ++i->second;
    }
}

void gu::Histogram::clear()
{
    for (std::map<double, long long>::iterator i = cnt_.begin();
         i != cnt_.end(); ++i)
    {
        i->second = 0;
    }
}

std::ostream& gu::operator<<(std::ostream& os, const Histogram& hs)
{
    std::map<double, long long>::const_iterator i, i_next;

    long long norm = 0;
    for (i = hs.cnt_.begin(); i != hs.cnt_.end(); ++i)
    {
        norm += i->second;
    }

    for (i = hs.cnt_.begin(); i != hs.cnt_.end(); i = i_next)
    {
        i_next = i;
        ++i_next;
        os << i->first << ":" << std::fabs(double(i->second)/double(norm));
        if (i_next != hs.cnt_.end()) os << ",";
    }

    return os;
}


std::string gu::Histogram::to_string() const
{
    std::ostringstream os;
    os << *this;
    return os.str();
}
