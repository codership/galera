// Copyright (C) 2012-2013 Codership Oy <info@codership.com>

/**
 * @file Functions to dump binary buffer contents in a readable form
 *
 * $Id$
 */

#ifndef _gu_hexdump_h_
#define _gu_hexdump_h_

#include "gu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* This makes it 32*2 + 7 spaces = 71 character per line - just short of 80 */
#define GU_HEXDUMP_BYTES_PER_LINE 32

/*! Dumps contents of the binary buffer in a readable form to a 0-terminated
 *  string of length not exeeding str_size - 1
 * @param buf      input binary buffer
 * @param but_size size of the input buffer
 * @param str      target string buffer (will be always 0-terminated)
 * @param str_size string buffer size (including terminating 0)
 * @param alpha    dump alphanumeric characters as they are, padded with '.'
 *                 (e.g. D.u.m.p.)
 */
extern void
gu_hexdump(const void* buf, ssize_t buf_size,
           char*       str, ssize_t str_size,
           bool alpha);

#ifdef __cplusplus
}
#endif

#endif /* _gu_hexdump_h_ */

