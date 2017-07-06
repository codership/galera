/*
 * Copyright (C) 2016-2017 Codership Oy <info@codership.com>
 */

#ifndef __GU_PROGRESS__
#define __GU_PROGRESS__

#include "gu_logger.hpp"
#include "gu_datetime.hpp"

#include <string>
#include <iomanip>
#include <cmath>

namespace gu
{
    template<typename T>
    class Progress
    {
        std::string const prefix_;
        std::string const units_;

        gu::datetime::Period const time_interval_;
        T                    const unit_interval_;

        T const total_;
        T       current_;
        T       last_size_;
        gu::datetime::Date last_time_;
        unsigned char const total_digits_;

        void report(gu::datetime::Date const now)
        {
            log_info << prefix_ << "..."
                     << std::fixed << std::setprecision(1) << std::setw(5)
                     << (double(current_)/total_ * 100) << "% ("
                     << std::setw(total_digits_) << current_ << '/' << total_
                     << units_ << ") complete.";

            last_time_ = now;
        }

        static std::string const DEFAULT_INTERVAL; // see definition below

    public:

        /*
         * Creates progress context and logs the beginning of the progress (0%)
         *
         * @param p prefix to be printed in each report
         *          (include trailing space)
         * @param u units to be printed next to numbers (empty string - no units)
         *          (include space between number and units)
         * @param t  total amount of work in units
         * @param ui minimal unit interval to report progress
         * @param ti minimal time interval to report progress
         */
        Progress(const std::string& p,
                 const std::string& u,
                 T const t,
                 T const ui,
                 const std::string& ti = DEFAULT_INTERVAL)
            :
            prefix_       (p),
            units_        (u),
            time_interval_(ti),
            unit_interval_(ui),
            total_        (t),
            current_      (0),
            last_size_    (current_),
            last_time_    (),
            total_digits_ (::ceil(::log10(total_ + 1)))
        {
            report(gu::datetime::Date::monotonic());
        }

        /* Increments progress by @increment.
         * If time limit is reached, logs the total progress */
        void update(T const increment)
        {
            current_ += increment;

            if (current_ - last_size_ >= unit_interval_
                /* don't log too close to the end */
                && total_ - current_ > unit_interval_)
            {
                gu::datetime::Date const now(gu::datetime::Date::monotonic());

                if (now - last_time_ >= time_interval_) report(now);

                last_size_ = current_;
                /* last_time_ is updated in report() */
            }
        }

        /* Logs the end of the progress (100%) */
        void finish()
        {
            current_ = total_;
            report(gu::datetime::Date::monotonic());
        }
    }; /* class Progress */

    template <typename T> std::string const
    Progress<T>::DEFAULT_INTERVAL = "PT10S"; /* 10 sec */

} /* namespace gu */

#endif /* __GU_PROGRESS__ */
