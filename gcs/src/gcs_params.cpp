/*
 * Copyright (C) 2010-2017 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gcs_params.hpp"
#include "gcs_fc.hpp" // gcs_fc_hard_limit_fix

#include "gu_inttypes.hpp"

#include <cerrno>

const char* const GCS_PARAMS_FC_FACTOR         = "gcs.fc_factor";
const char* const GCS_PARAMS_FC_LIMIT          = "gcs.fc_limit";
const char* const GCS_PARAMS_FC_MASTER_SLAVE   = "gcs.fc_master_slave";
const char* const GCS_PARAMS_FC_DEBUG          = "gcs.fc_debug";
const char* const GCS_PARAMS_SYNC_DONOR        = "gcs.sync_donor";
const char* const GCS_PARAMS_MAX_PKT_SIZE      = "gcs.max_packet_size";
const char* const GCS_PARAMS_RECV_Q_HARD_LIMIT = "gcs.recv_q_hard_limit";
const char* const GCS_PARAMS_RECV_Q_SOFT_LIMIT = "gcs.recv_q_soft_limit";
const char* const GCS_PARAMS_MAX_THROTTLE      = "gcs.max_throttle";
#ifdef GCS_SM_DEBUG
const char* const GCS_PARAMS_SM_DUMP           = "gcs.sm_dump";
#endif /* GCS_SM_DEBUG */

static const char* const GCS_PARAMS_FC_FACTOR_DEFAULT         = "1.0";
static const char* const GCS_PARAMS_FC_LIMIT_DEFAULT          = "16";
static const char* const GCS_PARAMS_FC_MASTER_SLAVE_DEFAULT   = "no";
static const char* const GCS_PARAMS_FC_DEBUG_DEFAULT          = "0";
static const char* const GCS_PARAMS_SYNC_DONOR_DEFAULT        = "no";
static const char* const GCS_PARAMS_MAX_PKT_SIZE_DEFAULT      = "64500";
static ssize_t const GCS_PARAMS_RECV_Q_HARD_LIMIT_DEFAULT     = SSIZE_MAX;
static const char* const GCS_PARAMS_RECV_Q_SOFT_LIMIT_DEFAULT = "0.25";
static const char* const GCS_PARAMS_MAX_THROTTLE_DEFAULT      = "0.25";

bool
gcs_params_register(gu_config_t* conf)
{
    bool ret = 0;

    ret |= gu_config_add (conf, GCS_PARAMS_FC_FACTOR,
                          GCS_PARAMS_FC_FACTOR_DEFAULT);
    ret |= gu_config_add (conf, GCS_PARAMS_FC_LIMIT,
                          GCS_PARAMS_FC_LIMIT_DEFAULT);
    ret |= gu_config_add (conf, GCS_PARAMS_FC_MASTER_SLAVE,
                          GCS_PARAMS_FC_MASTER_SLAVE_DEFAULT);
    ret |= gu_config_add (conf, GCS_PARAMS_FC_DEBUG,
                          GCS_PARAMS_FC_DEBUG_DEFAULT);
    ret |= gu_config_add (conf, GCS_PARAMS_SYNC_DONOR,
                          GCS_PARAMS_SYNC_DONOR_DEFAULT);
    ret |= gu_config_add (conf, GCS_PARAMS_MAX_PKT_SIZE,
                          GCS_PARAMS_MAX_PKT_SIZE_DEFAULT);

    char tmp[32] = { 0, };
    snprintf (tmp, sizeof(tmp) - 1, "%lld",
              (long long)GCS_PARAMS_RECV_Q_HARD_LIMIT_DEFAULT);
    ret |= gu_config_add (conf, GCS_PARAMS_RECV_Q_HARD_LIMIT, tmp);

    ret |= gu_config_add (conf, GCS_PARAMS_RECV_Q_SOFT_LIMIT,
                          GCS_PARAMS_RECV_Q_SOFT_LIMIT_DEFAULT);
    ret |= gu_config_add (conf, GCS_PARAMS_MAX_THROTTLE,
                          GCS_PARAMS_MAX_THROTTLE_DEFAULT);
#ifdef GCS_SM_DEBUG
    ret |= gu_config_add (conf, GCS_PARAMS_SM_DUMP, "0");
#endif /* GCS_SM_DEBUG */
    return ret;
}

static long
params_init_bool (gu_config_t* conf, const char* const name, bool* const var)
{
    bool val;

    long rc = gu_config_get_bool(conf, name, &val);

    if (rc < 0) {
        /* Cannot parse parameter value */
        gu_error ("Bad %s value", name);
        return rc;
    }
    else if (rc > 0) {
        assert(0);
        val = false;
        rc = -EINVAL;
    }

    *var = val;

    return rc;
}

static long
params_init_long (gu_config_t* conf, const char* const name,
                  long min_val, long max_val, long* const var)
{
    int64_t val;

    long rc = gu_config_get_int64(conf, name, &val);

    if (rc < 0) {
        /* Cannot parse parameter value */
        gu_error ("Bad %s value", name);
        return rc;
    }
    else {
        /* Found parameter value */
        if (max_val == min_val) {
            max_val = LONG_MAX;
            min_val = LONG_MIN;
        }

        if (val < min_val || val > max_val) {
            gu_error ("%s value out of range [%ld, %ld]: %" PRIi64,
                      name, min_val, max_val, val);
            return -EINVAL;
        }
    }

    *var = val;

    return 0;
}

static long
params_init_int64 (gu_config_t* conf, const char* const name,
                   int64_t const min_val, int64_t const max_val,
                   int64_t* const var)
{
    int64_t val;

    long rc = gu_config_get_int64(conf, name, &val);

    if (rc < 0) {
        /* Cannot parse parameter value */
        gu_error ("Bad %s value", name);
        return rc;
    }
    else {
        /* Found parameter value */
        if ((min_val != max_val) && (val < min_val || val > max_val)) {
            gu_error ("%s value out of range [%" PRIi64 ", %" PRIi64 "]: %"
                      PRIi64, name, min_val, max_val, val);
            return -EINVAL;
        }
    }

    *var = val;

    return 0;
}

static long
params_init_double (gu_config_t* conf, const char* const name,
                    double const min_val, double const max_val,
                    double* const var)
{
    double val;

    long rc = gu_config_get_double(conf, name, &val);

    if (rc < 0) {
        /* Cannot parse parameter value */
        gu_error ("Bad %s value", name);
        return rc;
    }
    else {
        /* Found parameter value */
        if ((min_val != max_val) && (val < min_val || val > max_val)) {
            gu_error ("%s value out of range [%f, %f]: %f",
                      name, min_val, max_val, val);
            return -EINVAL;
        }
    }

    *var = val;

    return 0;
}

long
gcs_params_init (struct gcs_params* params, gu_config_t* config)
{
    long ret;

    if ((ret = params_init_long (config, GCS_PARAMS_FC_LIMIT, 0, LONG_MAX,
                                 &params->fc_base_limit))) return ret;

    if ((ret = params_init_long (config, GCS_PARAMS_FC_DEBUG, 0, LONG_MAX,
                                 &params->fc_debug))) return ret;

    if ((ret = params_init_long (config, GCS_PARAMS_MAX_PKT_SIZE, 0,LONG_MAX,
                                 &params->max_packet_size))) return ret;

    if ((ret = params_init_double (config, GCS_PARAMS_FC_FACTOR, 0.0, 1.0,
                                   &params->fc_resume_factor))) return ret;

    if ((ret = params_init_double (config, GCS_PARAMS_RECV_Q_SOFT_LIMIT,
                                   0.0, 1.0 - 1.e-9,
                                   &params->recv_q_soft_limit))) return ret;

    if ((ret = params_init_double (config, GCS_PARAMS_MAX_THROTTLE,
                                   0.0, 1.0 - 1.e-9,
                                   &params->max_throttle))) return ret;

    int64_t tmp;
    if ((ret = params_init_int64 (config, GCS_PARAMS_RECV_Q_HARD_LIMIT, 0, 0,
                                  &tmp))) return ret;
    params->recv_q_hard_limit = tmp * gcs_fc_hard_limit_fix;
    // allow for some meta overhead

    if ((ret = params_init_bool (config, GCS_PARAMS_FC_MASTER_SLAVE,
                                 &params->fc_master_slave))) return ret;

    if ((ret = params_init_bool (config, GCS_PARAMS_SYNC_DONOR,
                                 &params->sync_donor))) return ret;
    return 0;
}
