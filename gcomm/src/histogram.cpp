
#include "histogram.hpp"

#include "gcomm/exception.hpp"
#include "gcomm/logger.hpp"

#include <sstream>
#include <limits>

using std::istringstream;
using std::ostringstream;

BEGIN_GCOMM_NAMESPACE

Histogram::Histogram(const string& vals)
{

    vector<string> varr = strsplit(vals, ',');

    for (vector<string>::const_iterator i = varr.begin(); i != varr.end(); ++i)
    {
        double val;
        istringstream is(*i);
        is >> val;
        if (is.fail())
        {
            throw FatalException("parse error");
        }
        if (cnt.insert(make_pair(val, 0)).second == false)
        {
            throw FatalException("");
        }
    }
    if (cnt.insert(make_pair(std::numeric_limits<double>::max(), 0)).second == false)
    {
        throw FatalException("");
    }
}

void Histogram::insert(const double val)
{
    if (val < 1.e-32)
    {
        LOG_WARN("zero value, discarding");
        return;
    }
    map<const double, uint64_t>::iterator i = cnt.lower_bound(val);
    if (i == cnt.end())
    {
        throw FatalException("");
    }
    i->second++;
}

void Histogram::clear()
{
    for (map<const double, uint64_t>::iterator i = cnt.begin();
         i != cnt.end(); ++i)
    {
        i->second = 0.0;
    }
}

string Histogram::to_string() const
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
        os << i->first << "->" << i_next->first << ": " << 100.*double(i_next->second)/norm << "% ";
    }
    os << "total: " << norm;

    return os.str();
}


END_GCOMM_NAMESPACE
