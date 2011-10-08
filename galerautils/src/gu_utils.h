// Copyright (C) 2010 Codership Oy <info@codership.com>

/**
 * @file Miscellaneous utility functions
 *
 * $Id$
 */

#ifndef _gu_utils_h_
#define _gu_utils_h_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The string conversion functions below are slighly customized
 * versions of standard libc functions designed to understand 'on'/'off' and
 * K/M/G size modifiers and the like.
 *
 * They return pointer to the next character after conversion:
 * - if (ret == str) no conversion was made
 * - if (ret[0] == '\0') whole string was converted */

extern const char*
gu_str2ll   (const char* str, long long* ll);

extern const char*
gu_str2dbl  (const char* str, double*    dbl);

extern const char*
gu_str2bool (const char* str, bool*      b);

extern const char*
gu_str2ptr  (const char* str, void**     ptr);

#ifdef __cplusplus
}
#endif

#endif /* _gu_utils_h_ */
