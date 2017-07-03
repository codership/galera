/*
 * Copyright (C) 2009-2017 Codership Oy <info@codership.com>
 *
 * $Id$
 */

// This is a small class to facilitate lock-stepping in multithreaded unit tests

#ifndef _gu_lock_step_h_
#define _gu_lock_step_h_

#include <stdbool.h>

#include "gu_threads.h"

typedef struct gu_lock_step
{
    gu_mutex_t mtx;
    gu_cond_t  cond;
    long       wait;
    long       cont;
    bool       enabled;
}
gu_lock_step_t;

extern void
gu_lock_step_init (gu_lock_step_t* ls);

/* enable or disable lock-stepping */
extern void
gu_lock_step_enable (gu_lock_step_t* ls, bool enable);

extern void
gu_lock_step_wait (gu_lock_step_t* ls);

/* returns how many waiters there were,
 * waits for timeout_ms milliseconds if no waiters, if timeout_ms < 0 waits forever,
 * if 0 - no wait at all */
extern long
gu_lock_step_cont (gu_lock_step_t* ls, long timeout_ms);

extern void
gu_lock_step_destroy (gu_lock_step_t* ls);

#endif /* _gu_lock_step_h_ */
