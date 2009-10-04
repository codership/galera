// Copyright (C) 2009 Codership Oy <info@codership.com>

#ifndef __GALERA_STATUS_H__
#define __GALERA_STATUS_H__

#include <galerautils.h>
#include <gcs.h>

#include "wsrep_api.h"

typedef enum galera_stage
{
    GALERA_STAGE_INIT,
    GALERA_STAGE_JOINING,
    GALERA_STAGE_SST_PREPARE,
    GALERA_STAGE_RST_SENT,
    GALERA_STAGE_SST_WAIT,
    GALERA_STAGE_JOINED,
    GALERA_STAGE_SYNCED,
    GALERA_STAGE_DONOR,
    GALERA_STAGE_RST_FAILED,
    GALERA_STAGE_SST_FAILED,
    GALERA_STAGE_MAX
}
galera_stage_t;

struct galera_status
{
    gu_uuid_t    state_uuid;         //!< current state UUID
    gcs_seqno_t  last_applied;       //!< last applied trx seqno
    gcs_seqno_t  replicated;         //!< how many actions replicated
    gcs_seqno_t  replicated_bytes;   //!< how many bytes replicated
    gcs_seqno_t  received;           //!< how many actions received from group
    gcs_seqno_t  received_bytes;     //!< how many bytes received from group
    gcs_seqno_t  local_commits;      //!< number of local commits
    gcs_seqno_t  local_cert_failures;//!< number of local certification failures
    gcs_seqno_t  local_bf_aborts;    //!< number of brute-forced transactions
    gcs_seqno_t  fc_waits;           //!< how many times had to wait for FC
    galera_stage_t stage;            //!< operational stage (see above)
};

/* Returns array of status variables */
extern struct wsrep_status_var*
galera_status_get (const struct galera_status* s);

extern void
galera_status_free (struct wsrep_status_var* s);

#endif // __GALERA_STATUS_H__
