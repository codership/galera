// Copyright (C) 2007 Codership Oy <info@codership.com>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <rpc/types.h>
#include <rpc/xdr.h>

#include "wsdb_priv.h"
#include "wsdb_xdr.h"

#undef gu_malloc
#undef gu_free

#define gu_malloc malloc
#define gu_free free

/* xdr conversion for wsdb_key  */
bool_t xdr_wsdb_key_part(XDR *xdrs, void *ptr, ...) {
    struct wsdb_key_part *part = ptr;
    if (!xdr_char(xdrs, &part->type))                        return FALSE;
    if (!xdr_u_short(xdrs, &part->length))                   return FALSE;

    if (xdrs->x_op == XDR_DECODE) {
        part->data = gu_malloc (part->length);
        memset(part->data, '\0', part->length);
    }
    if (!xdr_opaque(xdrs, part->data, (u_int)part->length)) return FALSE;
    if (xdrs->x_op == XDR_FREE) gu_free (part->data);
#ifdef REMOVED
    if (!xdr_bytes(
        xdrs, (char **)&part->data, &(u_int)part->length, USHRT_MAX
    )) return FALSE;
#endif
    return TRUE;
}

bool_t xdr_wsdb_table_key(XDR *xdrs, void *ptr, ...) {
    struct wsdb_table_key *table_key = ptr;
#ifndef REMOVED
    size_t i;
    if (!xdr_u_short(xdrs, &table_key->key_part_count)) return FALSE;

    if (xdrs->x_op == XDR_DECODE) {
        size_t len = table_key->key_part_count * sizeof(struct wsdb_key_part);
        table_key->key_parts = (struct wsdb_key_part*) gu_malloc (len);
        memset(table_key->key_parts, '\0', len);
    }
    for (i=0; i<table_key->key_part_count; i++) {
        if (!xdr_reference(xdrs, (char**)&(table_key->key_parts), 
             sizeof(struct wsdb_key_part), xdr_wsdb_key_part
        )) return FALSE;
    }
    if (xdrs->x_op == XDR_FREE) {
        gu_free(table_key->key_parts);
    }
#else
    if (!xdr_array(
            xdrs, 
            (char **)&table_key->key_parts, (u_int*)&table_key->key_part_count,
            USHRT_MAX, sizeof(struct wsdb_key_part), xdr_wsdb_key_part
    )) return FALSE;
#endif
    return TRUE;
}

bool_t xdr_wsdb_key(XDR *xdrs, void *ptr, ...) {
    struct wsdb_key_rec *key = ptr;
    //if (!xdr_string(xdrs, &key->dbtable, 256))  return FALSE;
    //if (!xdr_bytes(
    //        xdrs, (char **)&(key->dbtable), (u_int *)&(key->dbtable_len), 256)
    //)  return FALSE;
    if (!xdr_u_short(xdrs, &key->dbtable_len)) return FALSE;
    if (xdrs->x_op == XDR_DECODE) {
        key->dbtable = (char *) gu_malloc (key->dbtable_len);
        memset(key->dbtable, '\0', key->dbtable_len);
    }
    if (!xdr_opaque(xdrs, key->dbtable, (u_int)key->dbtable_len)) return FALSE;
    if (xdrs->x_op == XDR_FREE) gu_free(key->dbtable);

    if (!xdr_reference(xdrs, (char **)&key->key, 
         sizeof(struct wsdb_table_key), xdr_wsdb_table_key
    ))    return FALSE;
    return TRUE;
}

bool_t xdr_wsdb_col_data_rec(XDR *xdrs, struct wsdb_col_data_rec *rec) {
    if (!xdr_u_short(xdrs, &rec->column)) return FALSE;
    if (!xdr_char(xdrs, &rec->data_type)) return FALSE;
    if (!xdr_bytes(
            xdrs, (char **)&rec->data, 
            (u_int *)&rec->length, USHRT_MAX)
    ) {
        return FALSE;
    }
    return TRUE;
}

//bool_t xdr_wsdb_cols_data_rec (XDR *xdrs, struct wsdb_cols_data_rec *rec, ...) {
bool_t xdr_wsdb_cols_data_rec (XDR *xdrs, void *ptr, ...) {
    struct wsdb_cols_data_rec *rec = (struct wsdb_cols_data_rec *)ptr;
    int i;
    if (!xdr_u_short(xdrs, &rec->col_count)) return FALSE;
    for (i=0; i<rec->col_count; i++) {
        if (!xdr_wsdb_col_data_rec(xdrs, &rec->data[i])) return FALSE;
    }
    return TRUE;
}

//bool_t xdr_wsdb_row_data_rec (XDR *xdrs, struct wsdb_row_data_rec *rec, ...) {
bool_t xdr_wsdb_row_data_rec (XDR *xdrs, void *ptr, ...) {
    struct wsdb_row_data_rec *rec = (struct wsdb_row_data_rec *)ptr;
    if (!xdr_bytes(
            xdrs, (char **)&rec->data, 
            (u_int *)&rec->length, USHRT_MAX)
    ) {
        return FALSE;
    }
    return TRUE;
}

struct xdr_discrim item_data_arms[3] = {
    {COLUMN, xdr_wsdb_cols_data_rec},
    {ROW, xdr_wsdb_row_data_rec},
    {0, NULL}
};

bool_t xdr_wsdb_item_rec(XDR *xdrs, void *ptr, ...) {
    struct wsdb_item_rec *item = ptr;
    if (!xdr_char(xdrs, &item->action)) return FALSE;
    if (!xdr_pointer(
            xdrs, (char **)&item->key,
            sizeof(struct wsdb_key_rec), xdr_wsdb_key)
    ) return FALSE;
    if (xdr_union(xdrs, (enum_t *)&item->data_mode, (char *)&item->u, item_data_arms, NULL)) 
        return FALSE;

    return TRUE;
}

bool_t xdr_wsdb_item_rec_ref(XDR *xdrs, struct wsdb_item_rec* item) {
    /* Intermediate pointer is needed to avoid a warning about 
     * dereferencing type-punned pointer breaking aliasing rules.
     * Where is dereferencing??? */
    char* pp = (void*) item;
    if (!xdr_reference(xdrs, &pp,
        sizeof(struct wsdb_item_rec), xdr_wsdb_item_rec)) return FALSE;
    return TRUE;
}

bool_t xdr_wsdb_query(XDR *xdrs, struct wsdb_query *q) {
#ifndef REMOVED
    if (!xdr_u_int(xdrs, &q->query_len))                 return FALSE;
    if (xdrs->x_op == XDR_DECODE) {
        q->query = (char *) gu_malloc (q->query_len);
        memset(q->query, '\0', q->query_len);
    }
    if (!xdr_opaque(xdrs, q->query, (u_int)(q->query_len))) return FALSE;
    if (xdrs->x_op == XDR_FREE) {
        gu_free(q->query);
    }
    if (!xdr_long(xdrs, &q->timeval))                      return FALSE;
    if (!xdr_u_int(xdrs, &q->randseed))                   return FALSE;
#else
    if (!xdr_bytes(xdrs, (char **)&q->query, 
        (uint*)&q->query_len, USHRT_MAX))        return FALSE;
#endif
    return TRUE;
}

/* xdr conversion for wsdb_write_set */
bool_t xdr_wsdb_write_set(XDR *xdrs, struct wsdb_write_set *ws) {
    uint32_t i;
    if (!xdr_longlong_t(xdrs, &ws->local_trx_id))          return FALSE;
    if (!xdr_longlong_t(xdrs, &ws->last_seen_trx))         return FALSE;
    if (!xdr_enum(xdrs, (enum_t *)&ws->type))              return FALSE;
    if (!xdr_enum(xdrs, (enum_t *)&ws->level))             return FALSE;

#ifdef  REMOVED
    if (!xdr_array(
        xdrs, (char **)&ws->queries, (uint*)&ws->query_count,
        USHRT_MAX, sizeof(struct wsdb_query), xdr_wsdb_query
    )) return FALSE;
#else
    if (!xdr_u_short(xdrs, &ws->query_count)) return FALSE;

    if (xdrs->x_op == XDR_DECODE) {
        size_t len = ws->query_count * sizeof(struct wsdb_query);
        if (len) {
            ws->queries = (struct wsdb_query*) gu_malloc (len);
            memset(ws->queries, '\0', len);
        } else {
            ws->queries = NULL;
        }
    }
    for (i=0; i<ws->query_count; i++) {
        if (!xdr_wsdb_query(xdrs, &ws->queries[i])) return FALSE;
    }
    if (xdrs->x_op == XDR_FREE) {
        gu_free(ws->queries);
    }
#endif

    /* connection query array */
    if (!xdr_u_short(xdrs, &ws->conn_query_count)) return FALSE;

    if (xdrs->x_op == XDR_DECODE) {
        size_t len = ws->conn_query_count * sizeof(struct wsdb_query);
        ws->conn_queries = (struct wsdb_query*) gu_malloc (len);
        memset(ws->conn_queries, '\0', len);
    }
    for (i=0; i<ws->conn_query_count; i++) {
        if (!xdr_wsdb_query(xdrs, &ws->conn_queries[i])) return FALSE;
    }
    if (xdrs->x_op == XDR_FREE) {
        gu_free(ws->conn_queries);
    }

#ifdef REMOVED
    if (!xdr_array(
        xdrs, (char **)&ws->items, (uint*)&ws->item_count, USHRT_MAX,
        sizeof(struct wsdb_item_rec), xdr_wsdb_item_rec_ref
    ))                                                     return FALSE;
#else
    if (!xdr_u_int(xdrs, &ws->item_count))               return FALSE;
    if (xdrs->x_op == XDR_DECODE) {
        size_t len = ws->item_count * sizeof(struct wsdb_item_rec);
        ws->items = (struct wsdb_item_rec*) gu_malloc (len);
        memset(ws->items, '\0', len);
    }
    for (i=0; i<ws->item_count; i++) {
        if (!xdr_wsdb_item_rec(xdrs, &ws->items[i]))      return FALSE;
    }
    if (xdrs->x_op == XDR_FREE) {
        gu_free(ws->items);
    }

    if (xdrs->x_op == XDR_DECODE) {
        ws->key_composition = NULL;
    }

    if (!xdr_u_int(xdrs, &ws->rbr_buf_len)) return FALSE;
    if (xdrs->x_op == XDR_DECODE) {
         if (ws->rbr_buf_len != 0) {
              ws->rbr_buf = (char *) gu_malloc (ws->rbr_buf_len);
              memset(ws->rbr_buf, '\0', ws->rbr_buf_len);
         }
    }
    if (ws->rbr_buf_len)
    {
         if (!xdr_opaque(xdrs, ws->rbr_buf, (u_int) ws->rbr_buf_len)) return FALSE;
    }
    else
    {
         assert(ws->level == WSDB_WS_QUERY); //TODO -> to exact RBR
    }
    if (xdrs->x_op == XDR_FREE) {
         if (ws->rbr_buf_len) {
              gu_free(ws->rbr_buf);
              ws->rbr_buf_len = 0;
         }
    }
#endif
    return TRUE;
}

int xdr_estimate_wsdb_size(struct wsdb_write_set *ws) {
    uint32_t i;
    int ws_size = 0;
    ws_size += sizeof(ws->local_trx_id);
    ws_size += sizeof(ws->last_seen_trx);
    ws_size += sizeof(ws->type);
    ws_size += sizeof(ws->level);
    //ws_size += sizeof(ws->state);
    ws_size += sizeof(ws->query_count);

    for (i=0; i<ws->query_count; i++) {
        ws_size += sizeof(ws->queries[i].query_len);
        ws_size += ws->queries[i].query_len;
        ws_size += sizeof(ws->queries[i].timeval);
        ws_size += sizeof(ws->queries[i].randseed);
    }
    ws_size += sizeof(ws->conn_query_count);
    for (i=0; i<ws->conn_query_count; i++) {
        ws_size += sizeof(ws->conn_queries[i].query_len);
        ws_size += ws->conn_queries[i].query_len;
        ws_size += sizeof(ws->conn_queries[i].timeval);
        ws_size += sizeof(ws->conn_queries[i].randseed);
    }

    ws_size += sizeof(ws->item_count);
    for (i=0; i<ws->item_count; i++) {
        int j;
        ws_size += sizeof(ws->items[i].action);
        ws_size += sizeof(ws->items[i].key->dbtable_len);
        ws_size += ws->items[i].key->dbtable_len;
        ws_size += sizeof(ws->items[i].key->key->key_part_count);
        for ( j=0; j < ws->items[i].key->key->key_part_count; j++) {
            ws_size += sizeof(ws->items[i].key->key->key_parts[j].type);
            ws_size += sizeof(ws->items[i].key->key->key_parts[j].length);
            ws_size += ws->items[i].key->key->key_parts[j].length;
        }
    }
    return(ws_size);
}
