/*
 * Copyright (C) 2016 Codership Oy <info@codership.com>
 */

#ifndef __GU_PROGRESS__
#define __GU_PROGRESS__

#include "gu_logger.hpp"
#include "gu_datetime.hpp"

#include <string>
#include <iomanip>

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

        void report(gu::datetime::Date const now)
        {
            log_info << prefix_ << '(' << total_ << units_ << ")... "
                     << std::fixed << std::setprecision(1)
                     << (double(current_)/total_ * 100)
                     << "% (" << current_ << units_ << ") complete.";

            last_time_ = now;
        }

    public:

        /*
         * @param p prefix to be printed in each report
         *          (include trailing space)
         * @param u units to be printed next to numbers (empty string - no units)
         *          (include space between number and units)
         * @param ti minimal time interval to report progress
         * @param ui minimal unit interval to report progress
         * @param t  total amount of work in units
         */
        Progress(const std::string& p,
                 const std::string& u,
                 const std::string& ti,
                 T const ui,
                 T const t)
            :
            prefix_       (p),
            units_        (u),
            time_interval_(ti),
            unit_interval_(ui),
            total_        (t),
            current_      (0),
            last_size_    (current_),
            last_time_    ()
        {
            report(gu::datetime::Date::monotonic());
        }

        void update(T const increment)
        {
            current_ += increment;

            if (current_ - last_size_ >= unit_interval_)
            {
                gu::datetime::Date const now(gu::datetime::Date::monotonic());

                if (now - last_time_ >= time_interval_) report(now);

                last_size_ = current_;
            }
        }

        void finish()
        {
            current_ = total_;
            report(gu::datetime::Date::monotonic());
        }
    }; /* class Progress */

} /* namespace gu */

#endif /* __GU_PROGRESS__ */
