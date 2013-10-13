// Copyright (C) 2009 Codership Oy <info@codership.com>

/**
 * @file Miscellaneous C++-related macros
 *
 * $Id$
 */

#ifndef _gu_macros_hpp_
#define _gu_macros_hpp_

/* To protect against "old-style" casts in libc macros
 * must be included after respective libc headers */
#if defined(SIG_IGN)
extern "C" { static void (* const GU_SIG_IGN)(int) = SIG_IGN; }
#endif

#if defined(MAP_FAILED)
extern "C" { static const void* const GU_MAP_FAILED = MAP_FAILED; }
#endif

namespace gu
{
    template <bool>struct CompileAssert {};
} /* namespace gu */

#define GU_COMPILE_ASSERT(expr, msg) \
    typedef gu::CompileAssert<(bool(expr))> msg[bool(expr) ? 1 : -1]

#endif /* _gu_macros_hpp_ */
