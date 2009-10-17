
#ifndef HISTOGRAM_HPP
#define HISTOGRAM_HPP

#include "gcomm/common.hpp"
#include <map>
#include <ostream>


namespace gcomm
{
    class Histogram
    {
    public:
        Histogram(const std::string&);
        void insert(const double);
        void clear();
        friend std::ostream& operator<<(std::ostream&, const Histogram&);
    private:
        std::map<const double, uint64_t> cnt;
    };
    
    std::ostream& operator<<(std::ostream&, const Histogram&);
}


#endif // HISTOGRAM_HPP
