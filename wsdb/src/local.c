// Copyright (C) 2007 Codership Oy <info@codership.com>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "wsdb_priv.h"
#include "file_cache.h"
#include "hash.h"
#include "file.h"

/* static variables */
static struct file_cache *local_cache;
static struct wsdb_file  *local_file;
static struct wsdb_hash  *trx_hash;
static uint16_t trx_limit;

enum wsdb_block_states {
    BLOCK_ACTIVE,
    BLOCK_FULL,
    BLOCK_FREE,
};

/* header for each cache block */
struct block_hdr {
    local_trxid_t          trx_id;
    enum wsdb_block_states state;
    uint16_t               pos;
    cache_id_t             next_block;
};

/* blocks for a transaction */
struct trx_info {
    char          ident;
    local_trxid_t id;
    trx_seqno_t   seqno_g;
    trx_seqno_t   seqno_l;
    cache_id_t    first_block;
    cache_id_t    last_block;
};
#define IDENT_trx_info 'X'

/* seqno of last committed transaction  */
trx_seqno_t last_committed_trx;
//struct gu_mutex last_committed_trx_mtx; // mutex protecting last_commit...
gu_mutex_t last_committed_trx_mtx; // mutex protecting last_commit...

static void set_last_committed_trx(trx_seqno_t seqno) {
    gu_mutex_lock(&last_committed_trx_mtx);
    last_committed_trx= seqno;
    gu_mutex_unlock(&last_committed_trx_mtx);
}
static trx_seqno_t get_last_committed_trx() {
    trx_seqno_t tmp;
    gu_mutex_lock(&last_committed_trx_mtx);
    tmp= last_committed_trx;
    gu_mutex_unlock(&last_committed_trx_mtx);
    return tmp;
}

/* not used
static uint32_t hash_fun_16(uint32_t max_size, uint16_t len, char *key) {
    return (*(uint16_t *)key) % trx_limit;
}

static int hash_cmp_16(uint16_t len1, char *key1, uint16_t len2, char *key2) {
    if ((uint16_t)(*key1) < (uint16_t)(*key2)) return -1;
    if ((uint16_t)(*key1) > (uint16_t)(*key2)) return 1;
    return 0;
}
*/

static uint32_t hash_fun_64(uint32_t max_size, uint16_t len, char *key) {
    return (*(uint64_t *)key) % trx_limit;
}

static int hash_cmp_64(uint16_t len1, char *key1, uint16_t len2, char *key2) {
    if (*(uint64_t *)key1 < *(uint64_t *)key2) return -1;
    if (*(uint64_t *)key1 > *(uint64_t *)key2) return 1;
    return 0;
}

/*
 * opens local state trx database
 */
int local_open(
    const char *dir,
    const char *file,
    uint16_t    block_size,
    uint16_t    limit
) {
    char full_name[256];
    int rcode;

    memset(full_name, 256, '\0');
    sprintf(
        full_name, "%s%s%s", (dir)  ? dir  : DEFAULT_WORK_DIR, 
        PATH_SEPARATOR,
        (file) ? file : DEFAULT_LOCAL_FILE
    );
    local_file = file_create(
        full_name, (block_size) ? block_size : DEFAULT_BLOCK_SIZE
    );

    /* assume 5000 trx write blocks */
    local_cache = file_cache_open(local_file, LOCAL_CACHE_LIMIT, 5000);
    trx_limit = (limit) ? limit : TRX_LIMIT;
    trx_hash  = wsdb_hash_open(trx_limit, hash_fun_64, hash_cmp_64);

    rcode = conn_init(0);

    /* initialize last_committed_trx mutex */
    gu_mutex_init(&last_committed_trx_mtx, NULL);

    return WSDB_OK;
}

static void remove_trx_info(local_trxid_t trx_id) {
    struct trx_info *trx;

    GU_DBUG_PRINT("wsdb",("deleting trx: %llu", (unsigned long long)trx_id));
    /* get the transaction info from local hash */
    trx = (struct trx_info *)wsdb_hash_delete(
        trx_hash, sizeof(local_trxid_t), (char *)&trx_id
    );
    if (trx) {
        CHECK_OBJ(trx, trx_info);
        gu_free(trx);
    } else {
        gu_error("Trying delete non existing trx: %llu",trx_id);
    }
    return;
}

static struct trx_info *get_trx_info(local_trxid_t trx_id) {
    struct trx_info *trx;

    /* get the transaction info from local hash */
    trx = (struct trx_info *)wsdb_hash_search(
        trx_hash, sizeof(local_trxid_t), (char *)&trx_id
    );
    if (trx) {
        GU_DBUG_PRINT("wsdb", 
           ("found trx: %llu == %llu blocks: %d -> %d", 
            trx_id, trx->id, trx->first_block, trx->last_block)
        );
        CHECK_OBJ(trx, trx_info);
    } else {
        GU_DBUG_PRINT("wsdb", ("trx does not exist: %llu", trx_id));
    }

    return trx;
}

static void new_trx_block(struct trx_info *trx) {
    GU_DBUG_ENTER("new_trx_block");

    cache_id_t new_block_id = file_cache_allocate_id(local_cache);
    struct block_hdr *last_block = (trx->last_block) ? 
        (struct block_hdr *)file_cache_get(local_cache, trx->last_block) : 
        NULL;
    struct block_hdr *new_block = 
        (struct block_hdr *)file_cache_new(local_cache, new_block_id);

    new_block->trx_id     = trx->id;
    new_block->state      = BLOCK_ACTIVE;
    new_block->pos        = sizeof(struct block_hdr);
    new_block->next_block = 0;

    GU_DBUG_PRINT("wsdb",("new trx block: %d", new_block_id));

    file_cache_forget(local_cache, new_block_id);

    if (last_block) {
        last_block->state      = BLOCK_FULL;
        last_block->next_block = new_block_id;
        file_cache_forget(local_cache, trx->last_block);
    }
    if (!trx->first_block) {
        trx->first_block = new_block_id;
    }
    trx->last_block = new_block_id;
    GU_DBUG_VOID_RETURN;
}

static struct trx_info *new_trx_info(local_trxid_t trx_id) {
    struct trx_info *trx;
          
    /* get the block for transaction */
    trx = (struct trx_info *)wsdb_hash_search(
        trx_hash, sizeof(local_trxid_t), (char *)&trx_id
    );
    if (trx) {
        gu_error("trx exist already: %llu", trx_id);
        return NULL;
    }

    MAKE_OBJ(trx, trx_info);
    trx->id          = trx_id;
    trx->seqno_g     = 0;
    trx->seqno_l     = 0;
    trx->first_block = 0;
    trx->last_block  = 0;

    GU_DBUG_PRINT("wsdb", ("created new trx: %llu", trx_id));

    wsdb_hash_push(
        trx_hash, sizeof(local_trxid_t), (char *)&trx_id, trx
    );

    /* create first block for the trx */
    new_trx_block(trx);
    
    return trx;
}

/* associates block pointer and cache ID */
struct block_info {
    struct block_hdr *block;
    cache_id_t        cache_id;
};

static void open_trx_block_access(
    struct trx_info *trx, struct block_info *bi
) {
    GU_DBUG_ENTER("open_trx_block_access");

    /* activate the block in cache */
    bi->cache_id = trx->last_block;
    bi->block = (struct block_hdr *)file_cache_get(local_cache, bi->cache_id);

    GU_DBUG_VOID_RETURN;
}

static void close_trx_block_access(
    struct trx_info *trx, struct block_info *bi
) {
    GU_DBUG_ENTER("close_trx_block_access");
    file_cache_forget(local_cache, bi->cache_id);
    GU_DBUG_VOID_RETURN;
}

static void append_in_trx_block(
    struct trx_info *trx, struct block_info *bi, uint16_t len, char *data
) {
    GU_DBUG_ENTER("append_in_trx_block");

    while (len) {
        uint16_t stored;
        char *ptr;

        //GU_DBUG_PRINT("wsdb", ("block: %d, len: %d",trx->last_block, len));
        stored = (len > local_cache->block_size - bi->block->pos) ? 
            local_cache->block_size - bi->block->pos : len;

        assert(bi->block->pos + stored <= local_cache->block_size);

        ptr = (char *)(bi->block) + bi->block->pos;
        if (stored > 0) {
            memcpy(ptr, data, stored);
            len        -= stored;
            bi->block->pos += stored;
        }

        if (len) {
            /* block full, need to get new */
            GU_DBUG_PRINT("wsdb", ("append trx block, len: %d %d ",len, stored));

            /* release previous block available for swapping */
            file_cache_forget(local_cache, bi->cache_id);

            /* create new block for trx */
            new_trx_block(trx);
            open_trx_block_access(trx, bi);

            data += stored;
            GU_DBUG_PRINT("wsdb", 
                       ("new block id: %d=%d", trx->last_block, bi->cache_id)
            );
        }
    }
    GU_DBUG_VOID_RETURN;
}

int wsdb_append_query(
    local_trxid_t trx_id, char *query, time_t timeval, uint32_t randseed
) {
    struct trx_info  *trx = get_trx_info(trx_id);
    char              rec_type;
    uint16_t          query_len = strlen(query) + 1;
    struct block_info bi;

    GU_DBUG_ENTER("wsdb_append_query");
    if (!trx) {
        trx = new_trx_info(trx_id);
    }
    GU_DBUG_PRINT("wsdb",("query for trx: %llu : %s", trx_id, query));

    if (!query) {
        GU_DBUG_RETURN(WSDB_ERR_BAD_QUERY);
    }

    open_trx_block_access(trx, &bi);
    rec_type = REC_TYPE_QUERY;
    append_in_trx_block(trx, &bi, (uint16_t)1, &rec_type);
    append_in_trx_block(trx, &bi, (uint16_t)2, (char *)&query_len);
    append_in_trx_block(trx, &bi, query_len-1, query);
    append_in_trx_block(trx, &bi, 1, "\0");
    append_in_trx_block(trx, &bi, (uint16_t)4, (char *)&timeval);
    append_in_trx_block(trx, &bi, (uint16_t)4, (char *)&randseed);
    close_trx_block_access(trx, &bi);
    GU_DBUG_RETURN(WSDB_OK);
}

int wsdb_append_row_key(
    local_trxid_t trx_id, struct wsdb_key_rec *key, char action
) {
    struct trx_info       *trx = get_trx_info(trx_id);
// @unused:   struct file_row_key   *row_key;
    char                   rec_type;
    uint16_t               rec_len;
    struct block_info      bi;
    struct wsdb_table_key *tkey;
    uint16_t               i;

    GU_DBUG_ENTER("wsdb_append_row_key");
    if (!key) {
        GU_DBUG_PRINT("wsdb",("no key for trx: %llu", trx_id));
        GU_DBUG_RETURN(WSDB_ERR_NO_KEY);
    }
    if ((action != WSDB_ACTION_DELETE) &&
        (action != WSDB_ACTION_INSERT) &&
        (action != WSDB_ACTION_UPDATE)
    ) {
        gu_error("bad action:%d, trx:%llu",action, trx_id);
        GU_DBUG_RETURN(WSDB_ERR_BAD_ACTION);
    }
    if (!trx) {
        trx = new_trx_info(trx_id);
    }

    /* first put the action code */
    open_trx_block_access(trx, &bi);

    rec_type = REC_TYPE_ACTION;
    append_in_trx_block(trx, &bi, (uint16_t)1, &rec_type);

    rec_len = 1;
    append_in_trx_block(trx, &bi, (uint16_t)2, (char *)&rec_len);
    append_in_trx_block(trx, &bi, rec_len, (char *)&action);

    rec_type = REC_TYPE_ROW_KEY;
    append_in_trx_block(trx, &bi, (uint16_t)1, &rec_type);
    
    /* db and table len & data */
    append_in_trx_block(trx, &bi, (uint16_t)2, (char *)&key->dbtable_len);
    append_in_trx_block(trx, &bi, key->dbtable_len, key->dbtable);
    
    /* number of key parts */
    tkey = key->key;
    append_in_trx_block(trx, &bi, (uint16_t)2, (char *)&tkey->key_part_count);

    /* each key part */
    for (i=0; i<tkey->key_part_count; i++) {
        struct wsdb_key_part *kp = &tkey->key_parts[i];

        append_in_trx_block(trx, &bi, (uint16_t)1, (char *)&kp->type);
        append_in_trx_block(trx, &bi, (uint16_t)2, (char *)&kp->length);
        append_in_trx_block(trx, &bi, kp->length,  (char *)kp->data);
    }
#ifdef REMOVED
    /* then the key specification */
    row_key = wsdb_key_2_file_row_key(key);

    rec_type = REC_TYPE_ROW_KEY;
    append_in_trx_block(trx, &bi, (uint16_t)1, &rec_type);

    rec_len = row_key->key_len + sizeof(struct file_row_key);
    append_in_trx_block(trx, &bi, (uint16_t)2, (char *)&rec_len);
    append_in_trx_block(trx, &bi, rec_len, (char *)row_key);
    
    GU_DBUG_PRINT("wsdb",
               ("append key: %p, len: %d", row_key->key, row_key->key_len)
    );
    gu_free(row_key);
#endif
    close_trx_block_access(trx, &bi);
    GU_DBUG_RETURN(WSDB_OK);
}

int wsdb_append_row(
    local_trxid_t trx_id, uint16_t len, void *data
) {
    struct trx_info      *trx = get_trx_info(trx_id);
    struct wsdb_row_data_rec  row_data;
    char                  rec_type;
    struct block_info     bi;

    if (!trx) {
        trx = new_trx_info(trx_id);
    }

    row_data.length    = len;
          
    open_trx_block_access(trx, &bi);

    rec_type = REC_TYPE_ROW_DATA;
    append_in_trx_block(trx, &bi, (uint16_t)1, &rec_type);

#ifdef REMOVED
    len = len + sizeof(struct wsdb_row_data_rec);
    append_in_trx_block(trx, &bi, (uint16_t)2, (char *)&len);
    append_in_trx_block(
        trx, &bi, sizeof(struct wsdb_row_data_rec), (char *)&row_data
    );
    append_in_trx_block(trx, &bi, len, (char *)&data);
#endif
    append_in_trx_block(trx, &bi, (uint16_t)2, (char *)&len);
    append_in_trx_block(trx, &bi, len, (char *)&data);

    close_trx_block_access(trx, &bi);
    
    return WSDB_OK;
}

int wsdb_append_row_col(
    local_trxid_t trx_id, char *dbtable, uint16_t dbtable_len,
    uint16_t col, char data_type, uint16_t len, void *data
) {
    struct trx_info      *trx = get_trx_info(trx_id);
    struct wsdb_col_data_rec  row_data;
    char                  rec_type;
    struct block_info     bi;

    if (!trx) {
        trx = new_trx_info(trx_id);
    }

    row_data.column    = col;
    row_data.data_type = data_type;
    row_data.length    = len;
          
    open_trx_block_access(trx, &bi);

    rec_type = REC_TYPE_ROW_DATA;
    append_in_trx_block(trx, &bi, (uint16_t)1, &rec_type);

    len = len + sizeof(struct wsdb_col_data_rec);
    append_in_trx_block(trx, &bi, (uint16_t)2, (char *)&len);
    append_in_trx_block(
        trx, &bi, sizeof(struct wsdb_col_data_rec), (char *)&row_data
    );
    append_in_trx_block(trx, &bi, len, (char *)&data);
    
    close_trx_block_access(trx, &bi);
    
    return WSDB_OK;
}

static int copy_from_block(
    char *target, uint16_t len, struct block_info *bi, char **pos
) {
    while (len && bi->block) {
        char *end = (char *)bi->block + bi->block->pos;
        uint16_t avail = end - *pos;
        if (target) memcpy(target, *pos, (len < avail) ? len : avail);
        if (len > avail) {
            file_cache_forget(local_cache, bi->cache_id);
            bi->cache_id = bi->block->next_block;
            bi->block = (struct block_hdr *)file_cache_get(
                local_cache, bi->cache_id
            );
            *pos = (bi->block) ? 
                (char *)bi->block + sizeof(struct block_hdr) : NULL;
            GU_DBUG_PRINT(
                "wsdb",("block: %p, %d, pos: %p", bi->block, bi->cache_id,*pos)
            );
            if (target) target += avail;
            len -= avail;
        } else {
            *pos += len;
            assert((len==avail) || (*pos<end));
            len   = 0;
            if (*pos == end) {
                if (bi->block->next_block) {
                    file_cache_forget(local_cache, bi->cache_id);
                    bi->cache_id = bi->block->next_block;
                    bi->block = (struct block_hdr *)file_cache_get(
                        local_cache, bi->cache_id
                    );
                    *pos = (bi->block) ? 
                        (char *)(bi->block) + sizeof(struct block_hdr) : NULL;
                    GU_DBUG_PRINT(
                        "wsdb",("pos==end, new: %p %p", bi->block, *pos)
                    );
                } else {
                    GU_DBUG_PRINT("wsdb",("pos==end, no next block"));
                    bi->block = NULL;
                }
            } else {
                //GU_DBUG_PRINT("wsdb",("end: %p, pos: %p",end, *pos));
                if (*pos > end) {
                    assert(0);
                }
            }
        }
    }
    return len;
}

static void free_wsdb_key_part(struct wsdb_key_part *part) {
    if (!part) return;
    gu_free(part->data);
}

static void free_wsdb_table_key(struct wsdb_table_key *tkey) {
    uint16_t i;
    if(!tkey) return;
    for (i=0; i<tkey->key_part_count; i++) {
        free_wsdb_key_part(&tkey->key_parts[i]);
    }
    if (tkey->key_parts) gu_free(tkey->key_parts);
    gu_free(tkey);
}

static void free_wsdb_key_rec(struct wsdb_key_rec *key) {
    if (!key) return;
    gu_free(key->dbtable);
    free_wsdb_table_key(key->key);
    gu_free(key);
}

static void free_wsdb_col_data_rec(struct wsdb_col_data_rec *rec) {
    if (!rec) return;
    gu_free(rec->data);
    gu_free(rec);
}

static void free_wsdb_row_data_rec(struct wsdb_row_data_rec *rec) {
    if (!rec) return;
    gu_free(rec->data);
    gu_free(rec);
}

static void free_wsdb_item_rec(struct wsdb_item_rec *item) {
    uint16_t i;
    if (!item) return;

    free_wsdb_key_rec(item->key);

    switch (item->data_mode) {
    case NO_DATA:
        break;
    case COLUMN:
      for (i=0; i<item->u.cols.col_count; i++) {
        free_wsdb_col_data_rec(&item->u.cols.data[i]);
      }
      break;
    case ROW:
        free_wsdb_row_data_rec(&item->u.row);
      break;
    }
}

static void free_wsdb_query(struct wsdb_query *query) {
    if (!query) return;

    gu_free(query->query);
}

void wsdb_write_set_free(struct wsdb_write_set *ws) {
    uint16_t i;
    if (!ws) return;

    for (i=0; i<ws->query_count; i++) {
        free_wsdb_query(&ws->queries[i]);
    }
    gu_free(ws->queries);
    for (i=0; i<ws->item_count; i++) {
        free_wsdb_item_rec(&ws->items[i]);
    }
    gu_free(ws->items);

    gu_free(ws);
}

enum ws_oper_mode {
    WS_OPER_COUNT,
    WS_OPER_CREATE,
};

static int get_write_set_do(
    struct wsdb_write_set *ws,
    struct trx_info       *trx,
    enum ws_oper_mode      mode
) {

    char *pos;
    uint query_count = 0;
    uint item_count  = 0;

    struct block_info bi;

    GU_DBUG_ENTER("wsdb_get_write_set_do");

    bi.cache_id = trx->first_block;
    bi.block = (struct block_hdr *)file_cache_get(local_cache, bi.cache_id);
    if (!bi.block) return WSDB_ERR_WS_FAIL;

    pos = (char *)bi.block + sizeof(struct block_hdr);
    
    while (bi.block) {
        char rec_type;

        if (copy_from_block((char *)&rec_type, 1, &bi, &pos)) break;

        GU_DBUG_PRINT("wsdb",("rec: %c", rec_type));
        GU_DBUG_PRINT("wsdb",("block: %p %d", bi.block, bi.cache_id));
        switch(rec_type) {
        case REC_TYPE_QUERY: {
            uint16_t query_len;
            
            /* get the length of the SQL query */
            copy_from_block((char *)(&query_len), 2, &bi, &pos);

            if (mode == WS_OPER_CREATE) {
                ws->queries[query_count].query_len = query_len;
                ws->queries[query_count].query =
                    (char*) gu_malloc (query_len + 1);
                memset(
                    ws->queries[query_count].query, '\0', query_len + 1
                );
            }

            copy_from_block(
                (mode == WS_OPER_COUNT) ? NULL :
                (char *)ws->queries[query_count].query, 
                query_len, &bi, &pos
            );

            copy_from_block(
                (mode == WS_OPER_COUNT) ? NULL :
                (char *)(&ws->queries[query_count].timeval), 
                4, &bi, &pos
            );
            copy_from_block(
                (mode == WS_OPER_COUNT) ? NULL :
                (char *)(&ws->queries[query_count].randseed), 
                4, &bi, &pos
            );

            query_count++;
            break;
        }
        case REC_TYPE_ACTION: {
            uint16_t len;

            copy_from_block((char *)&len, 2, &bi, &pos);
            if (len != 1) {
                gu_error("wsdb action len: %d", len);
                assert(len == 1);
            }
            copy_from_block(
                (mode == WS_OPER_COUNT) ? NULL :
                (char *)&(ws->items[item_count].action), len, &bi, &pos);

            item_count++;
            break;
        }
        case REC_TYPE_ROW_KEY: {
            struct wsdb_key_rec   *key;
            struct wsdb_table_key *tkey;
            uint16_t i;
            uint16_t key_part_count;

            /* retrieve db and table name */
            if (mode == WS_OPER_CREATE) {
                key = ws->items[item_count-1].key = (struct wsdb_key_rec *)
                    gu_malloc (sizeof(struct wsdb_key_rec));
                copy_from_block((char *)&key->dbtable_len, 2, &bi, &pos);
                key->dbtable = (char *) gu_malloc ((size_t)key->dbtable_len);
                copy_from_block(key->dbtable, key->dbtable_len, &bi, &pos);
            } else {
                uint16_t dbtable_len;
                copy_from_block((char *)&dbtable_len, 2, &bi, &pos);
                copy_from_block(NULL, dbtable_len, &bi, &pos);
            }

            /* number of key parts */
            if (mode == WS_OPER_CREATE) {
                key->key = tkey = (struct wsdb_table_key *) gu_malloc (
                    sizeof(struct wsdb_table_key)
                );
                copy_from_block((char *)&tkey->key_part_count, 2, &bi, &pos);
            
                tkey->key_parts = (struct wsdb_key_part *) gu_malloc (
                    tkey->key_part_count * sizeof(struct wsdb_key_part)
                );
            } else {
                copy_from_block((char *)&key_part_count, 2, &bi, &pos);
            }
            if (mode == WS_OPER_CREATE) {
                for (i=0; i<tkey->key_part_count; i++) {
                    struct wsdb_key_part *kp = &tkey->key_parts[i];
                    copy_from_block((char *)&kp->type, (uint16_t)1, &bi,&pos);
                    copy_from_block((char *)&kp->length,(uint16_t)2,&bi,&pos);
                    kp->data = gu_malloc ((size_t)kp->length);
                    copy_from_block((char *)kp->data, kp->length, &bi, &pos);
                }
            } else {
                for (i=0; i<key_part_count; i++) {
                    uint16_t kp_len;
                    copy_from_block(NULL, (uint16_t)1, &bi, &pos);
                    copy_from_block((char *)&kp_len, (uint16_t)2, &bi, &pos);
                    copy_from_block(NULL, kp_len, &bi, &pos);
                }
            }
#ifdef REMOVED
            copy_from_block((char *)&row_key, key_len, &bi, &pos);
            if (mode == WS_OPER_CREATE) {
                ws->items[item_count].key = file_row_key_2_wsdb_key(&row_key);
                GU_DBUG_PRINT("wsdb",("key: %p, len: %d", row_key.key, key_len));
            }
#endif
            break;
        }

        case REC_TYPE_ROW_DATA: {
            struct wsdb_row_data_rec   *row;

            /* retrieve row length and data */
            if (mode == WS_OPER_CREATE) {
                row = &ws->items[item_count-1].u.row;
                copy_from_block((char *)&row->length, 2, &bi, &pos);
                row->data = (char *) gu_malloc ((size_t)row->length);
                copy_from_block(row->data, row->length, &bi, &pos);
            } else {
                uint16_t len;
                copy_from_block((char *)&len, 2, &bi, &pos);
                copy_from_block(NULL, len, &bi, &pos);
            }

            break;
        }
        default:
            GU_DBUG_RETURN(WSDB_OK);
        }
    }

    file_cache_forget(local_cache, bi.cache_id);
    
    ws->query_count = query_count;
    ws->item_count  = item_count;

    ws->local_trx_id  = trx->id;
    ws->level         = WSDB_WS_QUERY;

    GU_DBUG_PRINT("wsdb",
      ("trx: %llu, last: %llu, level: %d", trx->id, last_committed_trx, ws->level)
    );
    GU_DBUG_RETURN(WSDB_OK);
}

struct wsdb_write_set *wsdb_get_write_set(
    local_trxid_t trx_id, connid_t conn_id
) {
    struct trx_info       *trx = get_trx_info(trx_id);
    struct wsdb_write_set *ws;
/* @unused:
    cache_id_t             cache_id;
    struct items          *items_first, *items_last, *im;
    struct queries        *queries_first, *queries_last, *q;
    struct block_hdr      *block;
    char                  *pos;

    struct block_info bi;
*/
    GU_DBUG_ENTER("wsdb_get_write_set");

    if (!trx) {
	GU_DBUG_PRINT("wsdb",
		   ("trx does not exist in wsdb_get_write_set: %llu", trx_id)
        );
        GU_DBUG_RETURN(NULL);
    }

    ws = (struct wsdb_write_set *) gu_malloc (sizeof(struct wsdb_write_set));
    ws->type = WSDB_WS_TYPE_TRX;

    /* build connection setup queries */
    conn_build_connection_queries(ws, conn_id);

    /* count the number of queries and items */
    get_write_set_do(ws, trx, WS_OPER_COUNT);

    ws->items = (struct wsdb_item_rec *) gu_malloc (
        ws->item_count * sizeof(struct wsdb_item_rec)
    );
    memset(ws->items, '\0', ws->item_count * sizeof(struct wsdb_item_rec));

    GU_DBUG_PRINT("wsdb",("query count: %d", ws->query_count));
    ws->queries = (struct wsdb_query *) gu_malloc (
        ws->query_count * sizeof(struct wsdb_query)
    );
    memset(ws->queries, '\0', ws->query_count * sizeof(struct wsdb_query));

    /* allocate queries and items */
    get_write_set_do(ws, trx, WS_OPER_CREATE);

    ws->local_trx_id  = trx_id;
    ws->last_seen_trx = get_last_committed_trx();
    ws->level         = WSDB_WS_QUERY;

    if (ws->last_seen_trx == 0) {
        gu_warn("Setting ws.last_seen_trx to 0");
    }

    GU_DBUG_PRINT("wsdb",
      ("trx: %llu, last: %llu, level: %d",trx_id,last_committed_trx,ws->level)
    );
    GU_DBUG_RETURN(ws);
}

struct wsdb_write_set *wsdb_get_conn_write_set(
    connid_t conn_id
) {
    struct wsdb_write_set *ws;

    GU_DBUG_ENTER("wsdb_get_conn_write_set");

    ws = (struct wsdb_write_set *) gu_malloc (sizeof(struct wsdb_write_set));
    ws->type = WSDB_WS_TYPE_CONN;

    /* build connection setup queries */
    conn_build_connection_queries(ws, conn_id);

    /* count the number of queries and items */
    ws->item_count    = 0;
    ws->items         = NULL;
    ws->query_count   = 0;
    ws->queries       = NULL;
    ws->local_trx_id  = 0;
    ws->last_seen_trx = 0;
    ws->level         = WSDB_WS_QUERY;

    GU_DBUG_RETURN(ws);
}

int wsdb_set_exec_query(
    struct wsdb_write_set *ws, char *query, uint16_t query_len
) {

    GU_DBUG_ENTER("wsdb_set_exec_query");

    if (ws->type != WSDB_WS_TYPE_CONN) {
        gu_error("Bad ws type: %d", ws->type);
        GU_DBUG_RETURN(WSDB_ERR_BAD_WRITE_SET);
    }

    /* count the number of queries and items */
    ws->query_count = 1;

    GU_DBUG_PRINT("wsdb",("query count: %d", ws->query_count));
    ws->queries = (struct wsdb_query *) gu_malloc (
        ws->query_count * sizeof(struct wsdb_query)
    );
    memset(ws->queries, '\0', ws->query_count * sizeof(struct wsdb_query));

    /* allocate queries and items */
    ws->queries[0].query = (char *) gu_malloc (query_len+1);

    strncpy(ws->queries[0].query, query, query_len);
    ws->queries[0].query[query_len] = '\0';
    ws->queries[0].query_len = query_len+1;

    GU_DBUG_RETURN(WSDB_OK);
}

int wsdb_set_global_trx_committed(trx_seqno_t trx_seqno) {
    GU_DBUG_ENTER("wsdb_set_global_trx_committed");
    GU_DBUG_PRINT("wsdb",("last committed: %llu", trx_seqno));

    if (get_last_committed_trx() < trx_seqno) {
        set_last_committed_trx(trx_seqno);
    } else {
	GU_DBUG_PRINT(
            "wsdb",("setting (G) last_committed_trx to lower value: %llu %llu",
                           trx_seqno, get_last_committed_trx())
        );
    }

    GU_DBUG_RETURN( WSDB_OK);
}
int wsdb_set_local_trx_committed(local_trxid_t trx_id) {
    struct trx_info       *trx = get_trx_info(trx_id);

    GU_DBUG_ENTER("wsdb_set_local_trx_committed");
    if (!trx) {
	GU_DBUG_PRINT("wsdb",("trx not found, set_local_trx_commit: %llu",trx_id));
        GU_DBUG_RETURN(WSDB_ERR_TRX_UNKNOWN);
    }
    GU_DBUG_PRINT("wsdb",("last committed: %llu->%llu", trx_id, trx->seqno_g));

    if (get_last_committed_trx() < trx->seqno_g) {
        set_last_committed_trx(trx->seqno_g);
    } else {
	GU_DBUG_PRINT(
            "wsdb",("setting last_committed_trx to lower value: %llu %llu",
                           trx->seqno_g, get_last_committed_trx())
        );
    }

    GU_DBUG_RETURN( WSDB_OK);
}

int wsdb_delete_local_trx(local_trxid_t trx_id) {
    struct trx_info       *trx = get_trx_info(trx_id);
    cache_id_t             cache_id;
    struct block_hdr      *block;

    GU_DBUG_ENTER("wsdb_delete_local_trx");
    GU_DBUG_PRINT("wsdb",("trx: %llu", trx_id));
    if (!trx) {
	GU_DBUG_PRINT("wsdb",("trx not found in del_local_trx: %llu", trx_id));
        GU_DBUG_RETURN(WSDB_ERR_TRX_UNKNOWN);
    }

    cache_id = trx->first_block;
    block = (struct block_hdr *)file_cache_get(local_cache, cache_id);
    if (!block) {
        gu_error("trx has empty cache block: %llu", cache_id);
        GU_DBUG_RETURN(WSDB_ERR_TRX_UNKNOWN);
    }

    while (block) {
        cache_id_t next_cb = block->next_block;
        file_cache_delete(local_cache, cache_id);
        cache_id = next_cb;
        block = (struct block_hdr *)file_cache_get(local_cache, cache_id);
    }

    GU_DBUG_RETURN(WSDB_OK);
}

int wsdb_delete_local_trx_info(local_trxid_t trx_id) {
    struct trx_info       *trx = get_trx_info(trx_id);

    GU_DBUG_ENTER("wsdb_delete_local_trx_info");
    GU_DBUG_PRINT("wsdb",("trx: %llu", trx_id));

    if (!trx) {
        GU_DBUG_RETURN(WSDB_ERR_TRX_UNKNOWN);
    }
    remove_trx_info(trx_id);

    GU_DBUG_RETURN(WSDB_OK);
}

int wsdb_assign_trx(
    local_trxid_t trx_id, trx_seqno_t seqno_l, trx_seqno_t seqno_g
) {
    struct trx_info       *trx = get_trx_info(trx_id);
 
    GU_DBUG_ENTER("wsdb_assign_trx");
    GU_DBUG_PRINT("wsdb",("trx: %llu -> %llu(%llu)",
                          trx_id, seqno_l, seqno_g));

    if (!trx) {
        gu_error("trx does not exist in assign_trx: %llu", trx_id);
        GU_DBUG_RETURN(WSDB_ERR_TRX_UNKNOWN);
    }

    trx->seqno_l = seqno_l;
    trx->seqno_g = seqno_g;

    GU_DBUG_RETURN(WSDB_OK);
}

trx_seqno_t wsdb_get_local_trx_seqno(local_trxid_t trx_id) {
    struct trx_info       *trx = get_trx_info(trx_id);

    GU_DBUG_ENTER("wsdb_get_local_trx_seqno");
    if (!trx) {
	GU_DBUG_PRINT("wsdb",("trx not found, : %llu",trx_id));
        GU_DBUG_RETURN(0);
    }
    GU_DBUG_RETURN(trx->seqno_l);
}
