
#define GALERA_DEPRECATED 1
#include "galera.h"


#include <dlfcn.h>
#include <errno.h>
#include <assert.h>
#include <string.h>



/*
 * Backwards compatibility stuff
 */
static galera_t *galera_ctx = NULL;


enum galera_status galera_init(const char *gcs_group, 
                               const char *gcs_address, 
                               const char *data_dir,
                               galera_log_cb_t logger)
{
    const char *library;

    if (!(library = getenv("GALERA_LIBRARY")))
        return GALERA_FATAL;

    if (galera_load(library, &galera_ctx))
        return GALERA_FATAL;

    assert(galera_ctx);

    return galera_ctx->init(galera_ctx, gcs_group, gcs_address, data_dir, logger);
}


enum galera_status galera_tear_down()
{
    assert(galera_ctx);
    galera_unload(galera_ctx);
    return GALERA_OK;
}

enum galera_status galera_set_conf_param_cb(galera_conf_param_fun configurator)
{
    assert(galera_ctx);
    return galera_ctx->set_conf_param_cb(galera_ctx, configurator);
}

enum galera_status galera_set_logger(galera_log_cb_t logger)
{
    assert(galera_ctx);
    return galera_ctx->set_logger(galera_ctx, logger);
}

void galera_dbug_push(const char *control)
{
    assert(galera_ctx);
    galera_ctx->dbug_push(galera_ctx, control);
}

void galera_dbug_pop()
{
    assert(galera_ctx);
    galera_ctx->dbug_pop(galera_ctx);
}


enum galera_status galera_set_execute_handler(galera_bf_execute_fun fun)
{
    assert(galera_ctx);
    return galera_ctx->set_execute_handler(galera_ctx, fun);
}

enum galera_status galera_set_execute_handler_rbr(galera_bf_execute_fun fun)
{
    assert(galera_ctx);
    return galera_ctx->set_execute_handler_rbr(galera_ctx, fun);
}

enum galera_status galera_set_ws_start_handler(galera_ws_start_fun fun)
{
    assert(galera_ctx);
    return galera_ctx->set_ws_start_handler(galera_ctx, fun);
}

enum galera_status galera_enable()
{
    assert(galera_ctx);
    return galera_ctx->enable(galera_ctx);
}


enum galera_status galera_disable()
{
    assert(galera_ctx);
    return galera_ctx->disable(galera_ctx);
}

enum galera_status galera_recv(void *ctx)
{
    assert(galera_ctx);
    return galera_ctx->recv(galera_ctx, ctx);
}


enum galera_status galera_commit(trx_id_t trx_id, conn_id_t conn_id, 
                                 const char *rbr_data, size_t data_len)
{
    assert(galera_ctx);
    return galera_ctx->commit(galera_ctx, trx_id, conn_id, rbr_data, data_len);
}

enum galera_status galera_cancel_commit(trx_id_t victim_trx)
{
    assert(galera_ctx);
    return galera_ctx->cancel_commit(galera_ctx, victim_trx);
}


enum galera_status galera_withdraw_commit(uint64_t seqno)
{
    assert(galera_ctx);
    return galera_ctx->withdraw_commit(galera_ctx, seqno);
}

enum galera_status galera_committed(trx_id_t trx_id)
{
    assert(galera_ctx);
    return galera_ctx->committed(galera_ctx, trx_id);
}

enum galera_status galera_rolledback(trx_id_t trx_id)
{
    assert(galera_ctx);
    return galera_ctx->rolledback(galera_ctx, trx_id);
}

enum galera_status galera_append_query(
    trx_id_t trx_id, char *query, time_t timeval, uint32_t randseed
)
{
    assert(galera_ctx);
    return galera_ctx->append_query(galera_ctx, trx_id, query, timeval, randseed);
}


enum galera_status galera_append_row_key(
    trx_id_t trx_id,
    char    *dbtable,
    uint16_t dbtable_len,
    uint8_t *key,
    uint16_t key_len,
    enum galera_action action)
{
    assert(galera_ctx);
    return galera_ctx->append_row_key(galera_ctx, trx_id, dbtable, dbtable_len,
                                      (char*)key, key_len, action);
}

enum galera_status galera_set_variable(    
    conn_id_t conn_id, 
    char *key,   uint16_t key_len, 
    char *query, uint16_t query_len)
{
    assert(galera_ctx);
    return galera_ctx->set_variable(galera_ctx, conn_id, key, key_len, query, query_len);
}

enum galera_status galera_set_database(
    conn_id_t conn_id, char *query, uint16_t query_len)
{
    assert(galera_ctx);
    return galera_ctx->set_database(galera_ctx, conn_id, query, query_len);
}

enum galera_status galera_to_execute_start(
    conn_id_t conn_id, char *query, uint16_t query_len
)
{
    assert(galera_ctx);
    return galera_ctx->to_execute_start(galera_ctx, conn_id, query, query_len);
}

enum galera_status galera_to_execute_end(conn_id_t conn_id)
{
    assert(galera_ctx);
    return galera_ctx->to_execute_end(galera_ctx, conn_id);
}


#undef GALERA_DEPRECATED

/**************************************************************************
 * Library loader
 **************************************************************************/

static int verify(const galera_t *gh, const char *iface_ver)
{
#define VERIFY(_p) if (!(_p)) return EINVAL;
    VERIFY(gh);
    VERIFY(gh->version);
    VERIFY(strcmp(gh->version, iface_ver) == 0);
    VERIFY(gh->init);
    VERIFY(gh->deinit);
    VERIFY(gh->enable);
    VERIFY(gh->disable);
    VERIFY(gh->recv);
    VERIFY(gh->dbug_push);
    VERIFY(gh->dbug_pop);
    VERIFY(gh->set_logger);
    VERIFY(gh->set_conf_param_cb);
    VERIFY(gh->set_execute_handler);
    VERIFY(gh->set_execute_handler_rbr);
    VERIFY(gh->set_ws_start_handler);
    VERIFY(gh->commit);
    VERIFY(gh->cancel_commit);
    VERIFY(gh->withdraw_commit);
    VERIFY(gh->committed);
    VERIFY(gh->rolledback);
    VERIFY(gh->append_query);
    VERIFY(gh->append_row_key);
    VERIFY(gh->set_variable);
    VERIFY(gh->set_database);
    VERIFY(gh->to_execute_start);
    VERIFY(gh->to_execute_end);
    return 0;
}

int galera_load(const char *spec, galera_t **hptr)
{
    int ret = 0;
    void *dlh = NULL;
    galera_loader_fun dlfun;

    if (!(spec && hptr))
        return EINVAL;

    *hptr = NULL;
    
    if (!(dlh = dlopen(spec, RTLD_LOCAL))) {
        ret = errno;
        goto out;
    }
    
    if (!(*(void **)(&dlfun) = dlsym(dlh, "galera_loader"))) {
        ret = errno;
        goto out;
        
    }

    ret = (*dlfun)(hptr);
    
    if (ret == 0 && !*hptr)
        ret = EACCES;
    if (ret == 0)
        ret = verify(*hptr, GALERA_INTERFACE_VERSION);
    if (ret == 0)
        (*hptr)->dlh = dlh;
out:
    if (!*hptr)
        dlclose(dlh);
    return ret;
}



void galera_unload(galera_t *hptr)
{
    void *dlh;
    assert(hptr);
    dlh = hptr->dlh;
    hptr->tear_down(hptr);
    if (dlh)
        dlclose(dlh);
}

