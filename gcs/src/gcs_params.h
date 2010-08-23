/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 *
 * $Id$
 */
#ifndef _gcs_params_h_
#define _gcs_params_h_

#include "galerautils.h"

struct gcs_params
{
    double fc_resume_factor;
    long   fc_base_limit;
    long   max_packet_size;
};

extern const char* GCS_PARAMS_FC_FACTOR;
extern const char* GCS_PARAMS_FC_LIMIT;
extern const char* GCS_PARAMS_MAX_PKT_SIZE;

/*! Initializes parameters from config or defaults (and updates config)
 *
 * @return 0 in case of success,
 *         -EINVAL if some values were set incorrectly in config */
extern long
gcs_params_init (struct gcs_params* params, gu_config_t* config);

#endif /* _gcs_params_h_ */

