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
    char            ident;
    local_trxid_t   id;
    wsdb_trx_info_t info;
    cache_id_t      first_block;
    cache_id_t      last_block;
};
#define IDENT_trx_info 'X'

/* seqno of last committed transaction  */
static trx_seqno_t last_committed_seqno = 0;
/* keeps overall count of local transactions currently referencing any seqno 
 * via last_seen */
static ulong       last_committed_refs   = 0;
static trx_seqno_t next_to_discard_seqno = 0;
static ulong       next_to_discard_refs  = 0;
static trx_seqno_t safe_to_discard_seqno = 0;
static const ulong discard_interval = 100;

static gu_mutex_t  last_committed_seqno_mtx; // mutex protecting last_commit...

// if seqno != last_seen - local trx, decrement reference counters
static inline void set_last_committed_seqno(trx_seqno_t seqno) {
    GU_DBUG_ENTER("set_last_committed_trx");
    gu_mutex_lock(&last_committed_seqno_mtx);
    if (last_committed_seqno < seqno) {
        last_committed_seqno = seqno;

        // check if we need to update safe_to_discard
        if (!next_to_discard_seqno && 
            ((safe_to_discard_seqno + discard_interval) < seqno)) {
            // high time to try to discard new seqno
            assert (0 == next_to_discard_refs);
            if (last_committed_refs) {
                // can't do it right away, some seqnos < seqno-1 are referenced
                next_to_discard_seqno = seqno - 1;
                // all local transactions reference seqnos < to_discard_seqno
                // wait until it gets unreferenced
                next_to_discard_refs  = last_committed_refs;
            }
            else {
                assert (safe_to_discard_seqno < seqno);
                safe_to_discard_seqno = seqno - 1;
            }
            gu_info ("DISCARD begin: safe = %llu, next = %llu, next refs = %lu",
                     safe_to_discard_seqno, next_to_discard_seqno,
                     next_to_discard_refs);
        }
    }
    else {
	GU_DBUG_PRINT(
            "wsdb",("trying to set last_committed_trx to lower value: "
                    "%llu %llu", last_committed_seqno, seqno)
            );
    }
    gu_mutex_unlock(&last_committed_seqno_mtx);
    GU_DBUG_VOID_RETURN;
}

trx_seqno_t wsdb_get_last_committed_seqno() {
    trx_seqno_t tmp;
    gu_mutex_lock(&last_committed_seqno_mtx);
    tmp = last_committed_seqno;
    last_committed_refs++;
    gu_mutex_unlock(&last_committed_seqno_mtx);
    return tmp;
}

void wsdb_deref_seqno (trx_seqno_t last_seen)
{
    gu_mutex_lock(&last_committed_seqno_mtx);
    last_committed_refs--;
    assert (last_committed_refs >= 0);
    if (next_to_discard_refs && next_to_discard_seqno >= last_seen) {
        // we're waiting until to_discard_seqno becomes unreferenced
        // this trx refernced it, decrement reference counter
        assert (to_discard_seqno);
        next_to_discard_refs--;
        if (!next_to_discard_refs) {
            // no more local refrences to seqnos <= next_to_discard_seqno,
            // next is now safe to discard
            safe_to_discard_seqno = next_to_discard_seqno;
            gu_info ("DISCARD end: safe = %llu, next = %llu, next refs = %lu",
                     safe_to_discard_seqno, next_to_discard_seqno,
                     next_to_discard_refs);
            next_to_discard_seqno = 0;
        }
        else {
            gu_info ("DISCARD cont: safe = %llu, next = %llu, next refs = %lu",
                     safe_to_discard_seqno, next_to_discard_seqno,
                     next_to_discard_refs);        
        }
    }
    gu_mutex_unlock(&last_committed_seqno_mtx);
}

trx_seqno_t wsdb_get_safe_to_discard_seqno()
{
    // seems like no need to lock
    return safe_to_discard_seqno;
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
    uint16_t    trxs_max
) {
    char full_name[256];
    int rcode;
    int cache_size;

    memset(full_name, 256, '\0');
    sprintf(
        full_name, "%s%s%s", (dir)  ? dir  : DEFAULT_WORK_DIR, 
        PATH_SEPARATOR,
        (file) ? file : DEFAULT_LOCAL_FILE
    );
    block_size = (block_size) ? block_size : DEFAULT_BLOCK_SIZE;
    local_file = file_create(full_name, block_size);

    /* consult application for desired local cache size */
    cache_size = wsdb_conf_get_param(
        WSDB_CONF_LOCAL_CACHE_SIZE, WSDB_TYPE_INT) ?
            *(int *)wsdb_conf_get_param(
                 WSDB_CONF_LOCAL_CACHE_SIZE, WSDB_TYPE_INT) : 
            LOCAL_CACHE_LIMIT;

    /* limit local cache size to 'cache_size' bytes */
    local_cache = file_cache_open(
        local_file, cache_size, cache_size/block_size
    );

    trx_limit = (trxs_max) ? trxs_max : TRX_LIMIT;
    trx_hash  = wsdb_hash_open(trx_limit, hash_fun_64, hash_cmp_64, true,false);

    rcode = conn_init(0);

    /* initialize last_committed_trx mutex */
    gu_mutex_init(&last_committed_seqno_mtx, NULL);

    return WSDB_OK;
}

void local_close() {
    uint32_t mem_usage;

    mem_usage = wsdb_hash_report(trx_hash);
    gu_info("mem usage for trx hash: %u", mem_usage);

    mem_usage = file_cache_report(local_cache);
    gu_info("mem usage for local cache: %u", mem_usage);

    file_cache_close(local_cache);
    wsdb_hash_close(trx_hash);
    conn_close();
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
    int rcode;

    /* get the block for transaction */
    trx = (struct trx_info *)wsdb_hash_search(
        trx_hash, sizeof(local_trxid_t), (char *)&trx_id
    );
    if (trx) {
        gu_warn("trx exist already: %llu", trx_id);
        return NULL;
    }

    MAKE_OBJ(trx, trx_info);
    trx->id            = trx_id;
    trx->info.seqno_g  = TRX_SEQNO_MAX;
    trx->info.seqno_l  = TRX_SEQNO_MAX;
    trx->first_block   = 0;
    trx->last_block    = 0;
    trx->info.ws       = 0;
    trx->info.position = WSDB_TRX_POS_VOID;
    trx->info.state    = WSDB_TRX_VOID;

    GU_DBUG_PRINT("wsdb", ("created new trx: %llu", trx_id));

    rcode = wsdb_hash_push(
        trx_hash, sizeof(local_trxid_t), (char *)&trx_id, trx
    );
    switch (rcode) {
    case WSDB_OK: 
        break;
    case WSDB_ERR_HASH_DUPLICATE:
        /* somebody pushed the trx just before us, read again... */
        trx = (struct trx_info *)wsdb_hash_search(
            trx_hash, sizeof(local_trxid_t), (char *)&trx_id
        );
        if (!trx) {
            gu_error("trx does not exist already: %llu", trx_id);
            return NULL;
        }
        break;
    default:
        gu_error("trx push failed for: %llu, rcode: %d", trx_id, rcode);
        return NULL;
    }

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
    struct trx_info *trx, struct block_info *bi, uint32_t len, char *data
) {
    GU_DBUG_ENTER("append_in_trx_block");

    while (len) {
        uint32_t stored;
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
    uint32_t          query_len = strlen(query) + 1;
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
    append_in_trx_block(trx, &bi, (uint32_t)4, (char *)&query_len);
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
static int read_next_block(struct block_info *bi, cache_id_t last_block) {
    cache_id_t next_block = bi->block->next_block;
    local_trxid_t trx_id  = bi->block->trx_id;

    file_cache_forget(local_cache, bi->cache_id);

    if (next_block) {
        if (bi->cache_id == last_block) {
            gu_error(
               "reading past last cache block, last: %d next: %d for %llu", 
               last_block, next_block, trx_id
            );
        }
        bi->cache_id = next_block;
        bi->block = (struct block_hdr *)file_cache_get(
            local_cache, bi->cache_id
        );
        if (!bi->block) {
            gu_error(
                 "could not read cache block, last:%d cur:%d", 
                 last_block, bi->cache_id
            );
            return -1;
        }
        if (bi->block->trx_id != trx_id) {
            gu_error(
               "trx changed in cache block, prev: %llu next %llu", 
               bi->block->trx_id, trx_id
            );
        }
    } else {
        gu_debug(
               "could not read full cache block, last:%d cur:%d", 
               last_block, bi->cache_id
        );
        bi->block = NULL;
        return 1;
    }
    return 0;
}
static int copy_from_block(
    char *target, uint32_t len, struct block_info *bi, 
    char **pos, cache_id_t last_block
) {
    while (len && bi->block) {
        char *end = (char *)bi->block + bi->block->pos;
        uint32_t avail = end - *pos;
        if (target) memcpy(target, *pos, (len < avail) ? len : avail);

        if (len > avail) {
            switch (read_next_block(bi, last_block)) {
            case -1:
            case 1:
                *pos = NULL;
                gu_error(
                     "block read failed at, len: %d avail: %d", len, avail
                );
                break;
            case 0:
                *pos = (char *)bi->block + sizeof(struct block_hdr);
                break;
            }  

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
                switch (read_next_block(bi, last_block)) {
                case -1:
                    *pos = NULL;
                    gu_error(
                        "block read failed at, len: %d avail: %d", len, avail
                    );
                case 1: break;
                case 0:
                    *pos = (char *)bi->block + sizeof(struct block_hdr);
                    break;
                }  
            } else {
                //GU_DBUG_PRINT("wsdb",("end: %p, pos: %p",end, *pos));
                if (*pos > end) {
                    assert(0);
                }
            }
        }
    }
    /* len=0 if full event was found */
    if (len) {
      gu_error(
          "Could not read full record from cache blocks, remaining: %d", len
      );
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
    uint32_t i;
    if (!ws) return;

    for (i=0; i<ws->query_count; i++) {
        free_wsdb_query(&ws->queries[i]);
    }
    gu_free(ws->queries);
    for (i=0; i<ws->conn_query_count; i++) {
        free_wsdb_query(&ws->conn_queries[i]);
    }
    if (ws->conn_queries) gu_free(ws->conn_queries);
    for (i=0; i<ws->item_count; i++) {
        free_wsdb_item_rec(&ws->items[i]);
    }
    gu_free(ws->items);

    if (ws->rbr_buf_len)
         gu_free(ws->rbr_buf);
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
    uint32_t query_count = 0;
    uint32_t item_count  = 0;

    struct block_info bi;

    GU_DBUG_ENTER("wsdb_get_write_set_do");

    bi.cache_id = trx->first_block;
    bi.block = (struct block_hdr *)file_cache_get(local_cache, bi.cache_id);
    if (!bi.block) return WSDB_ERR_WS_FAIL;

    pos = (char *)bi.block + sizeof(struct block_hdr);
    
    while (bi.block) {
        char rec_type;

        if (copy_from_block(
            (char *)&rec_type, 1, &bi, &pos, trx->last_block
        )) {
            gu_error("could not retrieve write set for trx: %llu", trx->id);
            return WSDB_ERR_WS_FAIL;
        }

        GU_DBUG_PRINT("wsdb",("rec: %c", rec_type));
        GU_DBUG_PRINT("wsdb",("block: %p %d", bi.block, bi.cache_id));
        switch(rec_type) {
        case REC_TYPE_QUERY: {
            uint32_t query_len;
            
            /* get the length of the SQL query */
            if (copy_from_block(
                (char *)(&query_len), 4, &bi, &pos, trx->last_block)
            ) {
                gu_error("could not retrieve write set for trx: %lu", trx->id);
                return WSDB_ERR_WS_FAIL;
            }
            if (mode == WS_OPER_CREATE) {
                ws->queries[query_count].query_len = query_len;
                ws->queries[query_count].query =
                    (char*) gu_malloc (query_len + 1);
                memset(
                    ws->queries[query_count].query, '\0', query_len + 1
                );
            }

            if (copy_from_block(
                (mode == WS_OPER_COUNT) ? NULL :
                (char *)ws->queries[query_count].query, 
                query_len, &bi, &pos, trx->last_block
            )) {
                gu_error("could not retrive write set for trx: %lu", trx->id);
                return WSDB_ERR_WS_FAIL;
            }

            if (copy_from_block(
                (mode == WS_OPER_COUNT) ? NULL :
                (char *)(&ws->queries[query_count].timeval), 
                4, &bi, &pos, trx->last_block
            )) {
                gu_error("could not retrive write set for trx: %lu", trx->id);
                return WSDB_ERR_WS_FAIL;
            }
            if (copy_from_block(
                (mode == WS_OPER_COUNT) ? NULL :
                (char *)(&ws->queries[query_count].randseed), 
                4, &bi, &pos, trx->last_block
            )) {
                gu_error("could not retrive write set for trx: %lu", trx->id);
                return WSDB_ERR_WS_FAIL;
            }

            query_count++;
            break;
        }
        case REC_TYPE_ACTION: {
            uint16_t len;

            if (copy_from_block(
                (char *)&len, 2, &bi, &pos, trx->last_block)
            ) {
                gu_error("could not retrive write set for trx: %lu", trx->id);
                return WSDB_ERR_WS_FAIL;
            }
            if (len != 1) {
                gu_error("wsdb action len: %d", len);
                assert(len == 1);
            }
            if (copy_from_block(
                (mode == WS_OPER_COUNT) ? NULL :
                (char *)&(ws->items[item_count].action), 
                len, &bi, &pos, trx->last_block
            )) {
                gu_error("could not retrive write set for trx: %lu", trx->id);
                return WSDB_ERR_WS_FAIL;
            }

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
                if (copy_from_block(
                    (char *)&key->dbtable_len, 2, &bi, &pos,
                    trx->last_block)
                ){
                    gu_error("could not retrive write set trx: %Llu", trx->id);
                    return WSDB_ERR_WS_FAIL;
                }
                key->dbtable = (char *) gu_malloc ((size_t)key->dbtable_len);
                if (copy_from_block(
                    key->dbtable, key->dbtable_len, &bi, &pos, 
                    trx->last_block)
                ){
                    gu_error("could not retrive write set trx: %Llu", trx->id);
                    return WSDB_ERR_WS_FAIL;
                }
            } else {
                uint16_t dbtable_len;
                if (copy_from_block(
                    (char *)&dbtable_len, 2, &bi, &pos, 
                    trx->last_block)
                ) {
                    gu_error("could not retrive write set trx: %lu", trx->id);
                    return WSDB_ERR_WS_FAIL;
                }
                if (copy_from_block(
                    NULL, dbtable_len, &bi, &pos, trx->last_block)
                ) {
                    gu_error("could not retrive write set trx: %llu", trx->id);
                    return WSDB_ERR_WS_FAIL;
                }
            }

            /* number of key parts */
            if (mode == WS_OPER_CREATE) {
                key->key = tkey = (struct wsdb_table_key *) gu_malloc (
                    sizeof(struct wsdb_table_key)
                );
                if (copy_from_block(
                    (char *)&tkey->key_part_count, 2, &bi, &pos, trx->last_block
                )) {
                    gu_error("could not retrive write set trx: %lu", trx->id);
                    return WSDB_ERR_WS_FAIL;
                }            
                tkey->key_parts = (struct wsdb_key_part *) gu_malloc (
                    tkey->key_part_count * sizeof(struct wsdb_key_part)
                );
            } else {
                if (copy_from_block(
                    (char *)&key_part_count, 2, &bi, &pos, 
                    trx->last_block)
                ) {
                    gu_error("could not retrive write set trx: %llu", trx->id);
                    return WSDB_ERR_WS_FAIL;
                }
            }
            if (mode == WS_OPER_CREATE) {
                for (i=0; i<tkey->key_part_count; i++) {
                    struct wsdb_key_part *kp = &tkey->key_parts[i];
                    if (copy_from_block(
                        (char *)&kp->type, (uint16_t)1, &bi, &pos, 
                        trx->last_block
                    )) {
                        gu_error(
                          "could not retrieve write set for trx: %llu", trx->id
                        );
                        return WSDB_ERR_WS_FAIL;
                    }

                    if (copy_from_block(
                        (char *)&kp->length, (uint16_t)2, &bi, &pos,
                        trx->last_block
                    )) {
                        gu_error(
                          "could not retrieve write set for trx: %llu", trx->id
                        );
                        return WSDB_ERR_WS_FAIL;
                    }

                    kp->data = gu_malloc ((size_t)kp->length);
                    if (copy_from_block(
                        (char *)kp->data, kp->length, &bi, &pos,
                        trx->last_block
                    )) {
                        gu_error(
                          "could not retrieve write set for trx: %llu", trx->id
                        );
                        return WSDB_ERR_WS_FAIL;
                    }
                }
            } else {
                for (i=0; i<key_part_count; i++) {
                    uint16_t kp_len;
                    if (copy_from_block(
                        NULL, (uint16_t)1, &bi, &pos, trx->last_block
                    )) {
                        gu_error(
                          "could not retrive write set for trx: %llu", trx->id
                        );
                        return WSDB_ERR_WS_FAIL;
                    }
                    if (copy_from_block(
                        (char *)&kp_len, (uint16_t)2, &bi, &pos, trx->last_block
                    )) {
                        gu_error(
                          "could not retrive write set for trx: %llu", trx->id
                        );
                        return WSDB_ERR_WS_FAIL;
                    }
                    if (copy_from_block(
                        NULL, kp_len, &bi, &pos, trx->last_block)
                    ) {
                        gu_error(
                          "could not retrive write set for trx: %llu", trx->id
                        );
                        return WSDB_ERR_WS_FAIL;
                    }                
                }
            }
            break;
        }

        case REC_TYPE_ROW_DATA: {
            struct wsdb_row_data_rec   *row;

            /* retrieve row length and data */
            if (mode == WS_OPER_CREATE) {
                row = &ws->items[item_count-1].u.row;
                if (copy_from_block(
                    (char *)&row->length, 2, &bi, &pos, trx->last_block)
                ) {
                    gu_error(
                      "could not retrieve write set for trx: %llu", trx->id
                    );
                    return WSDB_ERR_WS_FAIL;
                }

                row->data = (char *) gu_malloc ((size_t)row->length);
                if (copy_from_block(
                    row->data, row->length, &bi, &pos, trx->last_block
                )) {
                    gu_error(
                      "could not retrieve write set for trx: %llu", trx->id
                    );
                    return WSDB_ERR_WS_FAIL;
                }
            } else {
                uint16_t len;
                if (copy_from_block(
                    (char *)&len, 2, &bi, &pos, trx->last_block
                )) {
                    gu_error(
                      "could not retrieve write set for trx: %llu", trx->id
                    );
                    return WSDB_ERR_WS_FAIL;
                }

                if (copy_from_block(
                    NULL, len, &bi, &pos, trx->last_block
                )) {
                    gu_error(
                      "could not retrieve write set for trx: %llu", trx->id
                    );
                    return WSDB_ERR_WS_FAIL;
                }
            }

            break;
        }
        default:
            GU_DBUG_RETURN(WSDB_OK);
        }
    }

    if (bi.cache_id) {
        if (file_cache_forget(local_cache, bi.cache_id)) {
            gu_warn("cache forget failure, query count: %u , item count: %u",
                    query_count, item_count
            );
        }
    } else {
        gu_info("cache block id 0, in end of get_write_set_do");
    }
    
    ws->query_count = query_count;
    ws->item_count  = item_count;

    ws->local_trx_id  = trx->id;
    ws->level         = WSDB_WS_QUERY;

    GU_DBUG_PRINT("wsdb",
      ("trx: %llu, last: %llu, level: %d", trx->id, last_committed_seqno, ws->level)
    );
    GU_DBUG_RETURN(WSDB_OK);
}

/* effectively Query level WS constructor. For mysql's rbr we
 * need a separate one because there is a dedicated cache in mysql
 * where WS transport struct should get the content from. So far I
 * only add the content of mysql thd cache which affects the value of
 * WS' level */

struct wsdb_write_set *wsdb_get_write_set(
        local_trxid_t trx_id, connid_t conn_id, const char * row_buf, ulong buf_len
) {
    struct trx_info       *trx = get_trx_info(trx_id);
    struct wsdb_write_set *ws;

    GU_DBUG_ENTER("wsdb_get_write_set");

    if (!trx || !trx->first_block) {
	GU_DBUG_PRINT("wsdb",
		   ("trx does not exist in wsdb_get_write_set: %llu", trx_id)
        );
        GU_DBUG_RETURN(NULL);
    }

    ws = (struct wsdb_write_set *) gu_malloc (sizeof(struct wsdb_write_set));
    if (!ws) {
        gu_error("failed to allocate write set buffer for %llu", trx_id);
        GU_DBUG_RETURN(NULL);
    }
    ws->type = WSDB_WS_TYPE_TRX;

    /* build connection setup queries */
    conn_build_connection_queries(ws, conn_id);

    /* count the number of queries and items */
    if (get_write_set_do(ws, trx, WS_OPER_COUNT)) {
        gu_error("failed to count write set items %llu", trx_id);
        GU_DBUG_RETURN(NULL);
    }

    ws->items = (struct wsdb_item_rec *) gu_malloc (
        ws->item_count * sizeof(struct wsdb_item_rec)
    );
    if (!ws->items) {
        gu_error("failed to allocate write set items %d-%d for %llu", 
                 ws->item_count, ws->query_count, trx_id
        );
        GU_DBUG_RETURN(NULL);
    }
    memset(ws->items, '\0', ws->item_count * sizeof(struct wsdb_item_rec));

    GU_DBUG_PRINT("wsdb",("query count: %d", ws->query_count));
    ws->queries = (struct wsdb_query *) gu_malloc (
        ws->query_count * sizeof(struct wsdb_query)
    );
    if (!ws->queries) {
        gu_error("failed to allocate write set queries %d-%d for %llu", 
                 ws->item_count, ws->query_count, trx_id
        );
        GU_DBUG_RETURN(NULL);
    }
    memset(ws->queries, '\0', ws->query_count * sizeof(struct wsdb_query));

    /* allocate queries and items */
    if (get_write_set_do(ws, trx, WS_OPER_CREATE)) {
        gu_error("write set extracting failed at allocate phase");
        GU_DBUG_RETURN(NULL);
    }

    ws->local_trx_id  = trx_id;
    ws->last_seen_trx = wsdb_get_last_committed_seqno();
    ws->level         = buf_len? WSDB_WS_DATA_RBR /* actually rbr; there might be other raw data representations */ : WSDB_WS_QUERY;
    if (ws->level == WSDB_WS_DATA_RBR) {
            ws->rbr_buf_len = buf_len;
            ws->rbr_buf = (char *) gu_malloc(ws->rbr_buf_len);
            if (!ws->rbr_buf) {
                gu_error("failed to allocate write set rbr %lu for %llu", 
                         buf_len, trx_id
                );
                GU_DBUG_RETURN(NULL);
            }
            memcpy(ws->rbr_buf, row_buf, ws->rbr_buf_len);
    }
    else
    {
            ws->rbr_buf_len = 0;
            ws->rbr_buf = NULL;
    }
    if (ws->last_seen_trx == 0) {
        gu_warn("Setting ws.last_seen_trx to 0");
    }

    ws->key_composition = NULL;

    GU_DBUG_PRINT("wsdb",
      ("trx: %llu, last: %llu, level: %d",trx_id,last_committed_seqno,ws->level)
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
    struct wsdb_write_set *ws, char *query, uint32_t query_len
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
    ws->rbr_buf_len = 0;

    GU_DBUG_RETURN(WSDB_OK);
}

int wsdb_set_global_trx_committed(trx_seqno_t trx_seqno) {
    GU_DBUG_ENTER("wsdb_set_global_trx_committed");
    GU_DBUG_PRINT("wsdb",("last committed: %llu", trx_seqno));

    set_last_committed_seqno(trx_seqno);

    GU_DBUG_RETURN( WSDB_OK);
}

int wsdb_set_local_trx_committed(local_trxid_t trx_id) {
    struct trx_info       *trx = get_trx_info(trx_id);

    GU_DBUG_ENTER("wsdb_set_local_trx_committed");
    if (!trx) {
	GU_DBUG_PRINT("wsdb",("trx not found, set_local_trx_commit: %llu",trx_id));
        GU_DBUG_RETURN(WSDB_ERR_TRX_UNKNOWN);
    }
    GU_DBUG_PRINT("wsdb",("last committed: %llu->%llu", trx_id, trx->info.seqno_g));

    set_last_committed_seqno(trx->info.seqno_g);

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
    if (!cache_id) GU_DBUG_RETURN(WSDB_OK);

    block = (struct block_hdr *)file_cache_get(local_cache, cache_id);
    if (!block) {
        gu_error("trx has empty cache block: %llu", cache_id);
        trx->first_block = 0;
        trx->last_block  = 0;
        GU_DBUG_RETURN(WSDB_ERR_TRX_UNKNOWN);
    }

    while (block) {
        cache_id_t next_cb = block->next_block;
        file_cache_delete(local_cache, cache_id);
        
        if (next_cb) {
            cache_id = next_cb;
            block = (struct block_hdr *)file_cache_get(local_cache, cache_id);
        } else {
            block = NULL;
        }
    }

    trx->first_block = 0;
    trx->last_block  = 0;

    GU_DBUG_RETURN(WSDB_OK);
}

int wsdb_delete_local_trx_info(local_trxid_t trx_id) {
    struct trx_info       *trx = get_trx_info(trx_id);

    GU_DBUG_ENTER("wsdb_delete_local_trx_info");
    GU_DBUG_PRINT("wsdb",("trx: %llu", trx_id));

    if (!trx) {
        gu_warn("trx info did not exist: %llu", trx_id);
        GU_DBUG_RETURN(WSDB_ERR_TRX_UNKNOWN);
    }
    remove_trx_info(trx_id);

    GU_DBUG_RETURN(WSDB_OK);
}

static void print_trx_hash(void *ctx, void *data) {
    struct trx_info *trx = (struct trx_info *)data;
    gu_info(
        "trx, id: %llu seqno-g: %llu, seqno-l: %llu", 
        trx->id, trx->info.seqno_g, trx->info.seqno_l
    );
}

int wsdb_assign_trx_seqno(
    local_trxid_t trx_id, trx_seqno_t seqno_l, trx_seqno_t seqno_g, 
    enum wsdb_trx_state state
) {
    struct trx_info       *trx = get_trx_info(trx_id);
 
    GU_DBUG_ENTER("wsdb_assign_trx");
    GU_DBUG_PRINT("wsdb",("trx: %llu -> %llu(%llu)",
                          trx_id, seqno_l, seqno_g));

    if (!trx) {
        int elems;
        gu_error("trx does not exist in assign_trx, trx: %llu seqno: %llu", 
                 trx_id, seqno_g
        );

        elems = wsdb_hash_scan(trx_hash, NULL, print_trx_hash);
        gu_info("galera, found %d elements in trx hash", elems);

        /* try adding this trx in hash... */
        //trx = new_trx_info(trx_id);

        GU_DBUG_RETURN(WSDB_ERR_TRX_UNKNOWN);
    }

    trx->info.seqno_l = seqno_l;
    trx->info.seqno_g = seqno_g;
    trx->info.state   = state;

    GU_DBUG_RETURN(WSDB_OK);
}

int wsdb_assign_trx_state(local_trxid_t trx_id, enum wsdb_trx_state state) {
    struct trx_info       *trx = get_trx_info(trx_id);
 
    GU_DBUG_ENTER("wsdb_assign_trx_state");
    if (!trx) {
        gu_error("trx does not exist in assign_trx_state, trx: %lld", trx_id);

        GU_DBUG_RETURN(WSDB_ERR_TRX_UNKNOWN);
    }

    trx->info.state   = state;

    GU_DBUG_RETURN(WSDB_OK);
}

int wsdb_assign_trx_ws(
    local_trxid_t trx_id, struct wsdb_write_set *ws
) {
    struct trx_info       *trx = get_trx_info(trx_id);
 
    GU_DBUG_ENTER("wsdb_assign_trx_ws");

    if (!trx) {
        gu_error("trx does not exist in assign_trx_ws, trx: %llu", trx_id);
        GU_DBUG_RETURN(WSDB_ERR_TRX_UNKNOWN);
    }

    trx->info.ws = ws;

    GU_DBUG_RETURN(WSDB_OK);
}

int wsdb_assign_trx_pos(
    local_trxid_t trx_id, enum wsdb_trx_position pos
) {
    struct trx_info       *trx = get_trx_info(trx_id);
 
    GU_DBUG_ENTER("wsdb_assign_trx_pos");

    if (!trx) {
        gu_error("trx does not exist in assign_trx_pos, trx: %llu", trx_id);
        GU_DBUG_RETURN(WSDB_ERR_TRX_UNKNOWN);
    }

    trx->info.position = pos;

    GU_DBUG_RETURN(WSDB_OK);
}

void wsdb_get_local_trx_info(local_trxid_t trx_id, wsdb_trx_info_t *info) {
    struct trx_info       *trx = get_trx_info(trx_id);

    GU_DBUG_ENTER("wsdb_get_local_trx_info");
    if (!trx) {
	GU_DBUG_PRINT("wsdb",("trx not found, : %llu",trx_id));
        info->state = WSDB_TRX_MISSING;
        GU_DBUG_VOID_RETURN;
    }
    info->seqno_l  = trx->info.seqno_l;
    info->seqno_g  = trx->info.seqno_g;
    info->ws       = trx->info.ws;
    info->position = trx->info.position;
    info->state    = trx->info.state;


    GU_DBUG_VOID_RETURN;
}
