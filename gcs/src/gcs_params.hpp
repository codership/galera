/*
 * Copyright (C) 2010-2014 Codership Oy <info@codership.com>
 *
 * $Id$
 */
#ifndef _gcs_params_h_
#define _gcs_params_h_

#include "galerautils.h"

struct gcs_params
{
    double  fc_resume_factor;
    double  recv_q_soft_limit;
    double  max_throttle;
    ssize_t recv_q_hard_limit;
    long    fc_base_limit;
    long    max_packet_size;
    long    fc_debug;
    bool    fc_master_slave;
    bool    sync_donor;
};

extern const char* const GCS_PARAMS_FC_FACTOR;
extern const char* const GCS_PARAMS_FC_LIMIT;
extern const char* const GCS_PARAMS_FC_MASTER_SLAVE;
extern const char* const GCS_PARAMS_FC_DEBUG;
extern const char* const GCS_PARAMS_SYNC_DONOR;
extern const char* const GCS_PARAMS_MAX_PKT_SIZE;
extern const char* const GCS_PARAMS_RECV_Q_HARD_LIMIT;
extern const char* const GCS_PARAMS_RECV_Q_SOFT_LIMIT;
extern const char* const GCS_PARAMS_MAX_THROTTLE;
#ifdef GCS_SM_DEBUG
extern const char* const GCS_PARAMS_SM_DUMP;
#endif /* GCS_SM_DEBUG */

/*! Register configuration parameters */
extern bool
gcs_params_register(gu_config_t* config);

/*! Initializes parameters from config
 *
 * @return 0 in case of success,
 *         -EINVAL if some values were set incorrectly in config */
extern long
gcs_params_init (struct gcs_params* params, gu_config_t* config);

#endif /* _gcs_params_h_ */

