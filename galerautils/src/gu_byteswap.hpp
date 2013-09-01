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

template <> GU_FORCE_INLINE uint8_t  gtoh (const uint8_t& val)
{
    return  val;
}

template <> GU_FORCE_INLINE uint16_t gtoh (const uint16_t& val)
{
    return  gtoh16(val);
}

template <> GU_FORCE_INLINE unsigned int gtoh (const unsigned int& val)
{
    return  gtoh32(val);
}

#if sizeof (long) == sizeof (int)
template <> GU_FORCE_INLINE unsigned long gtoh (const unsigned long& val)
{
    return  gtoh32(val);
}
#elif sizeof (long) == sizeof (long long)
template <> GU_FORCE_INLINE unsigned long gtoh (const unsigned long& val)
{
    return  gtoh64(val);
}
#endif

template <> GU_FORCE_INLINE unsigned long long gtoh (const unsigned long long& val)
{
    return  gtoh64(val);
}

template <typename T> T htog (const T& val) { return gtoh<T>(val); }

} /* namespace gu */

#endif /* _gu_byteswap_hpp_ */
