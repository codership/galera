// Copyright (C) 2013 Codership Oy <info@codership.com>

/**
 * @system/os/platform dependent functions/macros
 *
 * $Id$
 */

#ifndef _gu_system_h_
#define _gu_system_h_

#define _GNU_SOURCE // program_invocation_name, program_invocation_short_name
#include <errno.h>

#include <stdlib.h> // getexecname, getprogname

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* See: http://lists.gnu.org/archive/html/bug-gnulib/2010-12/txtrjMzutB7Em.txt
 * for implementation of GU_SYS_PROGRAM_NAME on other platforms */

#if defined(__sun__)
# define GU_SYS_PROGRAM_NAME getexecname ()
#elif defined(__APPLE__) || defined(__FreeBSD__)
# define GU_SYS_PROGRAM_NAME getprogname ()
#elif defined(__linux__)
# define GU_SYS_PROGRAM_NAME program_invocation_name
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _gu_system_h_ */
