// Copyright (C) 2012-2017 Codership Oy <info@codership.com>

/**
 * @file Endian conversion templates for serialization
 *
 * $Id$
 */

#ifndef _gu_byteswap_hpp_
#define _gu_byteswap_hpp_

#include "gu_byteswap.h"
#include "gu_macros.hpp" // GU_COMPILE_ASSERT

#include <stdint.h>

namespace gu
{

/* General template utility class: undefined */
template <typename T, size_t>
class gtoh_template_helper
{
public:
    static T f(T val)
    {
        // to generate error on compilation stage rather then linking
        return val.this_template_use_is_not_supported();
    }
};

/* Utility argument size-specialized templates, don't use directly */

template <typename T>
class gtoh_template_helper<T, 1>
{
    GU_COMPILE_ASSERT(1 == sizeof(T), gtoh_wrong_argument_size1);
public:
    static GU_FORCE_INLINE T f(T val) { return  val; }
};

template <typename T>
class gtoh_template_helper<T, 2>
{
    GU_COMPILE_ASSERT(2 == sizeof(T), gtoh_wrong_argument_size2);
public:
    static GU_FORCE_INLINE T f(T val) { return  gtoh16(val); }
};

template <typename T>
class gtoh_template_helper<T, 4>
{
    GU_COMPILE_ASSERT(4 == sizeof(T), gtoh_wrong_argument_size4);
public:
    static GU_FORCE_INLINE T f(T val) { return  gtoh32(val); }
};

template <typename T>
class gtoh_template_helper<T, 8>
{
    GU_COMPILE_ASSERT(8 == sizeof(T), gtoh_wrong_argument_size8);
public:
    static GU_FORCE_INLINE T f(T val) { return  gtoh64(val); }
};

/* Proper generic byteswap templates for general use */
template <typename T> GU_FORCE_INLINE T gtoh (const T& val)
{
    return gtoh_template_helper<T, sizeof(T)>::f(val);
}

template <typename T> T htog (const T& val) { return gtoh<T>(val); }

} /* namespace gu */

#endif /* _gu_byteswap_hpp_ */
