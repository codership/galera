// Copyright (C) 2009-2011 Codership Oy <info@codership.com>

/**
 * @file General-purpose functions and templates
 *
 * $Id$
 */

#include "gu_utils.hpp"
#include "gu_string_utils.hpp"

#include <algorithm>

namespace gu {

bool
_to_bool (const std::string& s)
{
    std::istringstream iss(s);
    bool               ret;

    if ((iss >> ret).fail())
    {
        /* if 1|0 didn't work, try true|false */
        iss.clear();
        iss.seekg(0);

        if ((iss >> std::boolalpha >> ret).fail())
        {
            /* try on/off and yes/no */
            std::string tmp(s);

            gu::trim(tmp);

            if (tmp.length() >=2 && tmp.length() <= 3)
            {
                std::transform (tmp.begin(), tmp.end(), tmp.begin(),
                                static_cast<int(*)(int)>(std::tolower));

                if (tmp == "yes" || tmp == "on") return true;
                if (tmp == "off" || tmp == "no") return false;
            }

            throw NotFound();
        }
    }

    return ret;
}

} // namespace gu
