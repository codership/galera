// Copyright (C) 2009 Codership Oy <info@codership.com>

/**
 * @file Regular expressions parser based on POSIX regex functions in <regex.h>
 *
 * $Id$
 */

#include "gu_utils.hpp"
#include "gu_regex.hpp"

namespace gu
{
    using std::string;
    using std::vector;

    string
    RegEx::strerror (int rc) const
    {
        char buf[128];

        regerror(rc, &regex, buf, sizeof(buf));

        return string (buf);
    }

    static inline RegEx::Match
    regmatch2Match (const string& str, const regmatch_t& rm)
    {
        if (rm.rm_so == -1) return RegEx::Match();

        return RegEx::Match (str.substr(rm.rm_so, rm.rm_eo - rm.rm_so));
    }

    vector<RegEx::Match>
    RegEx::match (const string& str, size_t num) const
    {
        vector<RegEx::Match> ret;
        int rc;

        VLA<regmatch_t> matches(num);

        if ((rc = regexec(&regex, str.c_str(), num, &matches, 0)))
        {
            gu_throw_error (EINVAL) << "regexec(" << str << "): "
                                    << strerror(rc);
        }

        for (size_t i = 0; i < num; ++i)
        {
            ret.push_back (regmatch2Match (str, matches[i]));
        }

        return ret;
    }
}
