// Copyright (C) 2013-2014 Codership Oy <info@codership.com>

/**
 * @file Atomic memory access functions. At the moment these follow
 *       __atomic_XXX convention from GCC.
 */

#ifndef GU_ATOMIC_H
#define GU_ATOMIC_H

#ifdef __cplusplus
extern "C" {
#endif

// So far in tests full memory sync shows the most consistent performance -
// and it's the safest. @todo: reassess this later.
#define GU_ATOMIC_SYNC_DEFAULT GU_ATOMIC_SYNC_FULL

#ifdef __GNUC__

#if defined(__ATOMIC_RELAXED) // use __atomic_XXX builtins

#define GU_ATOMIC_SYNC_NONE    __ATOMIC_RELAXED
#define GU_ATOMIC_SYNC_DEPEND  __ATOMIC_ACQ_REL
#define GU_ATOMIC_SYNC_FULL    __ATOMIC_SEQ_CST

#define gu_atomic_fetch_and_add(ptr, val) \
    __atomic_fetch_add(ptr, val, GU_ATOMIC_SYNC_DEFAULT)
#define gu_atomic_fetch_and_sub(ptr, val)  \
    __atomic_fetch_sub(ptr, val, GU_ATOMIC_SYNC_DEFAULT)
#define gu_atomic_fetch_and_or(ptr, val)   \
    __atomic_fetch_or(ptr, val,  GU_ATOMIC_SYNC_DEFAULT)
#define gu_atomic_fetch_and_and(ptr, val)  \
    __atomic_fetch_and(ptr, val, GU_ATOMIC_SYNC_DEFAULT)
#define gu_atomic_fetch_and_xor(ptr, val)  \
    __atomic_fetch_xor(ptr, val, GU_ATOMIC_SYNC_DEFAULT)
#define gu_atomic_fetch_and_nand(ptr, val) \
    __atomic_fetch_nand(ptr, val,GU_ATOMIC_SYNC_DEFAULT)

#define gu_atomic_add_and_fetch(ptr, val)  \
    __atomic_add_fetch(ptr, val, GU_ATOMIC_SYNC_DEFAULT)
#define gu_atomic_sub_and_fetch(ptr, val)  \
    __atomic_sub_fetch(ptr, val, GU_ATOMIC_SYNC_DEFAULT)
#define gu_atomic_or_and_fetch(ptr, val)   \
    __atomic_or_fetch(ptr, val,  GU_ATOMIC_SYNC_DEFAULT)
#define gu_atomic_and_and_fetch(ptr, val)  \
    __atomic_and_fetch(ptr, val, GU_ATOMIC_SYNC_DEFAULT)
#define gu_atomic_xor_and_fetch(ptr, val)  \
    __atomic_xor_fetch(ptr, val, GU_ATOMIC_SYNC_DEFAULT)
#define gu_atomic_nand_and_fetch(ptr, val) \
    __atomic_nand_fetch(ptr, val,GU_ATOMIC_SYNC_DEFAULT)

// stores contents of vptr into ptr
#define gu_atomic_set(ptr, vptr)                        \
    __atomic_store(ptr, vptr, GU_ATOMIC_SYNC_DEFAULT)

// loads contents of ptr to vptr
#define gu_atomic_get(ptr, vptr)                        \
    __atomic_load(ptr, vptr, GU_ATOMIC_SYNC_DEFAULT)

#elif defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8) // use __sync_XXX builtins

#define GU_ATOMIC_SYNC_NONE    0
#define GU_ATOMIC_SYNC_DEPEND  0
#define GU_ATOMIC_SYNC_FULL    0

#define gu_atomic_fetch_and_add  __sync_fetch_and_add
#define gu_atomic_fetch_and_sub  __sync_fetch_and_sub
#define gu_atomic_fetch_and_or   __sync_fetch_and_or
#define gu_atomic_fetch_and_and  __sync_fetch_and_and
#define gu_atomic_fetch_and_xor  __sync_fetch_and_xor
#define gu_atomic_fetch_and_nand __sync_fetch_and_nand

#define gu_atomic_add_and_fetch  __sync_add_and_fetch
#define gu_atomic_sub_and_fetch  __sync_sub_and_fetch
#define gu_atomic_or_and_fetch   __sync_or_and_fetch
#define gu_atomic_and_and_fetch  __sync_and_and_fetch
#define gu_atomic_xor_and_fetch  __sync_xor_and_fetch
#define gu_atomic_nand_and_fetch __sync_nand_and_fetch

#define gu_atomic_set(ptr, vptr)                                \
    while (!__sync_bool_compare_and_swap(ptr, *ptr, *vptr));

#define gu_atomic_get(ptr, vptr) *vptr = __sync_fetch_and_or(ptr, 0)

#else
#error "This GCC version does not support 8-byte atomics on this platform. Use GCC >= 4.7.x."
#endif /* __ATOMIC_RELAXED */

#else /* __GNUC__ */
#error "Compiler not supported"
#endif

#ifdef __cplusplus
}
#endif

#endif /* !GU_ATOMIC_H */
