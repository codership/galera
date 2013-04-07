// Copyright (C) 2012-2013 Codership Oy <info@codership.com>

/**
 * @file "Clean" abort function to avoid stack and core dumps
 *
 * $Id$
 */

#ifndef _gu_abort_h_
#define _gu_abort_h_

#ifdef __cplusplus
extern "C" {
#endif

#include "gu_macros.h"

/* This function is for clean aborts, when we can't gracefully exit otherwise */
extern void gu_abort() GU_NORETURN;

#ifdef __cplusplus
}
#endif

#endif /* _gu_abort_h_ */
