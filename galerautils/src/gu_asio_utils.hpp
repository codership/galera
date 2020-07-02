//
// Copyright (C) 2020 Codership Oy <info@codership.com>
//

/**
 * Common implementation utilities for gu Asio
 */

#ifndef GU_ASIO_UTILS_HPP
#define GU_ASIO_UTILS_HPP

#ifndef GU_ASIO_IMPL
#error This header should not be included directly.
#endif // GU_ASIO_IMPL

#include "asio/ip/address.hpp"

// Workaround for clang 3.4 which pretends to be an old gcc compiler
// which in turn turns off some features in boost headers.
#if (defined(__clang__) && __clang_major__ == 3 && __clang_minor__ <= 4) || (__GNUC__ == 4 && __GNUC_MINOR__ == 4)

namespace gu
{
    template <class T> inline T* get_pointer(std::shared_ptr<T> const& r)
    {
        return r.get();
    }
}
#endif // defined(__clang__) && __clang_major__ == 3 && __clang_minor__ <= 4

#endif // GU_ASIO_UTILS_HPP
