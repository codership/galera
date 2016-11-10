/*
 * Copyright (C) 2013-2016 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gu_conf.h"
#include "gu_limits.h"
#include "gu_abort.h"
#include "gu_crc32c.h"

void
gu_init (gu_log_cb_t log_cb)
{
    gu_conf_set_log_callback (log_cb);

    /* this is needed in gu::MMap::sync() */
    size_t const page_size = GU_PAGE_SIZE;
    if (page_size & (page_size - 1))
    {
        gu_fatal("GU_PAGE_SIZE(%z) is not a power of 2", GU_PAGE_SIZE);
        gu_abort();
    }

    gu_crc32c_configure();
}
