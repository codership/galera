// Copyright (C) 2009 Codership Oy <info@codership.com>

/**
 * @file General-purpose functions and templates
 *
 * $Id: gu_macros.hpp 980 2009-08-23 13:41:42Z alex $
 */

#ifndef _gu_utils_hpp_
#define _gu_utils_hpp_

#include <string>
#include <sstream>
#include <iomanip>

namespace gu
{

template<typename T> inline std::string to_string(const T& x)
{
    std::ostringstream out;
    out << x;
    return out.str();
}

/*! make bool translate into 'true' or 'false' */
template<> inline std::string to_string<bool>(const bool& x)
{
    std::ostringstream out;
    out << std::boolalpha << x;
    return out.str();
}

/*! make double to print with full precision */
template<> inline std::string to_string<double>(const double& x)
{
    const int sigdigits = std::numeric_limits<double>::digits10;
    // or perhaps std::numeric_limits<double>::max_digits10
    std::ostringstream out;
    out << std::setprecision(sigdigits) << x;
    return out.str();
}

} // namespace gu

#endif /* _gu_utils_hpp_ */
