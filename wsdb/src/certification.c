// Copyright (C) 2007 Codership Oy <info@codership.com>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "wsdb_priv.h"
#include "hash.h"
#include "version_file.h"
#define USE_MEMPOOL

#define WSDB_WORKAROUND_197

/* index for table row level keys */
static struct wsdb_hash *key_index;

/* index for table level locks */
static struct wsdb_hash *table_index;
#ifdef USE_MEMPOOL
/* mempool for index_rec and seqno_list allocations */
static struct mempool *index_pool;
#endif
struct trx_hdr {
    trx_seqno_t trx_seqno;
};

/* certification index record key->seqno */
struct index_rec {
    trx_seqno_t trx_seqno;
};

/* persistent storage for certified write sets */
static struct wsdb_file *cert_trx_file;

#ifdef WSDB_WORKAROUND_197
static trx_seqno_t purged_up_to = 0;
#endif

struct seqno_list {
    trx_seqno_t        seqno;
    uint32_t           key_count;
    char              *keys;
    struct seqno_list *next;
};

struct {
    struct seqno_list *active_seqnos;
    struct seqno_list *last_active_seqno;
    uint32_t list_len;
    uint32_t list_size;
} trx_info;

static struct seqno_list *add_active_seqno(
    trx_seqno_t seqno, uint32_t keys_count, char *keys
) {
    struct seqno_list *seqno_elem = 
#ifdef USE_MEMPOOL
      (struct seqno_list *)mempool_alloc(index_pool, sizeof(struct seqno_list));
#else
      (struct seqno_list *)gu_malloc(sizeof(struct seqno_list));
#endif
    seqno_elem->seqno     = seqno;
    seqno_elem->key_count = keys_count;
    seqno_elem->keys      = keys;
    seqno_elem->next      = NULL;
    if (trx_info.last_active_seqno) {
        trx_info.last_active_seqno->next = seqno_elem;
    } else {
      trx_info.active_seqnos = seqno_elem;
    }
    trx_info.last_active_seqno = seqno_elem;
    trx_info.list_len++;
    trx_info.list_size += sizeof(struct seqno_list);
    if (keys) trx_info.list_size += *(uint32_t *)keys;

    return seqno_elem;
}

static void purge_active_seqnos(trx_seqno_t up_to) {
    struct seqno_list *trx = trx_info.active_seqnos;
    while (trx && trx->seqno < up_to) {
#ifdef USE_MEMPOOL
        int rcode = 0;
#endif
        struct seqno_list *next_trx = trx->next;
        char *keys = trx->keys;
        /* length of all keys */
        uint32_t all_keys_len = 0;

        /* key composition is not always present */
        if (keys) {
            all_keys_len = (*(uint32_t *)keys);
            keys += 4;

            /* remove each key from cert index */
            uint32_t key_idx;
            for (key_idx=0; key_idx<trx->key_count; key_idx++) {
                /* length of this key */
                uint16_t key_len = (*(uint16_t *)keys);
                keys += 2;
  
                struct seqno_list *match = (struct seqno_list*)wsdb_hash_search(
                    key_index, key_len, keys
                );

                if (!match) {
                    gu_debug("cert index was not found during purging");
                    gu_debug("seqno: %llu, key: %d, len: %d, key_count: %d",
                         trx->seqno, key_idx, key_len, trx->key_count
                    );
                    /* verify the match is not used by newer seqno */
                } else if (match->seqno <= trx->seqno) {
                    /* sanity check */
                    if (match->seqno < trx->seqno) {
                        gu_error(
                            "dangling cert index entry: %llu - %llu, key: %d",
                            match->seqno, trx->seqno, key_idx
                        );
                    }

                    match = (struct seqno_list *)wsdb_hash_delete(
                        key_index, key_len, keys
                    );
                    if (!match) {
                        gu_error("cert index delete failed");
                        gu_error(
                             "seqno: %llu, key: %d, len: %d, key_count: %d",
                             trx->seqno, key_idx, key_len, trx->key_count
                        );
                    } else {
#ifndef USE_MEMPOOL
                        /* cannot free here, key is in key_composition */
                        //gu_free(match);
#else
                        //rcode = mempool_free(index_pool, match);
                        if (rcode) {
                            gu_error("cert index free failed: %d", rcode);
                        }
#endif
                    }
                }
                keys += key_len;
            }
        }
        /* remove active trx holder struct */
        if (trx->keys) { gu_free(trx->keys); trx->keys = NULL; }

#ifndef USE_MEMPOOL
        gu_free(trx);
#else
        rcode = mempool_free(index_pool, trx);
        if (rcode) {
            gu_error("cert seqno list free failed: %d", rcode);
        }
#endif
        trx = trx_info.active_seqnos = next_trx;
        trx_info.list_len--;
        trx_info.list_size -= sizeof(struct seqno_list);
        trx_info.list_size -= all_keys_len;
    }

    if (trx == NULL) {
        /* Reached the end of list */
        trx_info.last_active_seqno = NULL;
    }
}

enum purge_method {
  PURGE_METHOD_FULL_SCAN=0,
  PURGE_METHOD_BY_KEYS,
};

static enum purge_method choose_purge_method(trx_seqno_t up_to) {
    struct seqno_list *trx = trx_info.active_seqnos;
    while (trx && trx->seqno < up_to) {
        if (trx->keys == NULL) return PURGE_METHOD_FULL_SCAN;
        trx = trx->next;
    }
    return PURGE_METHOD_BY_KEYS;
}
static void purge_seqno_list(trx_seqno_t up_to) {
    struct seqno_list *trx = trx_info.active_seqnos;
    while (trx && trx->seqno < up_to) {
#ifdef USE_MEMPOOL
        int rcode;
#endif
        struct seqno_list *next_trx = trx->next;

        if (trx->keys) { gu_free(trx->keys); trx->keys = NULL; }
#ifndef USE_MEMPOOL
        gu_free(trx);
#else
        rcode = mempool_free(index_pool, trx);
        if (rcode) {
            gu_error("cert seqno list free failed: %d", rcode);
        }
#endif
        trx = trx_info.active_seqnos = next_trx;
        trx_info.list_len--;
        trx_info.list_size -= sizeof(struct seqno_list);
    }

    if (trx == NULL) {
        /* Reached the end of list */
        trx_info.last_active_seqno = NULL;
    }
}

/* djb2
 * This algorithm was first reported by Dan Bernstein
 * many years ago in comp.lang.c
 */
static uint32_t hash_fun(uint32_t max_size, uint16_t len, char *str) {
    uint32_t prime = 5381;
    int c; 
    while (len-- > 0) {
        c = *str++;
        prime = ((prime << 5) + prime) + c; // prime*33 + c
    }
    return prime % max_size;
}

static int hash_cmp(uint16_t len1, char *key1, uint16_t len2, char *key2) {
    uint16_t i;
    if (len1 < len2) {
        return -1;
    } else if (len1 > len2) {
        return 1;
    }
    for(i=0; i<len1; i++) {
        if (key1[i] < key2[i]) {
            return -1;
        } else if (key1[i] > key2[i]) {
            return 1;
        }
    }
    return 0;
}

int wsdb_cert_init(const char* work_dir, const char* base_name) {
    /* open row level locks hash */

    /* tell hash to reuse key_composition as key values */
    key_index = wsdb_hash_open(262144, hash_fun, hash_cmp, true, true);
    cert_trx_file = version_file_open(
        (work_dir)  ? work_dir  : DEFAULT_WORK_DIR,
        (base_name) ? base_name : DEFAULT_CERT_FILE,
        DEFAULT_BLOCK_SIZE, DEFAULT_FILE_SIZE
    );

    /* open table level locks hash */
    table_index = wsdb_hash_open(1000, hash_fun, hash_cmp, true, false);
    
#ifdef USE_MEMPOOL
    index_pool = mempool_create(
        (sizeof(struct index_rec) > sizeof(struct seqno_list)) ?
         sizeof(struct index_rec) : sizeof(struct seqno_list),
        32000,
        MEMPOOL_DYNAMIC, false, "cert index"
    );
#endif
    return WSDB_OK;
}

int wsdb_certification_test(
    struct wsdb_write_set *ws, bool_t save_keys
) {
    uint32_t i;
    uint32_t all_keys_len;
    char *all_keys = ws->key_composition;

#ifdef WSDB_WORKAROUND_197
    if (gu_unlikely(ws->last_seen_trx < purged_up_to)) {
        gu_warn ("WS last_seen: %lld is below certification bound: %lld",
                 ws->last_seen_trx, purged_up_to);
        return WSDB_CERTIFICATION_FAIL;
    }
#endif

    if (!ws->key_composition) {
        (void)serialize_all_keys(&all_keys, ws);
        ws->key_composition = all_keys;
    }
    all_keys_len = (*(uint32_t *)all_keys);
    all_keys +=4;

    /* certification test */
    for (i = 0; i < ws->item_count; i++) {
        struct wsdb_item_rec *item = &ws->items[i];
        struct seqno_list *match;

        uint16_t full_key_len;
        char * full_key;

        full_key_len = (*(uint16_t *)all_keys);
        all_keys += 2;
        full_key = all_keys;

         /* check first against table level locks */
        match = (struct seqno_list *) wsdb_hash_search (
            table_index, item->key->dbtable_len, item->key->dbtable
        );
        if (match && match->seqno > ws->last_seen_trx && 
            match->seqno < ws->trx_seqno
        ) {
            GU_DBUG_PRINT("wsdb",
                ("trx: %lld conflicting table lock: %llu",
                 ws->trx_seqno, match->seqno)
            );

            if (!save_keys) {
                /* key composition is not needed anymore */
                gu_free(ws->key_composition);
                ws->key_composition = NULL;

                return WSDB_CERTIFICATION_FAIL;
            }
        }

        /* continue with row level lock checks */
        match = (struct seqno_list *)wsdb_hash_search(
            key_index, full_key_len, full_key
        );
        all_keys += full_key_len;

        if (match && match->seqno > ws->last_seen_trx && 
                match->seqno < ws->trx_seqno
        ) {
            GU_DBUG_PRINT("wsdb",
                   ("trx: %lld conflicting: %llu", ws->trx_seqno, match->seqno)
            );
            /* key composition is not needed anymore */
            gu_free(ws->key_composition);
            ws->key_composition = NULL;

            return WSDB_CERTIFICATION_FAIL;
        }
    }
    return WSDB_OK;
}

static int update_index(
    struct wsdb_write_set *ws, struct seqno_list *seqno_elem
) {
    uint32_t i;
    char *all_keys        = ws->key_composition;
    //uint32_t all_keys_len = (*(uint32_t *)all_keys);
    all_keys += 4;

    for (i = 0; i < ws->item_count; i++) {
        int rcode;
        uint16_t key_len = (*(uint16_t *)all_keys);
        all_keys += 2;
        

        /* push or replace, if duplicate was found */
        rcode = wsdb_hash_push_replace(
           key_index, key_len, all_keys, (void *)seqno_elem
        );

        if (rcode) {
            gu_error("cert index push failed: %d, key: %d", rcode, key_len);
            return WSDB_CERT_UPDATE_FAIL;
        }

        all_keys += key_len;
    }
    return WSDB_OK;
}

/* 
 * @brief: write certified write set permanently in file
 * @param: ws the write set to write
 * @param: trx_seqno seqno for the transaction
 * @returns: success code for the write operation
 */
static int write_to_file(struct wsdb_write_set *ws) {
    uint32_t i;
    struct trx_hdr trx;
    char rec_type = REC_TYPE_TRX;
    uint16_t len = sizeof(struct trx_hdr);

    trx.trx_seqno = ws->trx_seqno;
    /* store trx header */
    cert_trx_file->append_block(cert_trx_file, 1,   (char *)&rec_type);
    cert_trx_file->append_block(cert_trx_file, 2,   (char *)&len);
    cert_trx_file->append_block(cert_trx_file, len, (char *)&trx);

    /* store queries */
    for (i = 0; i < ws->query_count; i++) {
        rec_type = REC_TYPE_QUERY;
        len = ws->queries[i].query_len;
        //len = strlen(ws->queries[i]);
        cert_trx_file->append_block(cert_trx_file, 1,   (char *)&rec_type);
        cert_trx_file->append_block(cert_trx_file, 2,   (char *)&len);
        cert_trx_file->append_block(cert_trx_file, len, ws->queries[i].query);
    }

    /* store ws items */
    for (i = 0; i < ws->item_count; i++) {
        struct wsdb_item_rec *item = &ws->items[i];
        struct file_row_key *row_key;
        uint16_t j; 

        /* first action identifier */
        rec_type = REC_TYPE_ACTION;
        len = 1;
        cert_trx_file->append_block(cert_trx_file, 1,   (char *)&rec_type);
        cert_trx_file->append_block(cert_trx_file, 2,   (char *)&len);
        cert_trx_file->append_block(cert_trx_file, len, (char *)&item->action);

        /* then the key specification */
        row_key = wsdb_key_2_file_row_key(item->key);
        rec_type = REC_TYPE_ROW_KEY;
        len = row_key->key_len + sizeof(struct file_row_key);
        cert_trx_file->append_block(cert_trx_file, 1,   (char *)&rec_type);
        cert_trx_file->append_block(cert_trx_file, 2,   (char *)&len);
        cert_trx_file->append_block(cert_trx_file, len, (char *)row_key);
        gu_free (row_key);

        /* then data, if available */
        rec_type = REC_TYPE_ROW_DATA;
        switch (item->data_mode) {
        case NO_DATA: /* row data has not been stored */
            break;
        case COLUMN:
            for (j=0; j<item->u.cols.col_count; j++) {
              struct wsdb_col_data_rec *data = &item->u.cols.data[j];
              len = sizeof(struct wsdb_col_data_rec) + item->u.cols.data->length;
              cert_trx_file->append_block(cert_trx_file, 1,(char *)&rec_type);
              cert_trx_file->append_block(cert_trx_file, 2, (char *)&len);
              cert_trx_file->append_block(cert_trx_file, len, (char *)data);
            }
            break;
        case ROW:
          {
            struct wsdb_row_data_rec *data = &item->u.row;
            len = sizeof(struct wsdb_row_data_rec) + item->u.row.length;
            cert_trx_file->append_block(cert_trx_file, 1,(char *)&rec_type);
            cert_trx_file->append_block(cert_trx_file, 2, (char *)&len);
            cert_trx_file->append_block(cert_trx_file, len, (char *)data);
          }
        }
    }
    return WSDB_OK;
}

int wsdb_append_write_set(struct wsdb_write_set *ws) {
    int rcode;
    my_bool *persistency = (my_bool *)wsdb_conf_get_param(
        WSDB_CONF_WS_PERSISTENCY, WSDB_TYPE_INT
    );
    //uint32_t key_size;
    struct seqno_list *seqno_elem = NULL;

    /* certification test */
    rcode = wsdb_certification_test(ws, false); 
    if (rcode) {
        return rcode;
    }

    if (persistency && *persistency) {
        gu_debug("writing trx WS in file");
        /* append write set */
        write_to_file(ws);
    }

    /* add trx in the active trx list */
    seqno_elem = 
      add_active_seqno(ws->trx_seqno, ws->item_count, ws->key_composition);

    /* update index */
    rcode = update_index(ws, seqno_elem); 
    if (rcode) {
        gu_warn("certified WS failed to update cert index, "
                "rcode: %d trx: %lld", rcode, ws->trx_seqno);
        gu_free(ws->key_composition);
        ws->key_composition = NULL;
        return rcode;
    }
#ifdef REMOVED
    /* mem usage test */
    key_size = *(uint32_t *)ws->key_composition;

    //if (key_size > 50000) {
    if (key_size >= 100000) {
        gu_debug("key length: %lu for trx: %lld, not stored in RAM", 
                key_size, ws->trx_seqno
        );
        gu_free(ws->key_composition);
        ws->key_composition = NULL;
    }
#endif
    /* remember the serialized keys */
    //add_active_seqno(trx_seqno, ws->item_count, ws->key_composition);

    return WSDB_OK;
}

/*
 * @brief: determine if trx can be purged
 * @return 1 if purging is ok, otherwise 0
 */
static int delete_verdict(void *ctx, void *data, void **new_data) {
    struct seqno_list *entry = (struct seqno_list *)data;

    if (!entry) {
        gu_warn("hash delete_verdict has null entry pointer");
    }
    if (entry && entry->seqno < (*(trx_seqno_t *)ctx)) {
#ifdef USE_MEMPOOL
        int rcode = 0;
#endif
        *new_data= (void *)NULL;
#ifndef USE_MEMPOOL
        /* cannot free here, key_index references keys until purge time */
        //gu_free(entry);
#else
        //rcode = mempool_free(index_pool, entry);
        if (rcode) {
            gu_error("cert index free failed (delete_verdict): %d", rcode);
        }
#endif
        return 1;
    }

    return 0;
}
int purge_seqnos_by_scan(trx_seqno_t trx_id) {
    int deleted;

    /* purge entries from table index */
    deleted = wsdb_hash_delete_range(
        table_index, (void *)&trx_id, delete_verdict
    );
    gu_debug("purged %d entries from table index", deleted);

    /* purge entries from row index */
    deleted = wsdb_hash_delete_range(
        key_index, (void *)&trx_id, delete_verdict
    );
    gu_debug("purged %d entries from key index, up-to: %lu", deleted, trx_id);

    return WSDB_OK;
}

#include <malloc.h>
//#define DEBUG_REPORT
int wsdb_purge_trxs_upto(trx_seqno_t trx_id) {
#ifdef DEBUG_REPORT
    int mem_usage = wsdb_hash_report(key_index);
    struct mallinfo mi;

    gu_info("before PURGE, mem usage for key_idex: %u", mem_usage);

    gu_info("active seqno list len: %d, size: %d", 
            trx_info.list_len, trx_info.list_size
    );

    mi = mallinfo();
    gu_info("arena: %d ordblks: %d uordblks: %d fordblks: %d keepcost: %d  hblks: %d  hblkhd: %d",
            mi.arena, mi.ordblks, mi.uordblks, mi.fordblks, mi.keepcost, mi.hblks, mi.hblkhd);
#endif
    switch(choose_purge_method(trx_id)) {
    case PURGE_METHOD_FULL_SCAN:
        purge_seqno_list(trx_id);
        purge_seqnos_by_scan(trx_id);
        break;
    case PURGE_METHOD_BY_KEYS:
        purge_active_seqnos(trx_id);
        break;
    }
#ifdef DEBUG_REPORT
    mem_usage = wsdb_hash_report(key_index);
    gu_info("after PURGE, mem usage for key_idex: %u", mem_usage);

    gu_info("active seqno list len: %d, size: %d", 
            trx_info.list_len, trx_info.list_size
    );
#endif
#ifdef WSDB_WORKAROUND_197
    if (trx_id > purged_up_to) purged_up_to = trx_id;
#endif
    return 0;
}

/*
 * @brief: free all entries
 */
static int shutdown_verdict(void *ctx, void *data, void **new_data) {
    struct seqno_list *entry = (struct seqno_list *)data;

    /* find among the entries for this key, the one with last seqno */
    if (entry) {
#ifdef USE_MEMPOOL
        int rcode = 0;
#endif
#ifndef USE_MEMPOOL
        /* cannot free here, key_index references keys until purge time */
        //gu_free(entry);
#else
        //rcode = mempool_free(index_pool, entry);
        if (rcode) {
            gu_error("cert index free failed (shutdown_verdict): %d", rcode);
        }
#endif
    }
    return 1;
}
int wsdb_cert_close() {
    int deleted;

    uint32_t mem_usage;
    mem_usage = wsdb_hash_report(table_index);
    gu_info("mem usage for table_index: %u", mem_usage);
    mem_usage = wsdb_hash_report(key_index);
    gu_info("mem usage for key_index: %u", mem_usage);

    gu_info("active seqno list len: %d, size: %d", 
            trx_info.list_len, trx_info.list_size
    );

    /* purge entries from table index */
    deleted = wsdb_hash_delete_range(
        table_index, NULL, shutdown_verdict
    );
    gu_info("purged %d entries from table index", deleted);

    /* purge entries from row index */
    deleted = wsdb_hash_delete_range(
        key_index, NULL, shutdown_verdict
    );
    gu_info("purged %d entries from key index", deleted);

    /* close hashes, they are empty now */
    if (wsdb_hash_close(table_index)) {
        gu_error("failed to close table index");
    }

    if (wsdb_hash_close(key_index)) {
        gu_error("failed to close key index");
    }

    purge_seqno_list(LLONG_MAX);

#ifdef USE_MEMPOOL
    mem_usage = mempool_report(index_pool, true);
    gu_info("mempool usage for index: %u", mem_usage);
    mempool_close(index_pool);
#endif
    return WSDB_OK;
}

void wsdb_get_trx_info(trx_seqno_t trx_seqno, wsdb_trx_info_t* info)
{
    abort();
}

/* @todo: this function seems to be obsolete */
int wsdb_delete_global_trx(trx_seqno_t trx_seqno) 
{

    return WSDB_OK;
}

