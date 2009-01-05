

#include "galera.h"


#include <dlfcn.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>

static galera_t *galera_ctx = NULL;
static int dummy_mode = 0;

#define GALERA_INTERFACE_VERSION "1:0:0"

enum galera_status galera_init(const char *gcs_group, 
                               const char *gcs_address, 
                               const char *data_dir,
                               galera_log_cb_t logger)
{
    if (dummy_mode)
	return GALERA_OK;

    assert(galera_ctx);
    fprintf(stderr, "library loaded successfully\n");
    return galera_ctx->init(galera_ctx, gcs_group, gcs_address, data_dir, logger);
}


enum galera_status galera_tear_down()
{
    if (dummy_mode)
	return GALERA_OK;

    assert(galera_ctx);
    galera_ctx->tear_down(galera_ctx);
    return GALERA_OK;
}

enum galera_status galera_set_conf_param_cb(galera_conf_param_fun configurator)
{
    if (dummy_mode)
	return GALERA_OK;
    
    assert(galera_ctx);
    return galera_ctx->set_conf_param_cb(galera_ctx, configurator);
}

enum galera_status galera_set_logger(galera_log_cb_t logger)
{
    if (dummy_mode)
	return GALERA_OK;
    
    assert(galera_ctx);
    return galera_ctx->set_logger(galera_ctx, logger);
}

void galera_dbug_push(const char *control)
{
    if (dummy_mode)
	return;
    
    assert(galera_ctx);
    galera_ctx->dbug_push(galera_ctx, control);
}

void galera_dbug_pop()
{
    if (dummy_mode)
	return;
    
    assert(galera_ctx);
    galera_ctx->dbug_pop(galera_ctx);
}


enum galera_status galera_set_execute_handler(galera_bf_execute_fun fun)
{
    if (dummy_mode)
	return GALERA_OK;
    
    assert(galera_ctx);
    return galera_ctx->set_execute_handler(galera_ctx, fun);
}

enum galera_status galera_set_execute_handler_rbr(galera_bf_execute_fun fun)
{
    if (dummy_mode)
	return GALERA_OK;
    
    assert(galera_ctx);
    return galera_ctx->set_execute_handler_rbr(galera_ctx, fun);
}

enum galera_status galera_set_ws_start_handler(galera_ws_start_fun fun)
{
    if (dummy_mode)
	return GALERA_OK;
    
    assert(galera_ctx);
    return galera_ctx->set_ws_start_handler(galera_ctx, fun);
}

enum galera_status galera_enable()
{
    if (dummy_mode)
	return GALERA_OK;
    
    assert(galera_ctx);
    return galera_ctx->enable(galera_ctx);
}


enum galera_status galera_disable()
{
    if (dummy_mode)
	return GALERA_OK;
    
    assert(galera_ctx);
    return galera_ctx->disable(galera_ctx);
}

enum galera_status galera_recv(void *ctx)
{
    if (dummy_mode) {
        sleep(UINT_MAX);
	return GALERA_OK;
    }
    assert(galera_ctx);
    return galera_ctx->recv(galera_ctx, ctx);
}


enum galera_status galera_commit(trx_id_t trx_id, conn_id_t conn_id, 
                                 const char *rbr_data, size_t data_len)
{
    if (dummy_mode)
	return GALERA_OK;
    
    assert(galera_ctx);
    return galera_ctx->commit(galera_ctx, trx_id, conn_id, rbr_data, data_len);
}

enum galera_status galera_replay_trx(trx_id_t trx_id, void *app_ctx)
{
    if (dummy_mode)
	return GALERA_OK;
    
    assert(galera_ctx);
    return galera_ctx->replay_trx(galera_ctx, trx_id, app_ctx);
}

enum galera_status galera_cancel_commit(bf_seqno_t bf_seqno, trx_id_t victim_trx)
{
    if (dummy_mode)
	return GALERA_OK;
    
    assert(galera_ctx);
    return galera_ctx->cancel_commit(galera_ctx, bf_seqno, victim_trx);
}


enum galera_status galera_cancel_slave(bf_seqno_t bf_seqno, bf_seqno_t victim_seqno)
{
    if (dummy_mode)
        return GALERA_OK;
    assert(galera_ctx);
    return galera_ctx->cancel_slave(galera_ctx, bf_seqno, victim_seqno);
}

enum galera_status galera_committed(trx_id_t trx_id)
{
    if (dummy_mode)
	return GALERA_OK;
    
    assert(galera_ctx);
    return galera_ctx->committed(galera_ctx, trx_id);
}

enum galera_status galera_rolledback(trx_id_t trx_id)
{
    if (dummy_mode)
	return GALERA_OK;
    
    assert(galera_ctx);
    return galera_ctx->rolledback(galera_ctx, trx_id);
}

enum galera_status galera_append_query(
    trx_id_t trx_id, char *query, time_t timeval, uint32_t randseed
)
{
    if (dummy_mode)
	return GALERA_OK;
    
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
    if (dummy_mode)
	return GALERA_OK;
    
    assert(galera_ctx);
    return galera_ctx->append_row_key(galera_ctx, trx_id, dbtable, dbtable_len,
                                      (char*)key, key_len, action);
}

enum galera_status galera_set_variable(    
    conn_id_t conn_id, 
    char *key,   uint16_t key_len, 
    char *query, uint16_t query_len)
{
    if (dummy_mode)
	return GALERA_OK;
    
    assert(galera_ctx);
    return galera_ctx->set_variable(galera_ctx, conn_id, key, key_len, query, query_len);
}

enum galera_status galera_set_database(
    conn_id_t conn_id, char *query, uint16_t query_len)
{
    if (dummy_mode)
	return GALERA_OK;
    
    assert(galera_ctx);
    return galera_ctx->set_database(galera_ctx, conn_id, query, query_len);
}

enum galera_status galera_to_execute_start(
    conn_id_t conn_id, char *query, uint16_t query_len
)
{
    if (dummy_mode)
	return GALERA_OK;
    
    assert(galera_ctx);
    return galera_ctx->to_execute_start(galera_ctx, conn_id, query, query_len);
}

enum galera_status galera_to_execute_end(conn_id_t conn_id)
{
    if (dummy_mode)
	return GALERA_OK;
    
    assert(galera_ctx);
    return galera_ctx->to_execute_end(galera_ctx, conn_id);
}



/**************************************************************************
 * Library loader
 **************************************************************************/

static int verify(const galera_t *gh, const char *iface_ver)
{
#define VERIFY(_p) if (!(_p)) {					\
	fprintf(stderr, "galera_load(): verify(): %s\n", # _p);	\
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


static galera_loader_fun galera_dlf(void *dlh, const char *sym)
{
    union {
	galera_loader_fun dlfun;
	void *obj;
    } alias;
    alias.obj = dlsym(dlh, sym);
    return alias.dlfun;
}

int galera_load(const char *spec, galera_t **hptr)
{
    int ret = 0;
    void *dlh = NULL;
    galera_loader_fun dlfun;

    
    if (!spec)
        return EINVAL;

    fprintf(stderr, "galera_load(): loading %s\n", spec);

    if (strcmp(spec, "dummy") == 0) {
	dummy_mode = 1;
	return 0;
    }

    if (!hptr) {
        if (galera_ctx)
            return EBUSY;
        hptr = &galera_ctx;
    } else {
        *hptr = NULL;
    }
    
    if (!(dlh = dlopen(spec, RTLD_NOW | RTLD_LOCAL))) {
	fprintf(stderr, "galera_load(): dlopen(): %s\n", dlerror());
        ret = EINVAL;
        goto out;
    }

    if (!(dlfun = galera_dlf(dlh, "galera_loader"))) {
	fprintf(stderr, "galera_load(): dlopen(): %s\n", dlerror());
        ret = EINVAL;
        goto out;
        
    }

    ret = (*dlfun)(hptr);
    
    if (ret == 0 && !*hptr) {
	fprintf(stderr, "galera_load(): loader failed\n");
        ret = EACCES;
    }
    if (ret == 0 && 
        (ret = verify(*hptr, GALERA_INTERFACE_VERSION)) != 0 &&
        (*hptr)->tear_down) {
	fprintf(stderr, "galera_load(): interface version mismatch\n");
        (*hptr)->tear_down(*hptr);
    }
    if (ret == 0)
        (*hptr)->dlh = dlh;
out:
    if (ret != 0 && dlh)
        dlclose(dlh);
    if (ret == 0) {
      fprintf(stderr, "galera_load(): driver loaded succesfully\n");
    }
    return ret;
}



void galera_unload(galera_t *hptr)
{
    void *dlh;

    if (dummy_mode) {
	dummy_mode = 0;
	return;
    }
    
    if (!hptr) {
        hptr = galera_ctx;
        galera_ctx = NULL;
    }
    assert(hptr);
    dlh = hptr->dlh;
    if (dlh)
        dlclose(dlh);
}

