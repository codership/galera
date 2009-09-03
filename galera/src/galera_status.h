// Copyright (C) 2009 Codership Oy <info@codership.com>

#ifndef __GALERA_STATUS_H__
#define __GALERA_STATUS_H__

#include <galerautils.h>
#include <gcs.h>

#include "wsrep_api.h"

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
};

/* Returns array of status variables */
extern struct wsrep_status_var*
galera_status_get (const struct galera_status* s);

extern void
galera_status_free (struct wsrep_status_var* s);

#endif // __GALERA_STATUS_H__
