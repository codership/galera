//
// Copyright (C) 2017 Codership Oy <info@codership.com>
//

//
// Define gu::array through either std::array or boost::array
//
// Because of the lack of alias template in C++ a workaround of defining
// the type inside the struct array is used.
//
// For example, defining gu::array type for type T is done like:
//
// typedef gu::array<T, S>::type A;
//
//

#ifndef GU_ARRAY_HPP
#define GU_ARRAY_HPP

#if defined(HAVE_STD_ARRAY)
#   include <array>
#   define GU_ARRAY_NAMESPACE std
#elif defined(HAVE_TR1_ARRAY)
#   include <tr1/array>
#   define GU_ARRAY_NAMESPACE std::tr1
#elif defined(HAVE_BOOST_ARRAY_HPP)
#   include <boost/array.hpp>
#   define GU_ARRAY_NAMESPACE boost
#else
    #error No supported array headers
#endif

namespace gu
{
    template <typename T, std::size_t S>
    struct array
    {
        typedef GU_ARRAY_NAMESPACE::array<T, S> type;
    };
}

#undef GU_ARRAY_NAMESPACE

#endif // GU_SHARED_PTR_HPP
