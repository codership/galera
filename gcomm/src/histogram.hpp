/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_HISTOGRAM_HPP
#define GCOMM_HISTOGRAM_HPP

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
        std::map<double, long long> cnt_;
    };

    std::ostream& operator<<(std::ostream&, const Histogram&);
}


#endif // GCOMM_HISTOGRAM_HPP
