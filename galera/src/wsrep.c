

#include "wsrep.h"


#include <dlfcn.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>

static wsrep_t *wsrep_ctx = NULL;
static int dummy_mode = 0;

#define WSREP_INTERFACE_VERSION "1:0:0"

enum wsrep_status wsrep_init(const char *gcs_group, 
                               const char *gcs_address, 
                               const char *data_dir,
                               wsrep_log_cb_t logger)
{
    if (dummy_mode)
	return WSREP_OK;

    assert(wsrep_ctx);
    fprintf(stderr, "library loaded successfully\n");
    return wsrep_ctx->init(wsrep_ctx, gcs_group, gcs_address, data_dir, logger);
}


enum wsrep_status wsrep_tear_down()
{
    if (dummy_mode)
	return WSREP_OK;

    assert(wsrep_ctx);
    wsrep_ctx->tear_down(wsrep_ctx);
    return WSREP_OK;
}

enum wsrep_status wsrep_set_conf_param_cb(wsrep_conf_param_fun configurator)
{
    if (dummy_mode)
	return WSREP_OK;
    
    assert(wsrep_ctx);
    return wsrep_ctx->set_conf_param_cb(wsrep_ctx, configurator);
}

enum wsrep_status wsrep_set_logger(wsrep_log_cb_t logger)
{
    if (dummy_mode)
	return WSREP_OK;
    
    assert(wsrep_ctx);
    return wsrep_ctx->set_logger(wsrep_ctx, logger);
}

void wsrep_dbug_push(const char *control)
{
    if (dummy_mode)
	return;
    
    assert(wsrep_ctx);
    wsrep_ctx->dbug_push(wsrep_ctx, control);
}

void wsrep_dbug_pop()
{
    if (dummy_mode)
	return;
    
    assert(wsrep_ctx);
    wsrep_ctx->dbug_pop(wsrep_ctx);
}


enum wsrep_status wsrep_set_execute_handler(wsrep_bf_execute_fun fun)
{
    if (dummy_mode)
	return WSREP_OK;
    
    assert(wsrep_ctx);
    return wsrep_ctx->set_execute_handler(wsrep_ctx, fun);
}

enum wsrep_status wsrep_set_execute_handler_rbr(wsrep_bf_execute_fun fun)
{
    if (dummy_mode)
	return WSREP_OK;
    
    assert(wsrep_ctx);
    return wsrep_ctx->set_execute_handler_rbr(wsrep_ctx, fun);
}

enum wsrep_status wsrep_set_ws_start_handler(wsrep_ws_start_fun fun)
{
    if (dummy_mode)
	return WSREP_OK;
    
    assert(wsrep_ctx);
    return wsrep_ctx->set_ws_start_handler(wsrep_ctx, fun);
}

enum wsrep_status wsrep_enable()
{
    if (dummy_mode)
	return WSREP_OK;
    
    assert(wsrep_ctx);
    return wsrep_ctx->enable(wsrep_ctx);
}


enum wsrep_status wsrep_disable()
{
    if (dummy_mode)
	return WSREP_OK;
    
    assert(wsrep_ctx);
    return wsrep_ctx->disable(wsrep_ctx);
}

enum wsrep_status wsrep_recv(void *ctx)
{
    if (dummy_mode) {
        sleep(UINT_MAX);
	return WSREP_OK;
    }
    assert(wsrep_ctx);
    return wsrep_ctx->recv(wsrep_ctx, ctx);
}


enum wsrep_status wsrep_commit(trx_id_t trx_id, conn_id_t conn_id, 
                                 const char *rbr_data, size_t data_len)
{
    if (dummy_mode)
	return WSREP_OK;
    
    assert(wsrep_ctx);
    return wsrep_ctx->commit(wsrep_ctx, trx_id, conn_id, rbr_data, data_len);
}

enum wsrep_status wsrep_replay_trx(trx_id_t trx_id, void *app_ctx)
{
    if (dummy_mode)
	return WSREP_OK;
    
    assert(wsrep_ctx);
    return wsrep_ctx->replay_trx(wsrep_ctx, trx_id, app_ctx);
}

enum wsrep_status wsrep_cancel_commit(bf_seqno_t bf_seqno, trx_id_t victim_trx)
{
    if (dummy_mode)
	return WSREP_OK;
    
    assert(wsrep_ctx);
    return wsrep_ctx->cancel_commit(wsrep_ctx, bf_seqno, victim_trx);
}


enum wsrep_status wsrep_cancel_slave(bf_seqno_t bf_seqno, bf_seqno_t victim_seqno)
{
    if (dummy_mode)
        return WSREP_OK;
    assert(wsrep_ctx);
    return wsrep_ctx->cancel_slave(wsrep_ctx, bf_seqno, victim_seqno);
}

enum wsrep_status wsrep_committed(trx_id_t trx_id)
{
    if (dummy_mode)
	return WSREP_OK;
    
    assert(wsrep_ctx);
    return wsrep_ctx->committed(wsrep_ctx, trx_id);
}

enum wsrep_status wsrep_rolledback(trx_id_t trx_id)
{
    if (dummy_mode)
	return WSREP_OK;
    
    assert(wsrep_ctx);
    return wsrep_ctx->rolledback(wsrep_ctx, trx_id);
}

enum wsrep_status wsrep_append_query(
    trx_id_t trx_id, const char *query, time_t timeval, uint32_t randseed
)
{
    if (dummy_mode)
	return WSREP_OK;
    
    assert(wsrep_ctx);
    return wsrep_ctx->append_query(wsrep_ctx, trx_id, query, timeval, randseed);
}


enum wsrep_status wsrep_append_row_key(
    trx_id_t trx_id,
    char    *dbtable,
    uint16_t dbtable_len,
    uint8_t *key,
    uint16_t key_len,
    enum wsrep_action action)
{
    if (dummy_mode)
	return WSREP_OK;
    
    assert(wsrep_ctx);
    return wsrep_ctx->append_row_key(wsrep_ctx, trx_id, dbtable, dbtable_len,
                                      (char*)key, key_len, action);
}

enum wsrep_status wsrep_set_variable(    
    conn_id_t conn_id, 
    char *key,   uint16_t key_len, 
    char *query, uint16_t query_len)
{
    if (dummy_mode)
	return WSREP_OK;
    
    assert(wsrep_ctx);
    return wsrep_ctx->set_variable(wsrep_ctx, conn_id, key, key_len, query, query_len);
}

enum wsrep_status wsrep_set_database(
    conn_id_t conn_id, char *query, uint16_t query_len)
{
    if (dummy_mode)
	return WSREP_OK;
    
    assert(wsrep_ctx);
    return wsrep_ctx->set_database(wsrep_ctx, conn_id, query, query_len);
}

enum wsrep_status wsrep_to_execute_start(
    conn_id_t conn_id, char *query, uint16_t query_len
)
{
    if (dummy_mode)
	return WSREP_OK;
    
    assert(wsrep_ctx);
    return wsrep_ctx->to_execute_start(wsrep_ctx, conn_id, query, query_len);
}

enum wsrep_status wsrep_to_execute_end(conn_id_t conn_id)
{
    if (dummy_mode)
	return WSREP_OK;
    
    assert(wsrep_ctx);
    return wsrep_ctx->to_execute_end(wsrep_ctx, conn_id);
}



/**************************************************************************
 * Library loader
 **************************************************************************/

static int verify(const wsrep_t *gh, const char *iface_ver)
{
#define VERIFY(_p) if (!(_p)) {					\
	fprintf(stderr, "wsrep_load(): verify(): %s\n", # _p);	\
	return EINVAL;						\
    }

    VERIFY(gh);
    VERIFY(gh->version);
    VERIFY(strcmp(gh->version, iface_ver) == 0);
    VERIFY(gh->init);
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
    VERIFY(gh->replay_trx);
    VERIFY(gh->cancel_commit);
    VERIFY(gh->cancel_slave);
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


static wsrep_loader_fun wsrep_dlf(void *dlh, const char *sym)
{
    union {
	wsrep_loader_fun dlfun;
	void *obj;
    } alias;
    alias.obj = dlsym(dlh, sym);
    return alias.dlfun;
}

int wsrep_load(const char *spec, wsrep_t **hptr)
{
    int ret = 0;
    void *dlh = NULL;
    wsrep_loader_fun dlfun;

    
    if (!spec)
        return EINVAL;

    fprintf(stderr, "wsrep_load(): loading %s\n", spec);

    if (strcmp(spec, "dummy") == 0) {
	dummy_mode = 1;
	return 0;
    }

    if (!hptr) {
        if (wsrep_ctx)
            return EBUSY;
        hptr = &wsrep_ctx;
    } else {
        *hptr = NULL;
    }
    
    if (!(dlh = dlopen(spec, RTLD_NOW | RTLD_LOCAL))) {
	fprintf(stderr, "wsrep_load(): dlopen(): %s\n", dlerror());
        ret = EINVAL;
        goto out;
    }

    if (!(dlfun = wsrep_dlf(dlh, "wsrep_loader"))) {
	fprintf(stderr, "wsrep_load(): dlopen(): %s\n", dlerror());
        ret = EINVAL;
        goto out;
        
    }

    ret = (*dlfun)(hptr);
    
    if (ret == 0 && !*hptr) {
	fprintf(stderr, "wsrep_load(): loader failed\n");
        ret = EACCES;
    }
    if (ret == 0 && 
        (ret = verify(*hptr, WSREP_INTERFACE_VERSION)) != 0 &&
        (*hptr)->tear_down) {
	fprintf(stderr, "wsrep_load(): interface version mismatch\n");
        (*hptr)->tear_down(*hptr);
    }
    if (ret == 0)
        (*hptr)->dlh = dlh;
out:
    if (ret != 0 && dlh)
        dlclose(dlh);
    if (ret == 0) {
      fprintf(stderr, "wsrep_load(): driver loaded succesfully\n");
    }
    return ret;
}



void wsrep_unload(wsrep_t *hptr)
{
    void *dlh;

    if (dummy_mode) {
	dummy_mode = 0;
	return;
    }
    
    if (!hptr) {
        hptr = wsrep_ctx;
        wsrep_ctx = NULL;
    }
    assert(hptr);
    dlh = hptr->dlh;
    if (dlh)
        dlclose(dlh);
}

