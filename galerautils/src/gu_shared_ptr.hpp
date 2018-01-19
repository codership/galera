//
// Copyright (C) 2015-2017 Codership Oy <info@codership.com>
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

#if defined(HAVE_STD_SHARED_PTR)
#   include <memory>
#   define GU_SHARED_PTR_NAMESPACE std
#elif defined(HAVE_TR1_SHARED_PTR)
#   include <tr1/memory>
#   define GU_SHARED_PTR_NAMESPACE std::tr1
#elif defined(HAVE_BOOST_SHARED_PTR_HPP)
#   include <boost/shared_ptr.hpp>
#   include <boost/enable_shared_from_this.hpp>
#   include <boost/make_shared.hpp>
#   define GU_SHARED_PTR_NAMESPACE boost
#else
    #error No supported shared_ptr headers
#endif

namespace gu
{
    template <typename T>
    struct shared_ptr
    {
        typedef GU_SHARED_PTR_NAMESPACE::shared_ptr<T> type;
    };

    template <typename T>
    struct enable_shared_from_this
    {
        typedef GU_SHARED_PTR_NAMESPACE::enable_shared_from_this<T> type;
    };

#if __cplusplus >= 201103L
    /* variadic templates */
    template <class T, class... Args>
    typename shared_ptr<T>::type make_shared(Args&&... args)
    {
        return GU_SHARED_PTR_NAMESPACE::make_shared<T>(args...);
    }
#else
    /* add more templates if needed */
    template <class T>
    typename shared_ptr<T>::type make_shared()
    {
        return GU_SHARED_PTR_NAMESPACE::make_shared<T>();
    }
#endif
}

#undef GU_SHARED_PTR_NAMESPACE

#endif // GU_SHARED_PTR_HPP
