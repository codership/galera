// Copyright (C) 2013 Codership Oy <info@codership.com>

/**
 * @file Location of some "standard" types definitions
 *
 * $Id$
 */

#ifndef _gu_types_h_
#define _gu_types_h_

#include <stdint.h>    /* intXX_t and friends */
#include <stdbool.h>   /* bool */
#include <unistd.h>    /* ssize_t */
#include <stddef.h>    /* ptrdiff_t */
#include <sys/types.h> /* off_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char gu_byte_t;

#ifdef __cplusplus
}
#endif

#endif /* _gu_types_h_ */
