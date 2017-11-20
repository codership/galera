// Copyright (C) 2011-2013 Codership Oy <info@codership.com>

/**
 * @file Clean abort function
 *
 * $Id$
 */

#define _GNU_SOURCE
#include "gu_abort.h"

#include "gu_system.h"
#include "gu_log.h"
#include <sys/resource.h> /* for setrlimit() */
#include <signal.h>       /* for signal()    */
#include <stdlib.h>       /* for abort()     */

#ifdef __linux__
#include <sys/prctl.h>    /* for prctl() */
#endif /* __linux__ */

void
gu_abort (void)
{
    /* avoid coredump */
    struct rlimit core_limits = { 0, 0 };
    setrlimit (RLIMIT_CORE, &core_limits);

#ifdef __linux__
    /* Linux with its coredump piping option requires additional care.
     * See e.g. https://patchwork.kernel.org/patch/1091782/ */
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
#endif /* __linux__ */

    /* restore default SIGABRT handler */
    signal (SIGABRT, SIG_DFL);

#if defined(GU_SYS_PROGRAM_NAME)
    gu_info ("%s: Terminated.", GU_SYS_PROGRAM_NAME);
#else
    gu_info ("Program terminated.");
#endif

    abort();
}

