// Copyright (C) 2012 Codership Oy <info@codership.com>

/**
 * @file Functions to dump buffer contents in a readable form
 *
 * $Id$
 */

#ifndef _gu_print_buf_h_
#define _gu_print_buf_h_

#include "gu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! This function is to dump contents of the binary buffer into a readable form */
extern void gu_print_buf(const void* buf, ssize_t buf_size, char* str, ssize_t str_size);

#ifdef __cplusplus
}
#endif

#endif /* _gu_print_buf_h_ */

