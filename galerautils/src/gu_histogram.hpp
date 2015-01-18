/*
 * Copyright (C) 2014 Codership Oy <info@codership.com>
 */

#ifndef _gu_histogram_hpp_
#define _gu_histogram_hpp_

#include <map>
#include <ostream>

namespace gu
{
    class Histogram
    {
    public:
        Histogram(const std::string&);
        void insert(const double);
        void clear();
        friend std::ostream& operator<<(std::ostream&, const Histogram&);
        std::string to_string() const;
    private:
        std::map<double, long long> cnt_;
    };

    std::ostream& operator<<(std::ostream&, const Histogram&);
}

#endif // _gu_histogram_hpp_
