// Copyright (C) 2013 Codership Oy <info@codership.com>

/**
 * @file Atomic memory access functions. At the moment these are just
 *       redefinitions from gcc atomic builtins.
 *
 */

#ifndef GU_ATOMIC_H
#define GU_ATOMIC_H

#ifdef __GNUC__

#define gu_sync_fetch_and_add  __sync_fetch_and_add
#define gu_sync_fetch_and_sub  __sync_fetch_and_sub
#define gu_sync_fetch_and_or   __sync_fetch_and_or
#define gu_sync_fetch_and_and  __sync_fetch_and_and
#define gu_sync_fetch_and_xor  __sync_fetch_and_xor
#define gu_sync_fetch_and_nand __gu_sync_fetch_and_nand


#define gu_sync_add_and_fetch  __sync_add_and_fetch
#define gu_sync_sub_and_fetch  __sync_sub_and_fetch
#define gu_sync_or_and_fetch   __sync_or_and_fetch
#define gu_sync_and_and_fetch  __sync_and_and_fetch
#define gu_sync_xor_and_fetch  __sync_xor_and_fetch
#define gu_sync_nand_and_fetch __gu_sync_nand_and_fetch

#else /* __GNUC__ */
#error "Compiler not supported"
#endif

#endif /* !GU_ATOMIC_H */
