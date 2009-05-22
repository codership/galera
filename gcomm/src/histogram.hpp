
#ifndef HISTOGRAM_HPP
#define HISTOGRAM_HPP

#include "gcomm/common.hpp"
#include "gcomm/string.hpp"

#include <map>

using std::map;
using std::pair;
using std::make_pair;

BEGIN_GCOMM_NAMESPACE

class Histogram
{
    map<const double, uint64_t> cnt;
public:
    Histogram(const string&);
    void insert(const double);
    void clear();
    string to_string() const;
};

END_GCOMM_NAMESPACE

#endif // HISTOGRAM_HPP
