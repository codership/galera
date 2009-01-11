/* Copyright (C) 2007 Codership Oy <info@codership.com> */
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
    WSREP_CONF_MARK_COMMIT_EARLY, //!< update last seen trx asap
    WSREP_CONF_DEBUG,             //!< enable debug level logging
} wsrep_conf_param_id_t;

typedef enum wsrep_conf_param_type {
    WSREP_TYPE_INT,     //!< integer type
    WSREP_TYPE_DOUBLE,  //!< float
    WSREP_TYPE_STRING,  //!< null terminated string
} wsrep_conf_param_type_t;



typedef enum wsrep_action {
    WSREP_UPDATE,
    WSREP_DELETE,
    WSREP_INSERT,
} wsrep_action_t;


typedef uint64_t ws_id_t;
typedef uint64_t trx_id_t;
typedef uint64_t conn_id_t;
typedef int64_t bf_seqno_t;

/*!
 * @brief callback to return configuration parameter value
 *        The function should be able to return values for all
 *        parameters defined in enum wsrep_conf_param_id
 *
 * @param configuration parameter identifier
 */
typedef void * (*wsrep_conf_param_fun)(
    enum wsrep_conf_param_id, enum wsrep_conf_param_type
);

/*! 
 * Log severity levels, passed as first argument to log handler
 * @todo: how to synchronize it automatically with wsreputils?
 */
typedef enum wsrep_severity
{
    WSREP_LOG_FATAL, //! Unrecoverable error, application must quit.
    WSREP_LOG_ERROR, //! Operation failed, must be repeated. 
    WSREP_LOG_WARN,  //! Unexpected condition, but no operational failure.
    WSREP_LOG_INFO,  //! Informational message.
    WSREP_LOG_DEBUG  //! Debug message. Shows only of compiled with debug.
} wsrep_severity_t;

/*!
 * @brief error log handler
 *
 *        All messages from wsrep library are directed to this
 *        handler, if present.
 *
 * @param severity code
 * @param error message
 */
typedef void (*wsrep_log_cb_t)(int, const char *);

/*!
 * @brief transaction initialization function
 *
 *        This handler is called from wsrep library to initialize
 *        the context for following write set applying
 *
 * @param context pointer provided by the application
 * @param sequence number
 */
typedef int (*wsrep_ws_start_fun)(void *ctx, ws_id_t seqno);

/*!
 * @brief brute force apply function
 *
 *        This handler is called from wsrep library to execute
 *        the passed SQL statement in brute force.
 *
 * @param context pointer provided by the application
 * @param the SQL statement to execute
 */
typedef int (*wsrep_bf_execute_fun)(
    void *ctx, char *SQL, size_t sql_len, time_t timeval, uint32_t randseed
);

/*!
 * @brief brute force apply function
 *
 *        This handler is called from wsrep library to apply
 *        the passed data row in brute force.
 *
 * @param context pointer provided by the application
 * @param the data row
 * @param length of data
 */
typedef int (*wsrep_bf_apply_row_fun)(void *ctx, void *data, size_t len);

/*!
 * @brief sets the configuration parameter callback
 *
 * @param configurator   handler for returning configuration parameter values
 *
 */
enum wsrep_status wsrep_set_conf_param_cb(
    wsrep_conf_param_fun configurator
);

/*!
 * @brief wsrep shutdown, all memory objects are freed.
 *
 */
enum wsrep_status wsrep_tear_down(void);

/*!
 * @brief sets the logger callback for wsrep library
 * If logger is NULL, logger callback is not active
 *
 * @param error_fun   handler for error logging from wsrep library
 *
 */
enum wsrep_status wsrep_set_logger(wsrep_log_cb_t logger);

/*!
 * @brief initializes wsrep library
 *
 * @param gcs_group   symbolic group name (serves as unique group ID)
 * @param gcs_address URL-like gcs connection address (backend://address)
 *                    Currently supported backends: "dummy", "spread", "gcomm".
 * @param data_dir    directory where wsdb files are kept
 * @param error_fun   handler for error logging from wsrepw library
 *
 */
enum wsrep_status wsrep_init (const char           *gcs_group,
				const char           *gcs_address,
				const char           *data_dir,
				wsrep_log_cb_t       logger);
/*!
 * @brief Push/pop DBUG control string to wsrep own DBUG implementation.
 *        (optional)
 *
 * @param control DBUG library control string
 */
void wsrep_dbug_push (const char* control);
void wsrep_dbug_pop  (void);

/*!
 * @brief assigns handler for brute force applying of SQL statements
 *
 */
enum wsrep_status wsrep_set_execute_handler(wsrep_bf_execute_fun);

/*!
 * @brief assigns handler for brute force applying of a custom
 *        objects carring rows info such as mysql row-based replication events
 *
 */
enum wsrep_status wsrep_set_execute_handler_rbr(wsrep_bf_execute_fun);

/*!
 * @brief assigns handler for starting of write set applying
 *
 */
enum wsrep_status wsrep_set_ws_start_handler(wsrep_ws_start_fun);

/* replication enable/disable */
enum wsrep_status wsrep_enable(void);
enum wsrep_status wsrep_disable(void);


/*!
 * @brief start replication receiving
 *
 * This function never returns
 *
 */
enum wsrep_status wsrep_recv(void *ctx);

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
enum wsrep_status wsrep_commit(trx_id_t    trx_id,
                                 conn_id_t   conn_id,
                                 const char *rbr_data,
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
 * @param conn_id
 * @param rbr_data binary data when rbr is set
 * @param data_len the size of the rbr data

 * @retval WSREP_OK         cluster commit succeeded
 * @retval WSREP_TRX_FAIL   must rollback transaction
 * @retval WSREP_BF_ABORT   brute force abort happened after trx was replicated
 *                           must rollback transaction and try to replay
 * @retval WSREP_CONN_FAIL  must close client connection
 * @retval WSREP_NODE_FAIL  must close all connections and reinit
 *
 */
enum wsrep_status wsrep_replay_trx(trx_id_t trx_id, void *app_ctx);

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
enum wsrep_status wsrep_cancel_commit(
    bf_seqno_t bf_seqno, trx_id_t victim_trx
);
enum wsrep_status wsrep_cancel_slave(
    bf_seqno_t bf_seqno, bf_seqno_t victim_seqno
);
/*!
 * @brief withdraws a previously started commit
 *
 * wsrep_commit may stay waiting for total order semaphor
 * It is possible, that some other brute force transaction needs
 * to abort this commit operation.
 *
 * @param victim_seqno seqno of transaction, who needs to rollback
 *
 * @retval WSREP_OK         successful kill operaton
 * @retval WSREP_WARNING    could not kill the victim
 *
 */
enum wsrep_status wsrep_withdraw_commit(bf_seqno_t victim_seqno);
enum wsrep_status wsrep_withdraw_commit_by_trx(
    trx_id_t victim_trx
);

/*!
 * @brief marks the transaction as committed
 *
 * wsrep_committed marks the transaction as committed.
 * It also frees any resourced reserved for thye transaction.
 *
 * @param trx_id transaction which is committing
 * @retval WSREP_OK         cluster commit succeeded
 * @retval WSREP_TRX_FAIL   must rollback transaction
 */
enum wsrep_status wsrep_committed(trx_id_t trx_id);
enum wsrep_status wsrep_rolledback(trx_id_t trx_id);

/*!
 * @brief appends a query in transaction's write set
 *
 * @param trx_id transaction ID
 * @param query  SQL statement string
 */
enum wsrep_status wsrep_append_query(
    trx_id_t trx_id, const char *query, time_t timeval, uint32_t randseed
);

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
enum wsrep_status wsrep_append_row_key(
    trx_id_t trx_id,
    char    *dbtable,
    uint16_t dbtable_len,
    uint8_t *key,
    uint16_t key_len,
    enum wsrep_action action
);

/*!
 * @brief appends a set variable command connection's write set
 *
 * @param conn_id     connection ID
 * @param query       the set variable query
 * @param query_len   length of query (does not end with 0)
 * @param key         name of the variable, must be unique
 * @param key_len     length of the key data
 */
enum wsrep_status wsrep_set_variable(    
    conn_id_t conn_id, 
    char *key,   uint16_t key_len, 
    char *query, uint16_t query_len
);

/*!
 * @brief appends a set database command connection's write set
 *
 * @param conn_id     connection ID
 * @param query       the set database query
 * @param query_len   length of query (does not end with 0)
 */
enum wsrep_status wsrep_set_database(
    conn_id_t conn_id, char *query, uint16_t query_len
);


/*!
 * @brief executes a query under total order control
 *
 * wsrep_to_execute replicates the query and returns
 * success code, which caller must check. 
 * If commit was successful, transaction can commit, 
 * otherwise must rollback.
 *
 * @param trx_id transaction which is committing
 * @retval WSREP_OK         cluster commit succeeded
 * @retval WSREP_TRX_FAIL   must rollback transaction
 * @retval WSREP_CONN_FAIL  must close client connection
 * @retval WSREP_NODE_FAIL  must close all connections and reinit
 *
 *
 */
enum wsrep_status wsrep_to_execute_start(
    conn_id_t conn_id, char *query, uint16_t query_len
);
enum wsrep_status wsrep_to_execute_end(conn_id_t conn_id);

#undef WSREP_INTERFACE_VERSION
#define WSREP_INTERFACE_VERSION "1:0:0"

/*
 * wsrep interface for dynamically loadable libraries
 */
typedef struct wsrep_ wsrep_t;
struct wsrep_ {
    const char *version;
    wsrep_status_t (*init)(wsrep_t *, 
                            const char *gcs_group, 
                            const char *gcs_address, 
                            const char *data_dir,
                            wsrep_log_cb_t logger);
    
    wsrep_status_t (*enable)(wsrep_t *);
    wsrep_status_t (*disable)(wsrep_t *);
    
    
    wsrep_status_t (*recv)(wsrep_t *, void *);
    


    void (*dbug_push)(wsrep_t *, const char* ctrl);
    
    void (*dbug_pop)(wsrep_t *);
    
    wsrep_status_t (*set_logger)(wsrep_t *, wsrep_log_cb_t logger);

    wsrep_status_t (*set_conf_param_cb)(wsrep_t *, wsrep_conf_param_fun);
    
    wsrep_status_t (*set_execute_handler)(wsrep_t *,
                                           wsrep_bf_execute_fun);

    wsrep_status_t (*set_execute_handler_rbr)(wsrep_t *, 
                                               wsrep_bf_execute_fun);

    wsrep_status_t (*set_ws_start_handler)(wsrep_t *, wsrep_ws_start_fun);
    
    wsrep_status_t (*commit)(wsrep_t *, const trx_id_t, const conn_id_t, 
                              const char *, const size_t);
    
    wsrep_status_t (*replay_trx)(wsrep_t *,
                                  const trx_id_t trx_id,
                                  void *app_ctx);

    wsrep_status_t (*cancel_commit)(wsrep_t *,
                                     const bf_seqno_t bf_seqno,
                                     const trx_id_t);

    wsrep_status_t (*cancel_slave)(wsrep_t *,
                                    const bf_seqno_t bf_seqno,
                                    const bf_seqno_t victim_seqno);
    
    wsrep_status_t (*committed)(wsrep_t *, const trx_id_t);

    wsrep_status_t (*rolledback)(wsrep_t *, const trx_id_t);    

    wsrep_status_t (*append_query)(wsrep_t *, const trx_id_t, const char *, 
                                    const time_t, const uint32_t);
    
    wsrep_status_t (*append_row_key)(wsrep_t *, 
                                      const trx_id_t, 
                                      const char *dbtable,
                                      const size_t dbtable_len,
                                      const char *key, 
                                      const size_t key_len, 
                                      const wsrep_action_t action);
    
    wsrep_status_t (*set_variable)(wsrep_t *, const conn_id_t, 
                                    const char *key, const size_t key_len,
                                    const char *query, const size_t query_len);
    wsrep_status_t (*set_database)(wsrep_t *, const conn_id_t, 
                                    const char *query, const size_t query_len);
    
    wsrep_status_t (*to_execute_start)(wsrep_t *, 
                                        const conn_id_t, 
                                        const char *query, 
                                        const size_t query_len);
    wsrep_status_t (*to_execute_end)(wsrep_t *, conn_id_t conn_id);

    void (*tear_down)(wsrep_t *);
    void *dlh;
    void *opaque;
};
    
typedef int (*wsrep_loader_fun)(wsrep_t **);


/*!
 *
 * @brief loads wsrep library
 *
 * @param spec path to wsrep library 
 * @param hptr location to store wsrep handle
 *
 * @return zero on success, errno on failure
 */
int wsrep_load(const char *spec, wsrep_t **hptr);

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
