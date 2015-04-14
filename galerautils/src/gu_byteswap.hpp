// Copyright (C) 2012 Codership Oy <info@codership.com>

/**
 * @file Endian conversion templates for serialization
 *
 * $Id$
 */

#ifndef _gu_byteswap_hpp_
#define _gu_byteswap_hpp_

#include "gu_byteswap.h"

#include <stdint.h>

namespace gu
{

/* General template: undefined */
template <typename T> T gtoh (const T& val)
{
    // to generate error on compilation rather then linking
    return val.this_template_does_not_support_this_type();
}

/* Specialized templates */

template <> GU_FORCE_INLINE int8_t  gtoh (const int8_t& val)
{
    return  val;
}

template <> GU_FORCE_INLINE uint8_t  gtoh (const uint8_t& val)
{
    return  val;
}

template <> GU_FORCE_INLINE int16_t gtoh (const int16_t& val)
{
    return  gtoh16(val);
}

template <> GU_FORCE_INLINE uint16_t gtoh (const uint16_t& val)
{
    return  gtoh16(val);
}

template <> GU_FORCE_INLINE int32_t gtoh (const int32_t& val)
{
    return  gtoh32(val);
}

template <> GU_FORCE_INLINE uint32_t gtoh (const uint32_t& val)
{
    return  gtoh32(val);
}

#if 0 //remove (why do we need this?)
#if __LONG_MAX__ == __INT_MAX__
template <> GU_FORCE_INLINE unsigned long gtoh (const unsigned long& val)
{
    return  gtoh32(val);
}
#elif __LONG_MAX__ == __LONG_LONG_MAX__
template <> GU_FORCE_INLINE unsigned long gtoh (const unsigned long& val)
{
    return  gtoh64(val);
}
#else
# error can not determine size of long
#endif
#endif

template <> GU_FORCE_INLINE int64_t gtoh (const int64_t& val)
{
    return  gtoh64(val);
}

template <> GU_FORCE_INLINE uint64_t gtoh (const uint64_t& val)
{
    return  gtoh64(val);
}

template <typename T> T htog (const T& val) { return gtoh<T>(val); }

} /* namespace gu */

#endif /* _gu_byteswap_hpp_ */
