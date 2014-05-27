/*
 * Copyright (C) 2014 Codership Oy <info@codership.com>
 */

#include <cmath>
#include <sstream>

#include "gu_stats.hpp"

// http://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
// http://www.johndcook.com/standard_deviation.html

void gu::Stats::insert(const double val)
{
    n_++;
    if (n_ == 1) {
        old_m_ = new_m_ = val;
        old_s_ = new_s_ = 0.0;
    } else {
        new_m_ = old_m_ + (val - old_m_) / n_;
        new_s_ = old_s_ + (val - old_m_) * (val - new_m_);

        old_m_ = new_m_;
        old_s_ = new_s_;
    }
}

double gu::Stats::mean() const {
    return n_ > 0 ? new_m_ : 0.0;
}

double gu::Stats::variance() const {
    // n_ > 1 ? new_s_ / (n_ - 1) : 0.0;
    // is to compute unbiased sample variance
    // not population variance.
    return n_ > 0 ? new_s_ / n_ : 0.0;
}

double gu::Stats::std_dev() const {
    return sqrt(variance());
}

std::string gu::Stats::to_string() const
{
    std::ostringstream os;
    os << *this;
    return os.str();
}

std::ostream& gu::operator<<(std::ostream& os, const gu::Stats& stats)
{
    os << "avg=" << stats.mean()
       << ", sd=" << stats.std_dev()
       << ", n=" << stats.times();
    return os;
}
