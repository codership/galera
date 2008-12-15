// Copyright (C) 2007 Codership Oy <info@codership.com>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "wsdb_priv.h"
#include "key_array.h"

struct conn_info {
    char             ident;
    struct wsdb_conn_info info;

    struct key_array variables;

    /* SQL statement to set the default database */
    char   *set_default_db;
};
#define IDENT_conn_info 'C'

static struct wsdb_hash  *conn_hash;
static uint16_t conn_limit;

static uint32_t hash_fun_64(uint32_t max_size, uint16_t len, char *key) {
    return (*(uint64_t *)key) % conn_limit;
}

static int hash_cmp_64(uint16_t len1, char *key1, uint16_t len2, char *key2) {
    if (*(uint64_t *)key1 < *(uint64_t *)key2) return -1;
    if (*(uint64_t *)key1 > *(uint64_t *)key2) return 1;
    return 0;
}

int conn_init(uint16_t limit) {
    conn_limit = (limit) ? limit : CONN_LIMIT;
    conn_hash  = wsdb_hash_open(conn_limit, hash_fun_64, hash_cmp_64, true);
    return 0;
}

static int cleanup_verdict(void *ctx, void *data, void **new_data)
{
    gu_free(data);
    *new_data = (void*) NULL;
    return 1;
}

void conn_close()
{
    wsdb_hash_delete_range(conn_hash, NULL, &cleanup_verdict);
    wsdb_hash_close(conn_hash);
}

void conn_remove_conn(connid_t conn_id) {
    struct conn_info *conn;

    GU_DBUG_PRINT("wsdb",("deleting connection: %lu", conn_id));

    /* get the transaction info from local hash */
    conn = (struct conn_info *)wsdb_hash_delete(
        conn_hash, sizeof(connid_t), (char *)&conn_id
    );
    if (conn) {
        CHECK_OBJ(conn, conn_info);
        gu_free(conn);
    } else {
        gu_error("Trying delete non existing conn: %lu", conn_id);
    }
    return;
}

static struct conn_info *get_conn_info(
    connid_t conn_id
) {
    struct conn_info *conn;

    /* get the connection info from local hash */
    conn = (struct conn_info *)wsdb_hash_search(
        conn_hash, sizeof(connid_t), (char *)&conn_id
    );
    if (conn) {
        GU_DBUG_PRINT("wsdb", 
           ("found conn: %lu == %lu", conn_id, conn->info.id)
        );
        CHECK_OBJ(conn, conn_info);
    } else {
        GU_DBUG_PRINT("wsdb", ("conn does not exist: %lu", conn_id));
    }

    return conn;
}

static struct conn_info *new_conn_info(
    connid_t conn_id
) {
    struct conn_info *conn;
          
    /* get the block for connection */
    conn = (struct conn_info *)wsdb_hash_search(
        conn_hash, sizeof(connid_t), (char *)&conn_id
    );
    if (conn) {
        gu_error("connection exist already: %lu", conn_id);
        return NULL;
    }

    MAKE_OBJ(conn, conn_info);
    conn->info.id        = conn_id;
    conn->info.state     = WSDB_CONN_IDLE;
    conn->info.seqno     = LLONG_MAX;
    conn->set_default_db = NULL;
    key_array_open(&conn->variables);

    GU_DBUG_PRINT("wsdb", ("created new connection: %lu", conn_id));

    wsdb_hash_push(
        conn_hash, sizeof(connid_t), (char *)&conn_id, conn
    );

    return conn;
}

int wsdb_store_set_variable(
    connid_t conn_id, char *key, uint16_t key_len,
    char *data, uint16_t data_len
) {
    struct conn_info *conn = get_conn_info(conn_id);
    GU_DBUG_ENTER("wsdb_store_set_variable");
    if (!conn) {
        conn = new_conn_info(conn_id);
    }
    GU_DBUG_PRINT("wsdb",("set var for conn: %lu : %s", conn_id, data));

    key_array_replace(&conn->variables, key, key_len, data, data_len);

    GU_DBUG_RETURN(WSDB_OK);
}

int wsdb_store_set_database(
    connid_t conn_id, char *set_db, uint16_t set_db_len
) {
    struct conn_info *conn = get_conn_info(conn_id);
    GU_DBUG_ENTER("wsdb_store_set_database");
    if (!conn) {
        conn = new_conn_info(conn_id);
    }
    GU_DBUG_PRINT("wsdb",("set db for conn: %lu : %s", conn_id, set_db));

    if (conn->set_default_db) {
        gu_free(conn->set_default_db);
    }
    conn->set_default_db = (char *) gu_malloc (strlen(set_db) + 1);
    strcpy(conn->set_default_db, set_db);
    conn->set_default_db[strlen(set_db)] = '\0';

    GU_DBUG_RETURN(WSDB_OK);
}

struct variable_ctx {
    struct wsdb_write_set *ws;
    uint16_t               query_count;
};

static int handle_variable_elem(void *context, char *key, char *data) {
    struct variable_ctx *ctx = (struct variable_ctx *)context;

    ctx->ws->conn_queries[ctx->query_count].query_len = strlen(data);
    ctx->ws->conn_queries[ctx->query_count].query = (char *) gu_malloc (
        strlen(data) + 1
    );
    strcpy(ctx->ws->conn_queries[ctx->query_count].query, data);
    ctx->ws->conn_queries[ctx->query_count].query[strlen(data)] = '\0';
    ctx->query_count++;
    return 0;
}

int conn_build_connection_queries(
    struct wsdb_write_set *ws, connid_t conn_id
) {
    struct variable_ctx ctx;

    struct conn_info *conn = get_conn_info(conn_id);
    GU_DBUG_ENTER("conn_build_connection_queries");

    ws->conn_query_count = 0;
    ws->conn_queries     = NULL;

    if (!conn) {
        conn = new_conn_info(conn_id);
        GU_DBUG_RETURN(WSDB_ERR_CONN_UNKNOWN);
    }
    GU_DBUG_PRINT("wsdb",("build conn: %lu", conn_id));

    if (conn->set_default_db) {
        ws->conn_query_count++;
    }
    ws->conn_query_count += key_array_get_size(&conn->variables);

    GU_DBUG_PRINT("wsdb",("conn query count: %d", ws->conn_query_count));
    ws->conn_queries = (struct wsdb_query *) gu_malloc (
        ws->conn_query_count * sizeof(struct wsdb_query)
    );
    memset(
        ws->conn_queries, '\0', ws->conn_query_count*sizeof(struct wsdb_query)
    );

    /* copy the USE command */
    if (conn->set_default_db) {
        ws->conn_queries[0].query_len = strlen(conn->set_default_db);
        ws->conn_queries[0].query = (char *) gu_malloc (
            strlen(conn->set_default_db) + 1
        );
        ws->conn_queries[0].query[strlen(conn->set_default_db)] = '\0';
        strcpy(ws->conn_queries[0].query, conn->set_default_db);
        ctx.query_count = 1;
    } else {
        ctx.query_count = 0;
    }

    /* copy all the SET variable commands */
    ctx.ws = ws;
    if (key_array_scan_entries(&conn->variables, handle_variable_elem, &ctx)) {
        GU_DBUG_RETURN(WSDB_ERR_CONN_FAIL);
    }
    GU_DBUG_RETURN(WSDB_OK);
}

int wsdb_conn_set_seqno(
    connid_t conn_id, trx_seqno_t seqno
) {
    struct conn_info *conn = get_conn_info(conn_id);
    GU_DBUG_ENTER("conn_set_seqno");
    if (!conn) {
        gu_error("no connection in conn_set_seqno",conn_id);
        GU_DBUG_RETURN(WSDB_ERR_CONN_UNKNOWN);
    }
    GU_DBUG_PRINT("wsdb",("set seqno for conn: %lu : %lu", conn_id, seqno));

    conn->info.seqno   = seqno;
    conn->info.state   = WSDB_CONN_TRX;

    GU_DBUG_RETURN(WSDB_OK);
}

int wsdb_conn_reset_seqno( connid_t conn_id ) {
    struct conn_info *conn = get_conn_info(conn_id);
    GU_DBUG_ENTER("conn_set_seqno");
    if (!conn) {
        gu_error("no connection in conn_set_seqno",conn_id);
        GU_DBUG_RETURN(WSDB_ERR_CONN_UNKNOWN);
    }
    GU_DBUG_PRINT("wsdb",("reset seqno for conn: %lu", conn_id));

    conn->info.state   = WSDB_CONN_IDLE;

    GU_DBUG_RETURN(WSDB_OK);
}

int wsdb_conn_get_info(
    connid_t conn_id, 
    struct wsdb_conn_info *info
) {
    struct conn_info *conn = get_conn_info(conn_id);
    GU_DBUG_ENTER("conn_get_seqno");
    if (!conn) {
        gu_error("no connection in conn_get_seqno",conn_id);
        return WSDB_ERR_CONN_UNKNOWN;
    }
    GU_DBUG_PRINT("wsdb",("get seqno for conn: %lld : %lu", 
        conn_id, conn->info.seqno
    ));
    info->id    = conn->info.id;
    info->seqno = conn->info.seqno;
    info->state = conn->info.state;

    return WSDB_OK;
}
