// Copyright (C) 2009 Codership Oy <info@codership.com>

#include <stdio.h>

#include "galera_status.h"

static gu_uuid_t state_uuid = { { 0, } };
static char      state_uuid_str[GU_UUID_STR_LEN + 1] = { 0, };

static const char* status_str[GALERA_STAGE_MAX] =
{
    "Initialized (0)",
    "Joining (1)",
    "Prepare for SST (2)",
    "SST request sent (3)",
    "Waiting for SST (4)",
    "Joined (5)",
    "Synced (6)",
    "Donor (+)"
    "SST request failed (-)",
    "SST failed (-)",
};

enum status_vars
{
    STATUS_STATE_UUID,
    STATUS_LAST_APPLIED,
    STATUS_REPLICATED,
    STATUS_REPLICATED_BYTES,
    STATUS_RECEIVED,
    STATUS_RECEIVED_BYTES,
    STATUS_LOCAL_COMMITS,
    STATUS_LOCAL_CERT_FAILURES,
    STATUS_LOCAL_BF_ABORTS,
    STATUS_FC_WAITS,
    STATUS_LOCAL_STATUS,
    STATUS_MAX
};

static struct wsrep_status_var wsrep_status[STATUS_MAX + 1] =
{
    {"local_state_uuid",    WSREP_STATUS_STRING, {._string = state_uuid_str}},
    {"last_committed",      WSREP_STATUS_INT64,  { -1 }                     },
    {"replicated",          WSREP_STATUS_INT64,  { 0 }                      },
    {"replicated_bytes",    WSREP_STATUS_INT64,  { 0 }                      },
    {"received",            WSREP_STATUS_INT64,  { 0 }                      },
    {"received_bytes",      WSREP_STATUS_INT64,  { 0 }                      },
    {"local_commits",       WSREP_STATUS_INT64,  { 0 }                      },
    {"local_cert_failures", WSREP_STATUS_INT64,  { 0 }                      },
    {"local_bf_aborts",     WSREP_STATUS_INT64,  { 0 }                      },
    {"flow_control_waits",  WSREP_STATUS_INT64,  { 0 }                      },
    {"local_status",        WSREP_STATUS_STRING, { 0 }                      },
    {NULL, 0, { 0 }}
};

/* Returns array of status variables */
struct wsrep_status_var*
galera_status_get (const struct galera_status* s)
{
    if (gu_uuid_compare (&state_uuid, &s->state_uuid)) {

        state_uuid = s->state_uuid;
        sprintf (state_uuid_str, GU_UUID_FORMAT, GU_UUID_ARGS(&state_uuid));  
    }

    wsrep_status[STATUS_LAST_APPLIED       ].value._int64 = s->last_applied;
    wsrep_status[STATUS_REPLICATED         ].value._int64 = s->replicated;
    wsrep_status[STATUS_REPLICATED_BYTES   ].value._int64 = s->replicated_bytes;
    wsrep_status[STATUS_RECEIVED           ].value._int64 = s->received;
    wsrep_status[STATUS_RECEIVED_BYTES     ].value._int64 = s->received_bytes;
    wsrep_status[STATUS_LOCAL_COMMITS      ].value._int64 = s->local_commits;
    wsrep_status[STATUS_LOCAL_CERT_FAILURES].value._int64 =
        s->local_cert_failures;
    wsrep_status[STATUS_LOCAL_BF_ABORTS    ].value._int64 = s->local_bf_aborts;
    wsrep_status[STATUS_FC_WAITS           ].value._int64 = s->fc_waits;
    wsrep_status[STATUS_LOCAL_STATUS       ].value._string = 
        status_str[s->stage];

    return wsrep_status;
}

extern void
galera_status_free (struct wsrep_status_var* s __attribute__((unused)))
{
    // do nothing, wsrep_galera_status is static
}
