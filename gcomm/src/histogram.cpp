
#include "histogram.hpp"

#include "gcomm/exception.hpp"
#include "gcomm/logger.hpp"
#include "gcomm/types.hpp"

#include <gu_string.hpp>

#include <sstream>
#include <limits>
#include <vector>

using std::istringstream;
using std::ostringstream;
using std::string;
using std::map;
using std::make_pair;
using std::vector;

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
            gcomm_throw_fatal << "Parse error";
        }

        if (cnt.insert(make_pair(val, 0)).second == false)
        {
            gcomm_throw_fatal << "Failed to insert value: " << val;
        }
    }

    if (cnt.insert(
            make_pair(std::numeric_limits<double>::max(), 0)).second == false)
    {
        gcomm_throw_fatal << "Failed to insert numeric_limits<double>::max()";
    }
}

void gcomm::Histogram::insert(const double val)
{
    if (val < 0.0)
    {
        log_warn << "Negative value (" << val << "), discarding";
        return;
    }

    map<const double, uint64_t>::iterator i = cnt.lower_bound(val);

    if (i == cnt.end())
    {
        gcomm_throw_fatal;
    }

    i->second++;
}

void gcomm::Histogram::clear()
{
    for (map<const double, uint64_t>::iterator i = cnt.begin();
         i != cnt.end(); ++i)
    {
        i->second = 0;
    }
}

string gcomm::Histogram::to_string() const
{
    ostringstream os;
    
    map<const double, uint64_t>::const_iterator i, i_next;

    uint64_t norm = 0;
    for (i = cnt.begin(); i != cnt.end(); ++i)
    {
        norm += i->second;
    }

    for (i = cnt.begin(); i != cnt.end(); i = i_next)
    {
        i_next = i;
        ++i_next;
        if (i_next == cnt.end())
            break;
        os << i->first << "->" << i_next->first << ": " << 100.*double(i_next->second)/double(norm) << "% ";
    }
    os << "total: " << norm;

    return os.str();
}


