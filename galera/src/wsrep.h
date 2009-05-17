/* Copyright (C) 2009 Codership Oy <info@codership.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef WSREP_H
#define WSREP_H

#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  wsrep replication API
 */

#define WSREP_INTERFACE_VERSION "3:0:0"

/* Empty backend spec */
#define WSREP_NONE "none"

typedef uint64_t trx_id_t;      //!< applicaiton transaction ID
typedef uint64_t conn_id_t;     //!< application connection ID
typedef int64_t  wsrep_seqno_t; //!< sequence number of a writeset, etc.

/*! Undefined seqno */
static const wsrep_seqno_t WSREP_SEQNO_UNDEFINED = -1;

typedef enum wsrep_action {
    WSREP_UPDATE,
    WSREP_DELETE,
    WSREP_INSERT,
} wsrep_action_t;

/* status codes */
typedef enum wsrep_status {
    WSREP_OK        = 0, //!< success
    WSREP_WARNING,       //!< minor warning, error logged
    WSREP_TRX_MISSING,   //!< transaction is not known by wsrep
    WSREP_TRX_FAIL,      //!< transaction aborted, server can continue
    WSREP_BF_ABORT,      //!< trx was victim of brute force abort 
    WSREP_CONN_FAIL,     //!< error in client connection, must abort
    WSREP_NODE_FAIL,     //!< error in node state, wsrep must reinit
    WSREP_FATAL,         //!< fatal error, server must abort
} wsrep_status_t;

/* configuration parameters */
typedef enum wsrep_conf_param_id {
    WSREP_CONF_LOCAL_CACHE_SIZE,  //!< max size for local cache
    WSREP_CONF_WS_PERSISTENCY,    //!< WS persistency policy
    WSREP_CONF_DEBUG,             //!< enable debug level logging
} wsrep_conf_param_id_t;

typedef enum wsrep_conf_param_type {
    WSREP_TYPE_INT,     //!< integer type
    WSREP_TYPE_DOUBLE,  //!< float
    WSREP_TYPE_STRING,  //!< null terminated string
} wsrep_conf_param_type_t;

/* statistic parameters */
typedef enum wsrep_stat_param_id {
    WSREP_STAT_LAST_SEQNO, //!< last seqno applied
    WSREP_STAT_MAX
} wsrep_stat_param_id_t;

/*!
 * @brief callback to return configuration parameter value
 *        The function should be able to return values for all
 *        parameters defined in enum wsrep_conf_param_id
 *
 * @param configuration parameter identifier
 * @param configuration parameter type
 */
typedef void * (*wsrep_conf_param_cb_t)(wsrep_conf_param_id_t,
                                        wsrep_conf_param_type_t);

/*!
 * @brief callback to set statistic parameter value
 *        The function should be able to set values for all
 *        parameters defined in enum wsrep_stat_param_id
 *
 * @param statistic parameter identifier
 * @param parameter type (same as for configuration parameters)
 * @param value of the parameter
 */
typedef void (*wsrep_stat_param_cb_t)(wsrep_stat_param_id_t,
                                      wsrep_conf_param_type_t,
                                      void* value);

/*! 
 * Log severity levels, passed as first argument to log handler
 * @todo: how to synchronize it automatically with wsreputils?
 */
typedef enum wsrep_log_level
{
    WSREP_LOG_FATAL, //!< Unrecoverable error, application must quit.
    WSREP_LOG_ERROR, //!< Operation failed, must be repeated. 
    WSREP_LOG_WARN,  //!< Unexpected condition, but no operational failure.
    WSREP_LOG_INFO,  //!< Informational message.
    WSREP_LOG_DEBUG  //!< Debug message. Shows only of compiled with debug.
} wsrep_log_level_t;

/*!
 * @brief error log handler
 *
 *        All messages from wsrep library are directed to this
 *        handler, if present.
 *
 * @param log level
 * @param log message
 */
typedef void (*wsrep_log_cb_t)(wsrep_log_level_t, const char *);

/*!
 * UUID type - for all unique IDs 
 */
typedef struct wsrep_uuid {
    uint8_t uuid[16];
} wsrep_uuid_t;

/*! Undefined UUID */
static const wsrep_uuid_t WSREP_UUID_UNDEFINED = {{0,}};

/*!
 * Scan UUID from string
 * @return length of UUID string representation or negative error code 
 */
extern ssize_t
wsrep_uuid_scan (const char* str, size_t str_len, wsrep_uuid_t* uuid);

/*!
 * Print UUID to string
 * @return length of UUID string representation or negative error code 
 */
extern ssize_t
wsrep_uuid_print (const wsrep_uuid_t* uuid, char* str, size_t str_len);

/*!
 * Maximum logical member name length
 */
#define WSREP_MEMBER_NAME_LEN 32

/*!
 * Member status
 */
typedef enum wsrep_member_status {
    WSREP_MEMBER_EMPTY,  //!< incomplete state
    WSREP_MEMBER_JOINER, //!< incomplete state, requested state transfer
    WSREP_MEMBER_DONOR,  //!< complete state, donates state transfer
    WSREP_MEMBER_JOINED, //!< complete state
    WSREP_MEMBER_SYNCED  //!< complete state, synchronized with group
} wsrep_member_status_t;

/*!
 * Information about group member
 */
typedef struct wsrep_member_info {
    wsrep_uuid_t  id;
    char          name[WSREP_MEMBER_NAME_LEN];//!< human-readable name
    wsrep_seqno_t last_committed;             //!< last committed seqno
    wsrep_seqno_t slave_queue_len;            //!< length of the slave queue
    wsrep_member_status_t status;             //!< member status
    int           cpu_usage;                  //!< CPU utilization (%) 0..100
    int           load_avg;                   //!< Load average (%) can be > 100
    char*         incoming;                   //!< address for client requests 
} wsrep_member_info_t;

/*!
 * Group status
 */
typedef enum wsrep_view_status {
    WSREP_VIEW_PRIMARY,      //!< Primary group configuration (quorum present)
    WSREP_VIEW_NON_PRIMARY,  //!< Non-primary group configuration (quorum lost)
    WSREP_VIEW_DISCONNECTED, //!< Not connected to group, retrying.
    WSREP_VIEW_MAX
} wsrep_view_status_t;

/*!
 * View on the group
 */
typedef struct wsrep_view_info {
    wsrep_uuid_t        id;       //!< Group ID
    wsrep_seqno_t       conf;     //!< This configuration number
    wsrep_seqno_t       first;    //!< First seqno in this configuration
    wsrep_view_status_t status;   //!< Configuration status
    int                 my_idx;   //!< Index of this member in the configuration
    int                 memb_num; //!< Number of members in the configuration
    wsrep_member_info_t members[];//!< Array of member information
} wsrep_view_info_t;

/*!
 * @brief group view handler
 *
 *        This handler is called in total order corresponding to the group
 *        configuration change. It is to provide a vital information about
 *        new group view.
 *
 * @param view new view on the group
 */
typedef void (*wsrep_view_cb_t) (wsrep_view_info_t* view);

/*!
 * @brief transaction initialization function
 *
 *        This handler is called from wsrep library to initialize
 *        the context for following write set applying
 *
 * @param context pointer provided by the application
 * @param sequence number
 */
typedef int (*wsrep_ws_start_cb_t)(void *ctx, wsrep_seqno_t seqno);

/*!
 * @brief brute force apply function for SQL
 *
 *        This handler is called from wsrep library to execute
 *        the passed SQL statement in brute force.
 *
 * @param context pointer provided by the application
 * @param the SQL statement to execute
 */
typedef int (*wsrep_bf_execute_cb_t)(
    void *ctx, char *SQL, size_t sql_len, time_t timeval, uint32_t randseed
);

/*!
 * @brief brute force apply function for rows
 *
 *        This handler is called from wsrep library to apply
 *        the passed data row in brute force.
 *
 * @param context pointer provided by the application
 * @param the data row
 * @param length of data
 */
typedef int (*wsrep_bf_apply_row_cb_t)(void *ctx, void *data, size_t len);

/*!
 * @brief a callback to prepare the application to receive state transfer
 *
 *        This handler is called from wsrep library when it detects that
 *        this node needs state transfer (misses some actions).
 *        No group actions will be delivered for the duration of this call.
 *
 * @param msg state trasfer request message
 * @return size of the message or negative error code
 */
typedef ssize_t (*wsrep_state_prepare_cb_t) (void** msg);

/*!
 * @brief a callback to donate state transfer
 *
 *        This handler is called from wsrep library when it needs this node
 *        to deliver state to a new cluster member.
 *        No state changes will be committed for the duration of this call.
 *
 * @param msg state transfer request message
 * @param msg_len state transfer request message length
 * @return 0 for success of negative error code
 */
typedef int (*wsrep_state_donate_cb_t) (const void* msg, size_t msg_len);

/*!
 * Initialization parameters for wsrep, used as arguments for wsrep_init()
 */
typedef struct wsrep_init_args {
    /* Configuration options */
    const char* gcs_group;  //!< Symbolic group name
    const char* gcs_address;//!< URL-like gcs handle address (backend://address)
    const char* data_dir;   //!< directory where wsdb files are kept
    const char* name;       //!< Symbolic name of this process (e.g. hostname)
    const char* incoming;   //!< Incoming address for client connections

    /* Application state information */
    wsrep_uuid_t  state_uuid;  //! Application state group UUID
    wsrep_seqno_t state_seqno; //! Applicaiton state sequence number

    /* Application callbacks */
    wsrep_log_cb_t            logger_cb;
    wsrep_conf_param_cb_t     conf_param_cb;
    wsrep_stat_param_cb_t     stat_param_cb;
    wsrep_view_cb_t           view_handler_cb;
    wsrep_ws_start_cb_t       ws_start_cb;
    wsrep_bf_execute_cb_t     bf_execute_sql_cb;
    wsrep_bf_execute_cb_t     bf_execute_rbr_cb;
    wsrep_bf_apply_row_cb_t   bf_apply_row_cb;
    wsrep_state_prepare_cb_t  state_prepare_cb;
    wsrep_state_donate_cb_t   state_donate_cb;
} wsrep_init_args_t;

/*
 * wsrep interface for dynamically loadable libraries
 */
typedef struct wsrep_ wsrep_t;
struct wsrep_ {
/*!
 *  Interface version string
 */
    const char *version;

/*!
 * @brief initializes wsrep library
 *
 * @param args wsrep initialization parameters
 */
    wsrep_status_t (*init)(wsrep_t *, const wsrep_init_args_t *args);
    
/* replication enable/disable */
    wsrep_status_t (*enable) (wsrep_t *);    
    wsrep_status_t (*disable)(wsrep_t *);
    
/*!
 * @brief Push/pop DBUG control string to wsrep own DBUG implementation.
 *        (optional)
 *
 * @param ctrl DBUG library control string
 */
    void           (*dbug_push)(wsrep_t *, const char* ctrl);    
    void           (*dbug_pop) (wsrep_t *);
    
/*!
 * @brief start receiving replication events
 *
 * This function never returns
 *
 * @param ctx application context to be passed to callbacks
 */
    wsrep_status_t (*recv)(wsrep_t *, void *ctx);
    
/*!
 * @brief performs commit time operations
 *
 * wsrep_commit replicates the transaction and returns
 * success code, which caller must check. 
 * If commit was successful, transaction can commit, 
 * otherwise must rollback.
 *
 * @param trx_id transaction which is committing
 * @param conn_id
 * @param rbr_data binary data when rbr is set
 * @param data_len the size of the rbr data

 * @retval WSREP_OK         cluster commit succeeded
 * @retval WSREP_TRX_FAIL   must rollback transaction
 * @retval WSREP_CONN_FAIL  must close client connection
 * @retval WSREP_NODE_FAIL  must close all connections and reinit
 *
 *
 */
    wsrep_status_t (*commit)(wsrep_t *,
                             trx_id_t    trx_id,
                             conn_id_t   conn_id, 
                             const char* rbr_data,
                             size_t      data_len);
    
/*!
 * @brief wsrep_replay_trx
 *
 * If local trx has been aborted by brute force, and it has already
 * replicated before this abort, we must try if we can apply it as
 * slave trx. Note that slave nodes see only trx write sets and certification
 * test based on write set content can be different to DBMS lock conflicts.
 *
 * @param trx_id transaction which is committing
 * @param app_ctx
 *
 * @retval WSREP_OK         cluster commit succeeded
 * @retval WSREP_TRX_FAIL   must rollback transaction
 * @retval WSREP_BF_ABORT   brute force abort happened after trx was replicated
 *                          must rollback transaction and try to replay
 * @retval WSREP_CONN_FAIL  must close client connection
 * @retval WSREP_NODE_FAIL  must close all connections and reinit
 *
 */
    wsrep_status_t (*replay_trx)(wsrep_t *,
                                 trx_id_t  trx_id,
                                 void     *app_ctx);

/*!
 * @brief cancels a previously started commit
 *
 * wsrep_commit may stay waiting for total order semaphor
 * It is possible, that some other brute force transaction needs
 * to abort this commit operation.
 *
 * The kill routine checks that cancel is not tried against a transaction
 * who is front of the caller (in total order).
 *
 * @param bf_seqno seqno of brute force trx, running this cancel
 * @param victim_trx transaction to be killed, and which is committing
 *
 * @retval WSREP_OK         successful kill operaton
 * @retval WSREP_WARNING    could not kill the victim
 *
 */
    wsrep_status_t (*cancel_commit)(wsrep_t *,
                                    wsrep_seqno_t bf_seqno,
                                    trx_id_t      victim_trx);

/*!
 * @brief cancel another brute force transaction
 *
 * This routine is needed only if parallel applying is allowed
 * to happen.
 *
 * @param bf_seqno seqno of brute force trx, running this cancel
 * @param victim_seqno seqno of transaction to be killed
 *
 * @retval WSREP_OK         successful kill operaton
 * @retval WSREP_WARNING    could not kill the victim
 *
 */
    wsrep_status_t (*cancel_slave)(wsrep_t *,
                                   wsrep_seqno_t bf_seqno,
                                   wsrep_seqno_t victim_seqno);
    
/*!
 * @brief marks the transaction as committed
 *
 * wsrep_committed marks the transaction as committed.
 * It also frees any resourced reserved for the transaction.
 *
 * @param trx_id transaction which is committing
 * @retval WSREP_OK         cluster commit succeeded
 * @retval WSREP_TRX_FAIL   must rollback transaction
 */
    wsrep_status_t (*committed) (wsrep_t *, trx_id_t trx_id);

    wsrep_status_t (*rolledback)(wsrep_t *, trx_id_t trx_id);    

/*!
 * @brief appends a query in transaction's write set
 *
 * @param trx_id transaction ID
 * @param query  SQL statement string
 * @param timeval
 * @param randseed
 */
    wsrep_status_t (*append_query)(wsrep_t *,
                                   trx_id_t    trx_id,
                                   const char *query, 
                                   time_t      timeval,
                                   uint32_t    randseed);
    
/*!
 * @brief appends a row reference in transaction's write set
 *
 * @param trx_id      transaction ID
 * @param dbtable     unique name of the table "db.table"
 * @param dbtable_len length of table name (does not end with 0)
 * @param key         binary key data
 * @param key_len     length of the key data
 * @param action      action code according to enum wsrep_action
 */
    wsrep_status_t (*append_row_key)(wsrep_t *, 
                                     trx_id_t       trx_id, 
                                     const char    *dbtable,
                                     size_t         dbtable_len,
                                     const char    *key, 
                                     size_t         key_len, 
                                     wsrep_action_t action);
    

/*!
 * @brief appends a set variable command connection's write set
 *
 * @param conn_id     connection ID
 * @param query       the set variable query
 * @param query_len   length of query (does not end with 0)
 * @param key         name of the variable, must be unique
 * @param key_len     length of the key data
 */
    wsrep_status_t (*set_variable)(wsrep_t *,
                                   conn_id_t   conn_id, 
                                   const char *key,
                                   size_t      key_len,
                                   const char *query,
                                   size_t      query_len);

/*!
 * @brief appends a set database command connection's write set
 *
 * @param conn_id     connection ID
 * @param query       the 'set database' query
 * @param query_len   length of query (does not end with 0)
 */
    wsrep_status_t (*set_database)(wsrep_t *,
                                   conn_id_t   conn_id,
                                   const char *query,
                                   size_t      query_len);
    

/*!
 * @brief executes a query under total order control
 *
 * wsrep_to_execute replicates the query and returns
 * success code, which caller must check. 
 *
 * @param trx_id transaction which is committing
 * @param conn_id     connection ID
 * @param query       query to be executed
 *
 * @retval WSREP_OK         cluster commit succeeded
 * @retval WSREP_TRX_FAIL   must rollback transaction
 * @retval WSREP_CONN_FAIL  must close client connection
 * @retval WSREP_NODE_FAIL  must close all connections and reinit
 */
    wsrep_status_t (*to_execute_start)(wsrep_t *, 
                                       conn_id_t   conn_id, 
                                       const char *query, 
                                       size_t      query_len);

    wsrep_status_t (*to_execute_end)(wsrep_t *, conn_id_t conn_id);

/*!
 * @brief signals to wsrep provider that state has been sent to joiner
 *
 * @param uuid   sequence UUID (group UUID)
 * @param seqno  sequence number or negative error code of the operation
 */
    wsrep_status_t (*state_sent) (wsrep_t *,
                                  const wsrep_uuid_t* uuid,
                                  wsrep_seqno_t       seqno);

/*!
 * @brief signals to wsrep provider that new state has been received.
 *        May deadlock if called from state_prepare_cb.
 * @param uuid  sequence UUID (group UUID)
 * @param seqno sequence number or negative error code of the operation
 */
    wsrep_status_t (*state_received) (wsrep_t *,
                                      const wsrep_uuid_t* uuid,
                                      wsrep_seqno_t       seqno);

/*!
 * @brief wsrep shutdown, all memory objects are freed.
 */
    void (*tear_down)(wsrep_t *);

    void *dlh;
    void *opaque;
};
    
typedef int (*wsrep_loader_fun)(wsrep_t *);


/*!
 *
 * @brief loads wsrep library
 *
 * @param spec path to wsrep library 
 * @param hptr location to store wsrep handle
 *
 * @return zero on success, errno on failure
 */
int wsrep_load(const char *spec, wsrep_t **hptr, wsrep_log_cb_t log_cb);

/*!
 * @brief unload wsrep library and free resources
 * 
 * @param hptr wsrep handler pointer
 */
void wsrep_unload(wsrep_t *hptr);
    
#ifdef __cplusplus
}
#endif

#endif /* WSREP_H */
