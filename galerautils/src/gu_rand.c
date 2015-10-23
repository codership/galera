// Copyright (C) 2013-2015 Codership Oy <info@codership.com>

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

#include "gu_rand.h"
#include "gu_hash.h"

/*! Structure to hold entropy data.
 *  Should be at least 20 bytes on 32-bit systems and 28 bytes on 64-bit */
/*  Packed to avoid uninitialized data warnings when passed to hash */
struct gu_rse
{
    long long   time;
    const void* heap_ptr;
    const void* stack_ptr;
    long        pid;
}__attribute__((packed));

typedef struct gu_rse gu_rse_t;

long int
gu_rand_seed_long (long long time, const void* heap_ptr, pid_t pid)
{
    gu_rse_t rse = { time, heap_ptr, &time, pid };
    return gu_fast_hash64_medium (&rse, sizeof(rse));
}

#if GU_WORDSIZE == 32

unsigned int
gu_rand_seed_int  (long long time, const void* heap_ptr, pid_t pid)
{
    gu_rse_t rse = { time, heap_ptr, &time, pid };
    return gu_fast_hash32_short (&rse, sizeof(rse));
}

#endif /* GU_WORDSIZE == 32 */
