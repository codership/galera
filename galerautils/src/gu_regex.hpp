// Copyright (C) 2009 Codership Oy <info@codership.com>

/**
 * @file Regular expressions parser based on POSIX regex functions in <regex.h>
 *
 * $Id$
 */

#ifndef _gu_regex_hpp_
#define _gu_regex_hpp_

#include <regex.h>
#include <string>
#include <vector>

#include "gu_throw.hpp"

namespace gu
{
    class RegEx
    {
        regex_t     regex;

        std::string strerror (int rc) const;

    public:

        /*!
         * @param expr regular expression string
         */
        RegEx (const std::string& expr) : regex()
        {
            int rc;

            if ((rc = regcomp(&regex, expr.c_str(), REG_EXTENDED)) != 0)
            {
                gu_throw_fatal << "regcomp(" << expr << "): " << strerror(rc);
            }
        }

        ~RegEx ()
        {
            regfree (&regex);
        }

        /*!
         * This class is to differentiate between an empty and unset strings.
         * @todo: find a proper name for it and move to gu_utils.hpp
         */
        class Match
        {
            std::string value;
            bool        set;

        public:

            Match()                     : value(),  set(false) {}
            Match(const std::string& s) : value(s), set(true)  {}

            // throws NotSet
            const std::string& str() const
            {
                if (set) return value;

                throw NotSet();
            }

            bool is_set() const { return set; }
        };

        /*!
         * @brief Matches given string
         *
         * @param str string to match with expression
         * @param num number of matches to return
         *
         * @return vector of matched substrings
         */
        std::vector<Match>
        match (const std::string& str, size_t num) const;
    };
}

#endif /* _gu_regex_hpp_ */
