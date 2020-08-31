//
// Copyright (C) 2020 Codership Oy <info@codership.com>
//

/** @file gu_inttypes.hpp
 *
 * A convenience header to include correct inttypes header from C++
 * compilation units.
 *
 * Pre C++11: Define __STDC_FORMAT_MACROS required by older compilers
 * and include <inttypes.h>.
 *
 * C++11 and above: Include standard library header <cinttypes> directly.
 */

#ifndef GU_INTTYPES_HPP
#define GU_INTTYPES_HPP

#if __cplusplus < 201103L
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#else
#include <cinttypes>
#endif // __cplusplus < 201103L

#endif // GU_INTTYPES_HPP
