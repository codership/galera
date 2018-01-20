// Copyright (C) 2009-2018 Codership Oy <info@codership.com>

#ifndef __GALERA_INFO_H__
#define __GALERA_INFO_H__

#include "gcs.hpp"
#include "wsrep_api.h"

/* create view info out of configuration message
 * if my_uuid is defined - use it to determine wsrep_view_info_t::my_idx,
 * otherwise set my_uuid according to my_idx */

extern wsrep_view_info_t*
galera_view_info_create (const gcs_act_cchange& conf,
                         wsrep_cap_t            capabilities,
                         int                    my_idx,
                         wsrep_uuid_t&          my_uuid);

/* make a copy of view info object */
extern wsrep_view_info_t*
galera_view_info_copy (const wsrep_view_info_t* vi);

#endif // __GALERA_INFO_H__
