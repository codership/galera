// Copyright (C) 2009 Codership Oy <info@codership.com>

/**
 * @file Routines for safe integer conversion
 *
 * $Id$
 */

#ifndef _gu_convert_hpp_
#define _gu_convert_hpp_

#include "gu_macros.h"
#include "gu_throw.hpp"
#include <limits>

namespace gu
{
    /*!
     * Converts from type FROM to type TO with range checking.
     * Generic template is for the case sizeof(FROM) > sizeof(TO).
     *
     * @param from  value to convert
     * @param to    destination (provides type TO for template instantiation)
     * @return      value cast to TO
     */
    template <typename FROM, typename TO> inline
    TO convert (const FROM& from, const TO& to)
    {
        if (gu_unlikely(from > std::numeric_limits<TO>::max() ||
                        from < std::numeric_limits<TO>::min()))
        {
            // @todo: figure out how to print type name without RTTI
            gu_throw_error (ERANGE) << from << " is unrepresentable with "
                                    << (std::numeric_limits<TO>::is_signed ?
                                        "signed" : "unsigned") << " "
                                    << sizeof(TO) << " bytes ("
                                    << "min " << std::numeric_limits<TO>::min()
                                    << " max " << std::numeric_limits<TO>::max()
                                    << ")";
        }

        return static_cast<TO>(from);
    }

    /* Specialized templates are for signed conversion */

    template <> inline
    long long convert (const unsigned long long& from, const long long& to)
    {
        if (gu_unlikely(from > static_cast<unsigned long long>
                        (std::numeric_limits<long long>::max())))
        {
            gu_throw_error (ERANGE) << from
                                    << " is unrepresentable with 'long long'";
        }

        return static_cast<long long>(from);
    }

    template <> inline
    unsigned long long convert (const long long& from,
                                const unsigned long long& to)
    {
        if (gu_unlikely(from < 0))
        {
            gu_throw_error (ERANGE) << from
                           << " is unrepresentable with 'unsigned long long'";
        }

        return static_cast<unsigned long long>(from);
    }

    template <> inline
    long convert (const unsigned long& from, const long& to)
    {
        if (gu_unlikely(from > static_cast<unsigned long>
                        (std::numeric_limits<long>::max())))
        {
            gu_throw_error (ERANGE) << from
                                    << " is unrepresentable with 'long'";
        }

        return static_cast<long long>(from);
    }

    template <> inline
    unsigned long convert (const long& from, const unsigned long& to)
    {
        if (gu_unlikely(from < 0))
        {
            gu_throw_error (ERANGE) << from
                           << " is unrepresentable with 'unsigned long'";
        }

        return static_cast<unsigned long>(from);
    }

    template <> inline
    int convert (const unsigned int& from, const int& to)
    {
        if (gu_unlikely(from > static_cast<unsigned int>
                        (std::numeric_limits<int>::max())))
        {
            gu_throw_error (ERANGE) << from
                                    << " is unrepresentable with 'long'";
        }

        return static_cast<int>(from);
    }

    template <> inline
    unsigned int convert (const int& from, const unsigned int& to)
    {
        if (gu_unlikely(from < 0))
        {
            gu_throw_error (ERANGE) << from
                           << " is unrepresentable with 'unsigned long'";
        }

        return static_cast<unsigned int>(from);
    }
}

#endif /* _gu_convert_hpp_ */
