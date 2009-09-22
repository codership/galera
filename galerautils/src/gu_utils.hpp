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

#include "gu_exception.hpp"

namespace gu {

/*
 * String conversion functions for primitive types 
 */
/*! Generic to_string() template function */
template <typename T> inline std::string to_string(const T& x)
{
    std::ostringstream out;
    out << x;
    return out.str();
}

/*! Specialized template: make bool translate into 'true' or 'false' */
template <> inline std::string to_string<bool>(const bool& x)
{
    std::ostringstream out;
    out << std::boolalpha << x;
    return out.str();
}

/*! Specialized template: make double to print with full precision */
template <> inline std::string to_string<double>(const double& x)
{
    const int sigdigits = std::numeric_limits<double>::digits10;
    // or perhaps std::numeric_limits<double>::max_digits10?
    std::ostringstream out;
    out << std::setprecision(sigdigits) << x;
    return out.str();
}

/*! Generic from_string() template. Default base is decimal */
template <typename T> inline T
from_string(const std::string& s,
            std::ios_base& (*f)(std::ios_base&) = std::dec) throw(NotFound)
{
    std::istringstream iss(s);
    T                  ret;

    if ((iss >> f >> ret).fail()) throw NotFound();

    return ret;
}

/*! Specialized template for reading pointers. Default base is hex */
template <> inline void* from_string<void*>(const std::string& s,
                                            std::ios_base& (*f)(std::ios_base&))
    throw(NotFound)
{
    std::istringstream iss(s);
    void*              ret;

    if ((iss >> std::hex >> ret).fail()) throw NotFound();

    return ret;
}

/*! Specialized template for reading bool. Tries both 1|0 and true|false */
template <> inline bool from_string<bool> (const std::string& s,
                                           std::ios_base& (*f)(std::ios_base&))
    throw(NotFound)
{
    std::istringstream iss(s);
    bool               ret;

    if ((iss >> ret).fail())
    {
        /* of 1|0 didn't work, try true|false */
        iss.clear();
        iss.seekg(0);
        if ((iss >> std::boolalpha >> ret).fail()) throw NotFound();
    }

    return ret;
}

/*! 
 * Substitute for the Variable Length Array on the stack from C99.
 * Provides automatic deallocation when out of scope:
 *
 * void foo(size_t n) { VLA<int> bar(n); bar[0] = 5; throw; }
 */
template <typename T> class VLA
{
    T* array;

    VLA (const VLA&);
    VLA& operator= (const VLA&);

public:

    VLA (size_t n) : array(new T[n]) {}

    ~VLA () { delete[] array; }

    T* operator& ()          { return array;    }

    T& operator[] (size_t i) { return array[i]; }
};

} // namespace gu

#endif /* _gu_utils_hpp_ */
