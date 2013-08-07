/* Copyright (C) 2013 Codership Oy <info@codership.com> */
/**
 * @file generic buffer declaration
 *
 * $Id$
 */

#ifndef _gu_buf_h_
#define _gu_buf_h_

#include "gu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct gu_buf
{
    const void* ptr;
    ssize_t     size;
};

#ifdef __cplusplus
}
#endif

#endif /* _gu_buf_h_ */
