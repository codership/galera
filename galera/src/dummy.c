
#include <galera.h>

#include <errno.h>
#include <string.h>

typedef struct dg_ {
    int foo;
} dg_t;

#define PRIV(_p) ((dg_t *)(_p)->opaque);


static void dg_tear_down(galera_t *hptr)
{
    dg_t *dg = PRIV(hptr);
    free(dg);
    free(hptr);
}


static galera_status_t dg_init(galera_t *g, const char *gcs_group, 
                               const char *gcs_address, 
                               const char *data_dir, galera_log_cb_t logger)
{
    return GALERA_OK;
}

static galera_status_t dg_deinit(galera_t *g)
{
    return GALERA_OK;
}

static galera_status_t dg_enable(galera_t *g)
{
    return GALERA_OK;
}

static galera_status_t dg_disable(galera_t *g)
{
    return GALERA_OK;
}

static galera_status_t dg_recv(galera_t *g, void *ctx)
{
    return GALERA_OK;
}

static void dg_dbug_push(galera_t *g, const char *ctrl)
{

}

static void dg_dbug_pop(galera_t *g)
{

}

static galera_status_t dg_set_logger(galera_t *g, galera_log_cb_t logger)
{
    return GALERA_OK;
}

static galera_status_t dg_set_conf_param_cb(galera_t *g, galera_conf_param_fun fun)
{

    return GALERA_OK;
}

static galera_status_t dg_set_execute_handler(galera_t *g, galera_bf_execute_fun fun)
{
    return GALERA_OK;
}

static galera_status_t dg_set_execute_handler_rbr(galera_t *g, galera_bf_execute_fun fun)
{
    return GALERA_OK;
}

static galera_status_t dg_set_ws_start_handler(galera_t *g, galera_ws_start_fun fun)
{
    return GALERA_OK;
}

static galera_status_t dg_commit(galera_t *g, const trx_id_t trx_id, const conn_id_t conn_id, const char *query, const size_t query_len)
{
    return GALERA_OK;
}

static galera_status_t dg_cancel_commit(galera_t *g, const trx_id_t trx_id)
{
    return GALERA_OK;
}

static galera_status_t dg_withdraw_commit(galera_t *g, const ws_id_t seqno)
{
    return GALERA_OK;
}

static galera_status_t dg_committed(galera_t *g, const trx_id_t trx_id)
{
    return GALERA_OK;
}

static galera_status_t dg_rolledback(galera_t *g, const trx_id_t trx_id)
{
    return GALERA_OK;
}


static galera_status_t dg_append_query(galera_t *g, const trx_id_t trx_id, 
                                       const char *query, const time_t timeval,
                                       const uint32_t randseed)
{
    return GALERA_OK;
}

static galera_status_t dg_append_row_key(galera_t *g, 
                                         const trx_id_t trx_id, 
                                         const char *dbtable,
                                         const size_t dbtable_len,
                                         const char *key, 
                                         const size_t key_len, 
                                         const galera_action_t action)
{
    return GALERA_OK;
}


static galera_status_t dg_set_variable(galera_t *g, const conn_id_t conn_id, 
                                       const char *key, const size_t key_len,
                                       const char *query, const size_t query_len)
{
    return GALERA_OK;
}

static galera_status_t dg_set_database(galera_t *g, 
                                       const conn_id_t conn_id, 
                                       const char *query, 
                                       const size_t query_len)
{
    return GALERA_OK;
}

static galera_status_t dg_to_execute_start(galera_t *g, 
                                           const conn_id_t conn_id,
                                           const char *query, 
                                           const size_t query_len)
{
    return GALERA_OK;
}

static galera_status_t dg_to_execute_end(galera_t *g,
                                         const conn_id_t conn_id)
{
    return GALERA_OK;
}


static galera_t dg_init_str = {
    GALERA_INTERFACE_VERSION,
    &dg_init,
    &dg_deinit,
    &dg_enable,
    &dg_disable,
    &dg_recv,
    &dg_dbug_push,
    &dg_dbug_pop,
    &dg_set_logger,
    &dg_set_conf_param_cb,
    &dg_set_execute_handler,
    &dg_set_execute_handler_rbr,
    &dg_set_ws_start_handler,
    &dg_commit,
    &dg_cancel_commit,
    &dg_withdraw_commit,
    &dg_committed,
    &dg_rolledback,
    &dg_append_query,
    &dg_append_row_key,
    &dg_set_variable,
    &dg_set_database,
    &dg_to_execute_start,
    &dg_to_execute_end,
    &dg_tear_down,
    NULL,
    NULL
};

int galera_loader(galera_t **hptr)
{
    if (!hptr)
        return EINVAL;
    
    *hptr = malloc(sizeof(galera_t));
    if (!*hptr)
        return ENOMEM;
    
    memset(*hptr, 0, sizeof(galera_t));
    **hptr = dg_init_str;

    return 0;
}
