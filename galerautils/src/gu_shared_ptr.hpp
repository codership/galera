//
// Copyright (C) 2015 Codership Oy <info@codership.com>
//

//
// Define gu::shared_ptr and gu::enable_shared_from_this.
//
// Because of the lack of alias template in C++ a workaround of defining
// the type inside the struct shared_ptr is used.
//
// For example, defining shared pointer type for type T is done like:
//
// typedef gu::shared_ptr<T>::type TPtr;
//
//

#ifndef GU_SHARED_PTR_HPP
#define GU_SHARED_PTR_HPP

#if defined(HAVE_TR1_MEMORY)

#include <tr1/memory>

namespace gu
{
    template <typename T>
    struct shared_ptr
    {
        typedef std::tr1::shared_ptr<T> type;
    };

    template <typename T>
    struct enable_shared_from_this
    {
        typedef std::tr1::enable_shared_from_this<T> type;
    };

}

#elif defined(HAVE_BOOST_SHARED_PTR_HPP)

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
namespace gu
{

    template <typename T>
    struct shared_ptr
    {
        typedef boost::shared_ptr<T> type;
    };

    template <typename T>
    struct enable_shared_from_this
    {
        typedef boost::enable_shared_from_this<T> type;
    };

}
#endif

#endif // GU_SHARED_PTR_HPP
