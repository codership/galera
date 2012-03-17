// Copyright (C) 2007 Codership Oy <info@codership.com>

/**
 * @file Miscellaneous macros
 *
 * $Id$
 */

#ifndef _gu_macros_h_
#define _gu_macros_h_

/* "Shamelessly stolen" (tm) goods from Linux kernel */
/*
 * min()/max() macros that also do
 * strict type-checking.. See the
 * "unnecessary" pointer comparison.
 */
#if 0 // typeof() is not in C99
#define GU_MAX(x,y) ({       \
        typeof(x) _x = (x);  \
        typeof(y) _y = (y);  \
        (void) (&_x == &_y); \
        _x > _y ? _x : _y; })

#define GU_MIN(x,y) ({       \
        typeof(x) _x = (x);  \
        typeof(y) _y = (y);  \
        (void) (&_x == &_y); \
        _x < _y ? _x : _y; })
#endif

#define gu_offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

#if __GNUC__ >= 3
#define gu_likely(x)   __builtin_expect((x), 1)
#define gu_unlikely(x) __builtin_expect((x), 0)
#else
#define gu_likely(x)   (x)
#define gu_unlikely(x) (x)
#endif

#endif /* _gu_macros_h_ */
