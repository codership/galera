// Copyright (C) 2009-2017 Codership Oy <info@codership.com>

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
#include "gu_types.hpp"

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

/*! Generic from_string() template. Default base is decimal.
 * @throws NotFound */
template <typename T> inline T
from_string(const std::string& s,
            std::ios_base& (*f)(std::ios_base&) = std::dec)
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

/*! Specialized template for reading pointers. Default base is hex.
 * @throws NotFound */
template <> inline void*
from_string<void*>(const std::string& s,
                   std::ios_base& (*f)(std::ios_base&))
{
    std::istringstream iss(s);
    void*              ret;

    if ((iss >> std::hex >> ret).fail()) throw NotFound();

    return ret;
}

extern "C" const char* gu_str2bool (const char* str, bool* bl);

/*! Specialized template for reading bool. Tries both 1|0 and true|false
 * @throws NotFound */
template <> inline bool
from_string<bool> (const std::string& s,
                   std::ios_base& (*f)(std::ios_base&))
{
    bool ret;
    const char* const str(s.c_str());
    const char* const endptr(gu_str2bool(str, &ret));
    if (endptr == str || endptr == 0 || *endptr != '\0') throw NotFound();
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

/*! swap method for arrays, which does't seem to be built in all compilers */
template <typename T, size_t N>
inline void
swap_array(T (&a)[N], T (&b)[N])
{
    for (size_t n(0); n < N; ++n) std::swap(a[n], b[n]);
}

typedef std::ios_base& (*base_t) (std::ios_base& str);

template <base_t base = std::hex,
          typename T  = unsigned long long,
          bool prefix = false>
class PrintBase
{
public:
    explicit PrintBase(T t) : val_(t) {}
    void print(std::ostream& os) const
    {
        using namespace std;

        ios_base::fmtflags const old_flags(os.flags());
        char               const old_fill (os.fill());

        int width(sizeof(T) * 2); // default hex width
        if (base == oct) width = width * 1.333334 + 0.5;

        if (prefix) os << showbase;

        os << internal << base << setfill('0') << setw(width) << val_;

        os.flags(old_flags);
        os.fill(old_fill);
    }
private:
    T const val_;
};

template <base_t base, typename T>
std::ostream& operator << (std::ostream& os, const PrintBase<base, T>& b)
{
    b.print(os); return os;
}

/*! template to do arithmetics on void and byte pointers, compiler will
 *  catch anything else.
 * @return input type */
template <typename T, typename PtrOffsetType>
inline T*
ptr_offset(T* ptr, PtrOffsetType i) { return static_cast<byte_t*>(ptr) + i; }

template <typename T, typename PtrOffsetType>
inline const T*
ptr_offset(const T* ptr, PtrOffsetType i)
    { return static_cast<const byte_t*>(ptr)+i; }

} // namespace gu

#endif /* _gu_utils_hpp_ */
