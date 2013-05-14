// Copyright (C) 2013 Codership Oy <info@codership.com>

/**
 * @file routines to generate "random" seeds for RNGs by collecting some easy
 *       entropy.
 *
 * gu_rand_seed_long() goes for srand48()
 *
 * gu_rand_seed_int()  goes for srand() and rand_r()
 *
 * $Id$
 */

#ifndef _gu_rand_h_
#define _gu_rand_h_

#include "gu_arch.h"

#include <sys/types.h> // for pid_t

extern long int
gu_rand_seed_long (long long time, const void* heap_ptr, pid_t pid);

#if GU_WORDSIZE == 32

extern unsigned int
gu_rand_seed_int  (long long time, const void* heap_ptr, pid_t pid);

#else

#define gu_rand_seed_int gu_rand_seed_long

#endif /* GU_WORDSIZE */

#endif /* _gu_rand_h_ */
