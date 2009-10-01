
#ifndef HISTOGRAM_HPP
#define HISTOGRAM_HPP

#include "gcomm/common.hpp"
#include <map>
#include <string>


namespace gcomm
{
    class Histogram
    {
        std::map<const double, uint64_t> cnt;
    public:
        Histogram(const std::string&);
        void insert(const double);
        void clear();
        std::string to_string() const;
    };
}


#endif // HISTOGRAM_HPP
