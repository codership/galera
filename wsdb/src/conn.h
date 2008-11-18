// Copyright (C) 2007 Codership Oy <info@codership.com>

/*!
 * @file conn.h
 * @brief keyed variable length array utility API 
 * 
 */
#ifndef CONN_H_INCLUDED
#define CONN_H_INCLUDED

#include <gcs.h>
#include "wsdb_api.h"

/*!
 * @brief initializes connection management
 *
 * @param limit max number of connections
 * @param void_seqno un-initialized seqno value
 */
int conn_init(uint16_t limit, trx_seqno_t void_seqno);
/*!
 * @brief closes connection management and releases allocated resources
 */
void conn_close();

/*!
 * @brief removes connection 
 *
 * @param conn_id ID for the connection
 */
void conn_remove_conn(connid_t conn_id);

#ifdef IN_WSDB_API
/*!
 * @brief builds connection management queries for write set
 *
 * @param conn_id ID for the connection
 * @param key unique key for the variable (name)
 * @param data the SQL query to set the variable value
 */
int conn_store_set_variable(connid_t conn_id, char *key, char *data);

/*!
 * @brief stores set dabase query for connection
 *
 * @param conn_id ID for the connection
 * @param set_db the set/use default database command
 */
int conn_store_set_database(connid_t conn_id, char *set_db);
#endif

/*!
 * @brief builds connection management queries for write set
 *
 * @param ws the write set
 * @param conn_id ID for the connection
 */
int conn_build_connection_queries(
    struct wsdb_write_set *ws, connid_t conn_id
);

#ifdef IN_WSDB_API
/*!
 * @brief assigns seqno to connection
 *
 * @param conn_id ID for the connection
 * @param seqno   connection sequence (for ordering)
 */
int conn_set_seqno (connid_t conn_id, gcs_seqno_t seqno);

/*!
 * @brief queries connection seqno
 *
 * @param conn_id ID for the connection
 */
gcs_seqno_t conn_get_seqno (connid_t conn_id);
#endif // IN_WSDB_API

#endif // CONN_H_INCLUDED
