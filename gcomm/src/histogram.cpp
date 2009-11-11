/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "histogram.hpp"

#include "gcomm/exception.hpp"
#include "gcomm/logger.hpp"
#include "gcomm/types.hpp"

#include <gu_string.hpp>

#include <sstream>
#include <limits>
#include <vector>

using namespace std;

gcomm::Histogram::Histogram(const string& vals)
    :
    cnt()
{
    vector<string> varr = gu::strsplit(vals, ',');

    for (vector<string>::const_iterator i = varr.begin(); i != varr.end(); ++i)
    {
        double val;

        istringstream is(*i);
        is >> val;

        if (is.fail())
        {
            gu_throw_fatal << "Parse error";
        }

        if (cnt.insert(make_pair(val, 0)).second == false)
        {
            gu_throw_fatal << "Failed to insert value: " << val;
        }
    }

    if (cnt.insert(make_pair(numeric_limits<double>::max(), 0)).second == false)
    {
        gu_throw_fatal << "Failed to insert numeric_limits<double>::max()";
    }
}

void gcomm::Histogram::insert(const double val)
{
    if (val < 0.0)
    {
        log_warn << "Negative value (" << val << "), discarding";
        return;
    }
    
    map<const double, long long>::iterator i = cnt.lower_bound(val);
    
    if (i == cnt.end())
    {
        gu_throw_fatal;
    }
    
    i->second++;
}

void gcomm::Histogram::clear()
{
    for (map<const double, long long>::iterator i = cnt.begin();
         i != cnt.end(); ++i)
    {
        i->second = 0;
    }
}

ostream& gcomm::operator<<(ostream& os, const Histogram& hs)
{
    map<const double, long long>::const_iterator i, i_next;
    
    long long norm = 0;
    for (i = hs.cnt.begin(); i != hs.cnt.end(); ++i)
    {
        norm += i->second;
    }
    
    for (i = hs.cnt.begin(); i != hs.cnt.end(); i = i_next)
    {
        i_next = i;
        ++i_next;
        if (i_next == hs.cnt.end())
            break;
        os << i->first << " -> " << i_next->first << ": " << 100.*double(i_next->second)/double(norm) << " ";
    }
    os << "total: " << norm;
    
    return os;
}


