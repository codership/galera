// Copyright (C) 2009 Codership Oy <info@codership.com>

#ifndef __GALERA_STATE_H__
#define __GALERA_STATE_H__

#include "galerautils.h"

/* parameters needed to define state of galera replication
 */
typedef struct galera_saved_state {
    int64_t   last_applied_seqno; /* seqno of last applied */
    gu_uuid_t uuid;               /* group uuid */
} galera_state_t;

/*!
 * @brief saves galera state in a file in the given directory
 * @param dir    file path where state file resides
 * @param state  state to save
 * @returns 0 if successful, -1 otherwise
 */
int galera_store_state(const char *dir, galera_state_t *state);

/*!
 * @brief restores galera state from the given directory
 * @param dir    file path where state file resides
 * @param state  variable where state will be restored
 * @returns 0 if successful, -1 otherwise
 */
int galera_restore_state(const char *dir, galera_state_t *state);

#endif // __GALERA_STATE_H__
