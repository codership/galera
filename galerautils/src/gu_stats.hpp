/*
 * Copyright (C) 2014 Codership Oy <info@codership.com>
 */

#ifndef _gu_stats_hpp_
#define _gu_stats_hpp_

#include <ostream>

namespace gu
{
    class Stats
    {
    public:
        Stats():n_(0),
                old_m_(), new_m_(),
                old_s_(), new_s_() {}
        void insert(const double);
        void clear() {
            n_ = 0;
        }
        unsigned int times() const {
            return n_;
        }
        double mean() const;
        double variance() const;
        double std_dev() const;
        friend std::ostream& operator<<(std::ostream&, const Stats&);
        std::string to_string() const;
    private:
        unsigned int n_;
        double old_m_;
        double new_m_;
        double old_s_;
        double new_s_;
    };

    std::ostream& operator<<(std::ostream&, const Stats&);
}

#endif // _gu_stats_hpp_
