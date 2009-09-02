// Copyright (C) 2009 Codership Oy <info@codership.com>

#ifndef __GALERA_STATUS_H__
#define __GALERA_STATUS_H__

#include <gcs.h>
#include "wsrep_api.h"

struct galera_status_vars
{
    wsrep_uuid_t  state_uuid;    //!< current state UUID
    wsrep_seqno_t last_applied;  //!< last applied trx seqno
    wsrep_seqno_t commits;       //!< number of local commits
    wsrep_seqno_t cert_failures; //!< number of local certification failures
    wsrep_seqno_t bf_aborts;     //!< number of brute-forced transactions
};

extern struct galera_status_vars galera_status;

/* Returns array of status variables */
extern struct wsrep_status_var*
galera_status_get ();

extern void
galera_status_free (struct wsrep_status_var* s);

#endif // __GALERA_STATUS_H__
