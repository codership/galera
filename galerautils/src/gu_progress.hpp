/*
 * Copyright (C) 2016-2021 Codership Oy <info@codership.com>
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
    public:
        class Callback
        {
        public:
            /**
             * @param total amount of work
             * @param done amount ot work
             */
            virtual void operator ()(T total, T done) = 0;

            virtual ~Callback() {}
        };

    private:
        Callback*   const callback_;
        std::string const prefix_;
        std::string const units_;

        gu::datetime::Period const log_interval_;
        T                    const unit_interval_;

        T total_;
        T current_;
        T last_check_;
        T last_logged_;

        gu::datetime::Date last_log_time_;
        gu::datetime::Date last_cb_time_;
        unsigned char const total_digits_;

        void log(gu::datetime::Date const now)
        {
            log_info << prefix_ << "..."
                     << std::fixed << std::setprecision(1) << std::setw(5)
                     << (double(current_)/total_ * 100) << "% ("
                     << std::setw(total_digits_) << current_ << '/' << total_
                     << units_ << ") complete.";

            last_log_time_ = now;
            last_logged_ = current_;
        }

        static std::string const DEFAULT_INTERVAL; // see definition below

        void cb(gu::datetime::Date const now)
        {
            (*callback_)(total_, current_);

            last_cb_time_ = now;
        }

    public:
        /*
         * Creates progress context and logs the beginning of the progress (0%)
         *
         * @param c a callback to call to report progress
         * @param p prefix to be printed in each log message
         *          (include trailing space)
         * @param u units to be printed next to numbers(empty string - no units)
         *          (include space between number and units)
         * @param t  total amount of work in units
         * @param ui minimal unit interval to log progress
         * @param ti minimal time interval to log progress
         */
        Progress(Callback* c,
                 const std::string& p,
                 const std::string& u,
                 T const t,
                 T const ui,
                 const std::string& ti = DEFAULT_INTERVAL)
            :
            callback_     (c),
            prefix_       (p),
            units_        (u),
            log_interval_ (ti),
            unit_interval_(ui),
            total_        (t),
            current_      (0),
            last_check_   (current_),
            last_logged_  (),
            last_log_time_(),
            last_cb_time_ (),
            total_digits_ (::ceil(::log10(total_ + 1)))
        {
            gu::datetime::Date const now(gu::datetime::Date::monotonic());
            if (callback_) cb(now);
            log(now);
        }

        /* On destruction log whatever progress was reached. */
        ~Progress()
        {
            gu::datetime::Date const now(gu::datetime::Date::monotonic());
            if (callback_) cb(now);
            if (last_logged_ != current_)log(now);
        }

        /* Increments progress by @increment.
         * If time limit is reached, logs the total progress */
        void update(T const increment)
        {
            /* while we may want to limit the rate of logging the progress,
             * it still makes sense (for monitoring) to call the callback
             * much more frequently */
            static gu::datetime::Period cb_interval("PT0.5S");

            current_ += increment;

            if (current_ - last_check_ >= unit_interval_)
            {
                gu::datetime::Date const now(gu::datetime::Date::monotonic());

                if (callback_ && now - last_cb_time_ >= cb_interval) cb(now);
                if (now - last_log_time_ >= log_interval_) log(now);

                last_check_ = current_;
                /* last_*_time_ is updated in log() and cb() */
            }
        }

        void update_total(T const increment)
        {
            total_ += increment;
        }

        /* mark progress as finished before object destruction */
        void finish()
        {
            current_ = total_;
        }

    private:
        Progress(const Progress&);
        Progress& operator=(Progress);

    }; /* class Progress */

    template <typename T> std::string const
    Progress<T>::DEFAULT_INTERVAL = "PT10S"; /* 10 sec */

} /* namespace gu */

#endif /* __GU_PROGRESS__ */
