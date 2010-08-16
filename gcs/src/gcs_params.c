/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gcs_params.h"

#include <inttypes.h>
#include <errno.h>

const char* GCS_PARAMS_FC_FACTOR    = "gcs.fc_factor";
const char* GCS_PARAMS_FC_LIMIT     = "gcs.fc_limit";
const char* GCS_PARAMS_MAX_PKT_SIZE = "gcs.max_packet_size";

static const double GCS_PARAMS_DEFAULT_FC_FACTOR    = 0.5;
static const long   GCS_PARAMS_DEFAULT_FC_LIMIT     = 16;
static const long   GCS_PARAMS_DEFAULT_MAX_PKT_SIZE = 64500;

static long
params_init_fc_limit (gu_config_t* config, long* val)
{
    int64_t limit;

    long rc = gu_config_get_int64(config, GCS_PARAMS_FC_LIMIT, &limit);

    if (rc < 0) {
        gu_error ("Bad %s value", GCS_PARAMS_FC_LIMIT);
        return -EINVAL;
    }
    else if (rc > 0) {
        limit = GCS_PARAMS_DEFAULT_FC_LIMIT;
    }
    else if (limit < 0 || limit > LONG_MAX) {
        gu_error ("Bad %s value: %"PRIi64, GCS_PARAMS_FC_LIMIT, limit);
        return -EINVAL;
    }

    *val = limit;

    if (rc > 0) gu_config_set_int64 (config, GCS_PARAMS_FC_LIMIT, *val);

    return 0;
}

static long
params_init_fc_factor (gu_config_t* config, double* val)
{
    long rc = gu_config_get_double (config, GCS_PARAMS_FC_FACTOR, val);

    if (rc < 0) {
        gu_error ("Bad %s value", GCS_PARAMS_FC_FACTOR);
        return -EINVAL;
    }
    else if (rc > 0) {
        *val = GCS_PARAMS_DEFAULT_FC_FACTOR;
        gu_config_set_double (config, GCS_PARAMS_FC_FACTOR, *val);
    }
    else if (*val < 0.0 || *val > 1.0) {
            gu_error ("Bad %s value: %f", GCS_PARAMS_FC_FACTOR, *val);
            return -EINVAL;
    }

    return 0;
}

static long
params_init_max_pkt_size (gu_config_t* config, long* val)
{
    int64_t pkt_size;

    long rc = gu_config_get_int64(config, GCS_PARAMS_MAX_PKT_SIZE, &pkt_size);

    if (rc < 0) {
        gu_error ("Bad %s value", GCS_PARAMS_MAX_PKT_SIZE);
        return -EINVAL;
    }
    else if (rc > 0) {
        pkt_size = GCS_PARAMS_DEFAULT_MAX_PKT_SIZE;
    }
    else if (pkt_size < 0 || pkt_size > LONG_MAX) {
        gu_error ("Bad %s value: %"PRIi64, GCS_PARAMS_MAX_PKT_SIZE, pkt_size);
        return -EINVAL;
    }

    *val = pkt_size;

    if (rc > 0) gu_config_set_int64 (config, GCS_PARAMS_MAX_PKT_SIZE, *val);

    return 0;
}

long
gcs_params_init (struct gcs_params* params, gu_config_t* config)
{
    if (params_init_fc_limit     (config, &params->fc_base_limit)    ||
        params_init_fc_factor    (config, &params->fc_resume_factor) ||
        params_init_max_pkt_size (config, &params->max_packet_size))
        return -EINVAL;
    else
        return 0;
}
