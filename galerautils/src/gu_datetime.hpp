/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * @file Date/time manipulation classes providing nanosecond resolution.
 */

#ifndef __GU_DATETIME__
#define __GU_DATETIME__


#include "gu_exception.hpp"
#include "gu_regex.hpp"
#include "gu_time.h"

#include <iostream>
#include <string>
#include <limits>

namespace gu
{
    namespace datetime
    {
        /* Multiplier constants */
        const long long NSec  = 1;
        const long long USec  = 1000*NSec;
        const long long MSec  = 1000*USec;
        const long long Sec   = 1000*MSec;
        const long long Min   = 60*Sec;
        const long long Hour  = 60*Min;
        const long long Day   = 24*Hour;
        const long long Month = 30*Day;
        const long long Year  = 12*Month;

        /*!
         * @brief Class representing time periods instead of
         *        system clock time.
         */
        class Period
        {
        public:
            /*!
             * @brief Constructor
             *
             * Duration format is PnYnMnDTnHnMnS where Y is year, M is month,
             * D is day, T is the time designator separating date and time
             * parts, H denotes hours, M (after T) is minutes and S seconds.
             *
             * All other n:s are expected to be integers except the one
             * before S which can be decimal to represent fractions of second.
             *
             * @param str Time period represented in ISO8601 duration format.
             */
            Period(const std::string& str = "") :
                nsecs()
            {
                if (str != "") parse(str);
            }

            Period(const long long nsecs_) : nsecs(nsecs_) { }

            static Period min() { return 0; }
            static Period max() { return std::numeric_limits<long long>::max();}

            bool operator==(const Period& cmp) const
            { return (nsecs == cmp.nsecs); }

            bool operator<(const Period& cmp) const
            { return (nsecs < cmp.nsecs); }

            bool operator>=(const Period& cmp) const
            { return !(*this < cmp); }

            Period operator+(const long long add) const
            { return (nsecs + add); }

            Period operator-(const long long dec) const
            { return (nsecs - dec); }

            Period operator*(const long long mul) const
            { return (nsecs*mul); }

            Period operator/(const long long div) const
            { return (nsecs/div); }

            long long get_nsecs() const { return nsecs; }

        private:

            friend class Date;
            friend std::istream& operator>>(std::istream&, Period&);

            static const char* const period_regex; /*! regexp string */
            static RegEx       const regex;        /*! period string parser */

            /*!
             * @brief Parse period string.
             */
            void parse(const std::string&);

            long long nsecs;
        };


        /*!
         * @brief Date/time representation.
         *
         * @todo Parsing date from string is not implemented yet,
         *       only possible to get current system time or
         *       maximum time.
         */
        class Date
        {
        public:

            /*!
             * @brief Get system time.
             * @note This call should be deprecated in favor of calendar()
             *       and monotonic().
             */
            static inline Date now() { return gu_time_monotonic(); }

            /*!
             * @brief Get time from system-wide realtime clock.
             */
            static inline Date calendar() { return gu_time_calendar(); }

            /*!
             * @brief Get time from monotonic clock.
             */
            static inline Date monotonic() { return gu_time_monotonic(); }

            /*!
             * @brief Get maximum representable timestamp.
             */
            static inline Date max()
            { return std::numeric_limits<long long>::max(); }

            /*!
             * @brief Get zero time
             */
            static inline Date zero() { return 0; }

            /*!
             * Return 64-bit timestamp representing system time in nanosecond
             * resolution.
             */
            long long get_utc() const { return utc; }

            /* Standard comparision operators */
            bool operator==(const Date cmp) const
            { return (utc == cmp.utc); }

            bool operator<(const Date cmp) const
            { return (utc < cmp.utc); }

            /*!
             * @brief Add period to Date
             */
            Date operator+(const Period& add) const
            { return (utc + add.get_nsecs()); }

            /*!
             * @brief Decrement period from Date
             */
            Date operator-(const Period& dec) const
            { return (utc - dec.get_nsecs()); }


            Period operator-(const Date& dec) const
            { return (utc - dec.utc); }


            Date(const long long utc_ = 0) : utc(utc_) { }

            /*! convert to timespec - for internal use */
            void _timespec(timespec& ts) const
            {
                ts.tv_sec  = utc / 1000000000L;
                ts.tv_nsec = utc % 1000000000L;
            }

        private:

            long long utc; /*!< System time in nanosecond precision */

            /*!
             * @brief Parse date from string.
             * @todo Not implemented yet
             */
            void parse(const std::string& str_);
        };

        /*!
         * @brief Output operator for Date class.
         * @todo Not implemented yet
         */
        std::ostream& operator<<(std::ostream&, const Date&);

        /*!
         * @brief Output operator for Period type.
         */
        std::ostream& operator<<(std::ostream&, const Period&);

        inline std::string to_string(const Period& p)
        {
            std::ostringstream os;
            os << p;
            return os.str();
        }

        inline std::istream& operator>>(std::istream& is, Period& p)
        {
            std::string str;
            is >> str;
            p.parse(str);
            return is;
        }

    } // namespace datetime
} // namespace gu

#endif // __GU_DATETIME__
