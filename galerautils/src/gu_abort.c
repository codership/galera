// Copyright (C) 2011-2013 Codership Oy <info@codership.com>

/**
 * @file Clean abort function
 *
 * $Id$
 */


#include "gu_abort.h"

#include "gu_system.h"
#include "gu_log.h"
#include <sys/resource.h> /* for setrlimit() */
#include <signal.h>       /* for signal()    */
#include <stdlib.h>       /* for abort()     */

static void (* app_callback) (void) = NULL;

void
gu_abort (void)
{
    /* avoid coredump */
    struct rlimit core_limits = { 0, 0 };
    setrlimit (RLIMIT_CORE, &core_limits);

    /* restore default SIGABRT handler */
    signal (SIGABRT, SIG_DFL);

#if defined(GU_SYS_PROGRAM_NAME)
    gu_info ("%s: Terminated.", GU_SYS_PROGRAM_NAME);
#else
    gu_info ("Program terminated.");
#endif

    if (app_callback)
    {
        app_callback();
    }

    abort();
}

/* Register the application callback that be called before exiting: */

void
gu_abort_register_cb (void (* callback) (void))
{
    app_callback = callback;
}
