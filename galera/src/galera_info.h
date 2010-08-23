// Copyright (C) 2009 Codership Oy <info@codership.com>

#ifndef __GALERA_INFO_H__
#define __GALERA_INFO_H__

#include <gcs.h>
#include "wsrep_api.h"

/* create view info out of configuration message */
extern wsrep_view_info_t*
galera_view_info_create (const gcs_act_conf_t* conf, bool st_required);

/* make a copy of view info object */
extern wsrep_view_info_t*
galera_view_info_copy (const wsrep_view_info_t* vi);

#endif // __GALERA_INFO_H__
