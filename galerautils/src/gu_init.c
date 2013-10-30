/*
 * Copyright (C) 2013 Codership Oy <info@codership.com>
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

    if (gu_page_size() != GU_PAGE_SIZE)
    {
        gu_fatal("GU_PAGE_SIZE(%u) does not maptch real system page size(%zu)",
                 GU_PAGE_SIZE, gu_page_size());
        gu_abort();
    }

    gu_crc32c_configure();
}
