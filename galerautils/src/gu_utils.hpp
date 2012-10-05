// Copyright (C) 2009-2010 Codership Oy <info@codership.com>

/**
 * @file General-purpose functions and templates
 *
 * $Id$
 */

#ifndef _gu_utils_hpp_
#define _gu_utils_hpp_

#include <string>
#include <sstream>
#include <iomanip>
#include <limits>

#include "gu_exception.hpp"
#include "gu_string.hpp"

namespace gu {

/*
 * String conversion functions for primitive types 
 */
/*! Generic to_string() template function */
template <typename T>
inline std::string to_string(const T& x,
                             std::ios_base& (*f)(std::ios_base&) = std::dec)
{
    std::ostringstream out;
    out << std::showbase << f << x;
    return out.str();
}


/*! Specialized template: make bool translate into 'true' or 'false' */
template <>
inline std::string to_string<bool>(const bool& x,
                                   std::ios_base& (*f)(std::ios_base&))
{
    std::ostringstream out;
    out << std::boolalpha << x;
    return out.str();
}

/*! Specialized template: make double to print with full precision */
template <>
inline std::string to_string<double>(const double& x,
                                     std::ios_base& (*f)(std::ios_base&))
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

    try
    {
        if ((iss >> f >> ret).fail()) throw NotFound();
    }
    catch (gu::Exception& e)
    {
        throw NotFound();
    }
    return ret;
}

/*! Specialized template for reading strings. This is to avoid throwing
 *  NotFound in case of empty string. */
template <> inline std::string
from_string<std::string>(const std::string& s,
                         std::ios_base& (*f)(std::ios_base&))
{
    return s;
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

extern bool _to_bool (const std::string& s) throw (NotFound);

/*! Specialized template for reading bool. Tries both 1|0 and true|false */
template <> inline bool from_string<bool> (const std::string& s,
                                           std::ios_base& (*f)(std::ios_base&))
    throw(NotFound)
{
    return _to_bool(s);
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


/*!
 * Object deletion operator. Convenient with STL containers containing
 * pointers. Example:
 *
 * @code
 * void cleanup()
 * {
 *     for_each(container.begin(), container.end(), DeleteObject());
 *     container.clear();
 * }
 *
 * @endcode
 */
class DeleteObject
{
public:
    template <class T> void operator()(T* t) { delete t; }
};


} // namespace gu

#endif /* _gu_utils_hpp_ */
