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

    if ((iss >> f >> ret).fail()) throw NotFound();

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

/*! Specialized template for reading bool. Tries both 1|0 and true|false */
template <> inline bool from_string<bool> (const std::string& s,
                                           std::ios_base& (*f)(std::ios_base&))
    throw(NotFound)
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
            /* try On/Off */
            std::string tmp(s);

            gu::trim(tmp);

            if (tmp.length() >=2 && tmp.length() <= 3 &&
                (tmp[0] == 'o' || tmp[0] == 'O'))
            {
                if (tmp.length() == 2 && (tmp[1] == 'n' || tmp[1] == 'N'))
                {
                    return true;
                }
                else if (tmp.length() == 3 &&
                         (tmp[1] == 'f' || tmp[1] == 'F') &&
                         (tmp[2] == 'f' || tmp[2] == 'F'))
                {
                    return false;
                }
            }

            throw NotFound();
        }
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
