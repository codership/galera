/*
 * Copyright (C) 2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*! @file Common initializer for various galerautils parts. Currently it is
 *        logger and CRC32C implementation. */

#ifndef _GU_INIT_H_
#define _GU_INIT_H_

#if defined(__cplusplus)
extern "C" {
#endif

#include "gu_log.h"

extern void
gu_init (gu_log_cb_t log_cb);

#if defined(__cplusplus)
}
#endif

#endif /* _GU_INIT_H_ */
