#ifndef GALERA_H
#define GALERA_H

#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  Galera replication API
 */

/* status codes */
typedef enum galera_status {
    GALERA_OK        = 0, //!< success
    GALERA_WARNING,       //!< minor warning, error logged
    GALERA_TRX_MISSING,   //!< transaction is not known by galera
    GALERA_TRX_FAIL,      //!< transaction aborted, server can continue
    GALERA_CONN_FAIL,     //!< error in client connection, must abort
    GALERA_NODE_FAIL,     //!< error in node state, galera must reinit
    GALERA_FATAL,         //!< fatal error, server must abort
} galera_status_t;

/* configuration parameters */
typedef enum galera_conf_param_id {
    GALERA_CONF_LOCAL_CACHE_SIZE, //!< max size for local cache
    GALERA_CONF_WS_PERSISTENCY,   //!< WS persistency policy
    GALERA_CONF_MARK_COMMIT_EARLY, //!< update last seen trx asap
} galera_conf_param_id_t;

typedef enum galera_conf_param_type {
    GALERA_TYPE_INT,     //!< integer type
    GALERA_TYPE_DOUBLE,  //!< float
    GALERA_TYPE_STRING,  //!< null terminated string
} galera_conf_param_type_t;

/*!
 * @brief callback to return configuration parameter value
 *        The function should be able to return values for all
 *        parameters defined in enum galera_conf_param_id
 *
 * @param configuration parameter identifier
 */
typedef void * (*galera_conf_param_fun)(
    enum galera_conf_param_id, enum galera_conf_param_type
);

/*!
 * @brief sets the configuration parameter callback
 *
 * @param configurator   handler for returning configuration parameter values
 *
 */
enum galera_status galera_set_conf_param_cb(
    galera_conf_param_fun configurator
);

/*!
 * @brief retains the connection context specified by the
 *        context parameter
 *
 * @param context pointer to context info provided by application
 */
typedef int (*galera_context_retain_fun)(void *context);

/*!
 * @brief stores the connection context specified by the
 *        context parameter
 *
 * @return context pointer to context info provided by application
 */
typedef void* (*galera_context_store_fun)();

/*! 
 * Log severity levels, passed as first argument to log handler
 * @todo: how to synchronize it automatically with galerautils?
 */
typedef enum galera_severity
{
    GALERA_LOG_FATAL, //! Unrecoverable error, application must quit.
    GALERA_LOG_ERROR, //! Operation failed, must be repeated. 
    GALERA_LOG_WARN,  //! Unexpected condition, but no operational failure.
    GALERA_LOG_INFO,  //! Informational message.
    GALERA_LOG_DEBUG  //! Debug message. Shows only of compiled with debug.
} galera_severity_t;

/*!
 * @brief error log handler
 *
 *        All messages from galera library are directed to this
 *        handler, if present.
 *
 * @param severity code
 * @param error message
 */
typedef void (*galera_log_cb_t)(int, const char *);

typedef uint64_t ws_id_t;

/*!
 * @brief transaction initialization function
 *
 *        This handler is called from galera library to initialize
 *        the context for following write set applying
 *
 * @param context pointer provided by the application
 * @param sequence number
 */
typedef int (*galera_ws_start_fun)(void *ctx, ws_id_t seqno);

/*!
 * @brief brute force apply function
 *
 *        This handler is called from galera library to execute
 *        the passed SQL statement in brute force.
 *
 * @param context pointer provided by the application
 * @param the SQL statement to execute
 */
typedef int (*galera_bf_execute_fun)(
    void *ctx, char *SQL, size_t sql_len, time_t timeval, uint32_t randseed
);

/*!
 * @brief brute force apply function
 *
 *        This handler is called from galera library to apply
 *        the passed data row in brute force.
 *
 * @param context pointer provided by the application
 * @param the data row
 * @param length of data
 */
typedef int (*galera_bf_apply_row_fun)(void *ctx, void *data, size_t len);

/*!
 * @brief galera shutdown, all memory objects are freed.
 *
 */
enum galera_status galera_tear_down();

/*!
 * @brief sets the logger callback for galera library
 * If logger is NULL, logger callback is not active
 *
 * @param error_fun   handler for error logging from galera library
 *
 */
enum galera_status galera_set_logger(galera_log_cb_t logger);

/*!
 * @brief initializes galera library
 *
 * @param gcs_group   symbolic group name (serves as unique group ID)
 * @param gcs_address URL-like gcs connection address (backend://address)
 *                    Currently supported backends: "dummy", "spread", "gcomm".
 * @param data_dir    directory where wsdb files are kept
 * @param error_fun   handler for error logging from galeraw library
 *
 */
enum galera_status galera_init (const char           *gcs_group,
				const char           *gcs_address,
				const char           *data_dir,
				galera_log_cb_t       logger);
/*!
 * @brief Push/pop DBUG control string to galera own DBUG implementation.
 *        (optional)
 *
 * @param control DBUG library control string
 */
void galera_dbug_push (const char* control);
void galera_dbug_pop  (void);

/*!
 * @brief assigns handler for brute force applying of SQL statements
 *
 */
enum galera_status galera_set_execute_handler(galera_bf_execute_fun);

/*!
 * @brief assigns handler for brute force applying of a custom
 *        objects carring rows info such as mysql row-based replication events
 *
 */
enum galera_status galera_set_execute_handler_rbr(galera_bf_execute_fun);

/*!
 * @brief assigns handler for starting of write set applying
 *
 */
enum galera_status galera_set_ws_start_handler(galera_ws_start_fun);

/* replication enable/disable */
enum galera_status galera_enable();
enum galera_status galera_disable();


/*!
 * @brief start replication receiving
 *
 * This function never returns
 *
 */
enum galera_status galera_recv(void *ctx);

typedef uint64_t trx_id_t;
typedef uint64_t conn_id_t;


/*!
 * @brief performs commit time operations
 *
 * galera_commit replicates the transaction and returns
 * success code, which caller must check. 
 * If commit was successful, transaction can commit, 
 * otherwise must rollback.
 *
 * @param trx_id transaction which is committing
 * @param conn_id
 * @param rbr_data binary data when rbr is set
 * @param data_len the size of the rbr data

 * @retval GALERA_OK         cluster commit succeeded
 * @retval GALERA_TRX_FAIL   must rollback transaction
 * @retval GALERA_CONN_FAIL  must close client connection
 * @retval GALERA_NODE_FAIL  must close all connections and reinit
 *
 *
 */
enum galera_status galera_commit(trx_id_t trx_id, conn_id_t conn_id, const char *rbr_data, size_t data_len);

/*!
 * @brief cancels a previously started commit
 *
 * galera_commit may stay waiting for total order semaphor
 * It is possible, that some other brute force transaction needs
 * to abort this commit operation.
 *
 * The kill routine checks that cancel is not tried against a transaction
 * who is front of the caller (in total order).
 *
 * @param victim_trx transaction to be killed, and which is committing
 *
 * @retval GALERA_OK         successful kill operaton
 * @retval GALERA_WARNING    could not kill the victim
 *
 */
enum galera_status galera_cancel_commit(
    trx_id_t victim_trx
);

/*!
 * @brief withdraws a previously started commit
 *
 * galera_commit may stay waiting for total order semaphor
 * It is possible, that some other brute force transaction needs
 * to abort this commit operation.
 *
 * @param victim_seqno seqno of transaction, who needs to rollback
 *
 * @retval GALERA_OK         successful kill operaton
 * @retval GALERA_WARNING    could not kill the victim
 *
 */
enum galera_status galera_withdraw_commit(uint64_t victim_seqno);

/*!
 * @brief marks the transaction as committed
 *
 * galera_committed marks the transaction as committed.
 * It also frees any resourced reserved for thye transaction.
 *
 * @param trx_id transaction which is committing
 * @retval GALERA_OK         cluster commit succeeded
 * @retval GALERA_TRX_FAIL   must rollback transaction
 */
enum galera_status galera_committed(trx_id_t trx_id);
enum galera_status galera_rolledback(trx_id_t trx_id);

/*!
 * @brief appends a query in transaction's write set
 *
 * @param trx_id transaction ID
 * @param query  SQL statement string
 */
enum galera_status galera_append_query(
    trx_id_t trx_id, char *query, time_t timeval, uint32_t randseed
);

typedef enum galera_action {
    GALERA_UPDATE,
    GALERA_DELETE,
    GALERA_INSERT,
} galera_action_t;

/*!
 * @brief appends a row reference in transaction's write set
 *
 * @param trx_id      transaction ID
 * @param dbtable     unique name of the table "db.table"
 * @param dbtable_len length of table name (does not end with 0)
 * @param key         binary key data
 * @param key_len     length of the key data
 * @param action      action code according to enum galera_action
 */
enum galera_status galera_append_row_key(
    trx_id_t trx_id,
    char    *dbtable,
    uint16_t dbtable_len,
    uint8_t *key,
    uint16_t key_len,
    enum galera_action action
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
enum galera_status galera_set_variable(    
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
enum galera_status galera_set_database(
    conn_id_t conn_id, char *query, uint16_t query_len
);


/*!
 * @brief executes a query under total order control
 *
 * galera_to_execute replicates the query and returns
 * success code, which caller must check. 
 * If commit was successful, transaction can commit, 
 * otherwise must rollback.
 *
 * @param trx_id transaction which is committing
 * @retval GALERA_OK         cluster commit succeeded
 * @retval GALERA_TRX_FAIL   must rollback transaction
 * @retval GALERA_CONN_FAIL  must close client connection
 * @retval GALERA_NODE_FAIL  must close all connections and reinit
 *
 *
 */
enum galera_status galera_to_execute_start(
    conn_id_t conn_id, char *query, uint16_t query_len
);
enum galera_status galera_to_execute_end(conn_id_t conn_id);





/*
 * Galera interface for dynamically loadable libraries
 */
typedef struct galera_ galera_t;
struct galera_ {
    const char *version;
    galera_status_t (*init)(galera_t *, 
                            const char *gcs_group, 
                            const char *gcs_address, 
                            const char *data_dir,
                            galera_log_cb_t logger);
    galera_status_t (*deinit)(galera_t *);
    
    galera_status_t (*enable)(galera_t *);
    galera_status_t (*disable)(galera_t *);
    
    
    galera_status_t (*recv)(galera_t *, void *);
    


    void (*dbug_push)(galera_t *, const char* ctrl);
    
    void (*dbug_pop)(galera_t *);
    
    galera_status_t (*set_logger)(galera_t *, galera_log_cb_t logger);

    galera_status_t (*set_conf_param_cb)(galera_t *, galera_conf_param_fun);
    
    galera_status_t (*set_execute_handler)(galera_t *,
                                           galera_bf_execute_fun);

    galera_status_t (*set_execute_handler_rbr)(galera_t *, 
                                               galera_bf_execute_fun);


    galera_status_t (*set_ws_start_handler)(galera_t *, galera_ws_start_fun);
    
    
    
    galera_status_t (*commit)(galera_t *, const trx_id_t, const conn_id_t, 
                              const char *, const size_t);
    

    
    galera_status_t (*cancel_commit)(galera_t *, const trx_id_t);
    galera_status_t (*withdraw_commit)(galera_t *, const ws_id_t);

    galera_status_t (*committed)(galera_t *, const trx_id_t);
    galera_status_t (*rolledback)(galera_t *, const trx_id_t);
    

    galera_status_t (*append_query)(galera_t *, const trx_id_t, const char *, 
                                    const time_t, const uint32_t);
    
    galera_status_t (*append_row_key)(galera_t *, 
                                      const trx_id_t, 
                                      const char *dbtable,
                                      const size_t dbtable_len,
                                      const char *key, 
                                      const size_t key_len, 
                                      const galera_action_t action);
    
    galera_status_t (*set_variable)(galera_t *, const conn_id_t, 
                                    const char *key, const size_t key_len,
                                    const char *query, const size_t query_len);
    galera_status_t (*set_database)(galera_t *, const conn_id_t, 
                                    const char *query, const size_t query_len);
    
    galera_status_t (*to_execute_start)(galera_t *, 
                                        const conn_id_t, 
                                        const char *query, 
                                        const size_t query_len);
    galera_status_t (*to_execute_end)(galera_t *, conn_id_t conn_id);

    void (*tear_down)(galera_t *);
    void *dlh;
    void *opaque;
};
    
typedef int (*galera_loader_fun)(galera_t **);


/*!
 *
 * @brief loads galera library
 *
 * @param spec path to galera library 
 * @param hptr location to store galera handle
 *
 * @return zero on success, errno on failure
 */
int galera_load(const char *spec, galera_t **hptr);

/*!
 * @brief unload galera library and free resources
 * 
 * @param hptr galera handler pointer
 */
void galera_unload(galera_t *hptr);
    
#ifdef __cplusplus
}
#endif


#endif /* GALERA_H */
