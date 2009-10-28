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

#include <iostream>
#include <string>

namespace gu
{
    namespace datetime
    {
        /* Multiplier constants */
        const int64_t NSec = 1;
        const int64_t USec = 1000*NSec;
        const int64_t MSec = 1000*USec;
        const int64_t Sec  = 1000*MSec;
        const int64_t Min  = 60*Sec;
        const int64_t Hour = 60*Min;
        const int64_t Day  = 24*Hour;
        const int64_t Month = 30*Day;
        const int64_t Year = 12*Month;

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
                if (str != "")
                    parse(str);
            }
            
            bool operator==(const Period& cmp) const
            { return (nsecs == cmp.nsecs); }

            bool operator<(const Period& cmp) const
            { return (nsecs < cmp.nsecs); }
            
            Period operator+(const int64_t add) const
            { return (nsecs + add); }
            
            Period operator-(const int64_t dec) const
            { return (nsecs - dec); }
            
            Period operator*(const int64_t mul) const
            { return (nsecs*mul); }
            
            Period operator/(const int64_t div) const
            { return (nsecs/div); }

            int64_t get_nsecs() const { return nsecs; }
            
        private:
            
            Period(const int64_t nsecs_) :
                nsecs(nsecs_) { }
            friend class Date;
            friend std::istream& operator>>(std::istream&, Period&);
            /*!
             * @brief Parse period string.
             */
            void parse(const std::string&)
                throw (gu::Exception);
            int64_t nsecs;
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
             */
            static Date now();
            
            /*!
             * @brief Get maximum representable timestamp.
             */
            static Date max();
            
            /*!
             * @brief Get zero time
             */
            
            static Date zero();

            /*!
             * Return 64-bit timestamp representing system time in nanosecond
             * resolution.
             */
            int64_t get_utc() const { return utc; }
            
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

        protected:
            Date(const int64_t utc_) : utc(utc_) { }
            int64_t utc; /*!< System time in nanosecond precision */
        private:
            /*!
             * @brief Parse date from string.
             * @todo Not implemented yet
             */
            void parse(const std::string& str_)
                throw (gu::Exception);
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
