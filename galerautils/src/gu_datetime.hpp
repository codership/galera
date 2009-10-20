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

#include <string>
#include "gu_exception.hpp"


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
            
            /* Standard comparision and arithmetic operations */
            bool operator==(const Date cmp) const { return (utc == cmp.utc); }
            bool operator<(const Date cmp) const { return (utc < cmp.utc); }
            Date operator+(const Date add) const { return (utc + add.utc); }
            Date operator-(const Date dec) const { return (utc - dec.utc); }
            
            virtual ~Date() { }
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
         * @brief Class representing time periods instead of 
         *        system clock time.
         */
        class Period : public Date
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
                Date(0)
            {
                if (str != "")
                    parse(str);
            }
        private:
            /*!
             * @brief Parse period string.
             */
            void parse(const std::string&)
                throw (gu::Exception);
        };
        
        /*!
         * @brief Output operator for Period type.
         */
        std::ostream& operator<<(std::ostream&, const Period&);
        
    } // namespace datetime
} // namespace gu

#endif // __GU_DATETIME__ 
