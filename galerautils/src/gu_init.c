/*
 * Copyright (C) 2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gu_conf.h"
#include "gu_crc32c.h"

void
gu_init (gu_log_cb_t log_cb)
{
    gu_conf_set_log_callback (log_cb);
    gu_crc32c_configure();
}
