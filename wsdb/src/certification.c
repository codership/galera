// Copyright (C) 2007 Codership Oy <info@codership.com>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "galera.h"
#include "wsdb_priv.h"
#include "hash.h"
#include "version_file.h"

/* index for table row level keys */
static struct wsdb_hash *key_index;

/* index for table level locks */
static struct wsdb_hash *table_index;

struct trx_hdr {
    trx_seqno_t trx_seqno;
};

/* certification index record key->seqno */
struct index_rec {
    trx_seqno_t trx_seqno;
};

/* persistent storage for certified write sets */
static struct wsdb_file *cert_trx_file;

/* list of cert index records kept in memory before purging */
trx_seqno_t purged_up_to;

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

static void add_active_seqno(
    trx_seqno_t seqno, uint32_t keys_count, char *keys
) {
    struct seqno_list *seqno_elem = 
        (struct seqno_list *)gu_malloc(sizeof(struct seqno_list));

    seqno_elem->seqno = seqno;
    seqno_elem->key_count = keys_count;
    seqno_elem->keys = keys;
    seqno_elem->next = NULL;
    if (trx_info.last_active_seqno) {
        trx_info.last_active_seqno->next = seqno_elem;
    } else {
      trx_info.active_seqnos = seqno_elem;
    }
    trx_info.last_active_seqno = seqno_elem;
    trx_info.list_len++;
    trx_info.list_size += sizeof(struct seqno_list);
    if (keys) trx_info.list_size += *(uint32_t *)keys;
}

static void purge_active_seqnos(trx_seqno_t up_to) {
    struct seqno_list *trx = trx_info.active_seqnos;
    while (trx && trx->seqno < up_to) {
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
  
                struct index_rec *match = (struct index_rec *)wsdb_hash_search(
                    key_index, key_len, keys
                );

                if (!match) {
                    gu_error("cert index was not found during purging");
                    gu_error("seqno: %llu, key: %d, len: %d, key_count: %d",
                         trx->seqno, key_idx, key_len, trx->key_count
                    );
                    /* verify the match is not used by newer seqno */
                } else if (match->trx_seqno <= trx->seqno) {
                    /* sanity check */
                    if (match->trx_seqno < trx->seqno) {
                        gu_error(
                            "dangling cert index entry: %llu - %llu, key: %d",
                            match->trx_seqno, trx->seqno, key_idx
                        );
                    }
                    match = (struct index_rec *)wsdb_hash_delete(
                        key_index, key_len, keys
                    );
                    if (!match) {
                        gu_error("cert index delete failed");
                        gu_error(
                             "seqno: %llu, key: %d, len: %d, key_count: %d",
                             trx->seqno, key_idx, key_len, trx->key_count
                        );
                    } else {
                        gu_free(match);
                    }
                }
                keys += key_len;
            }
        }
        /* remove active trx holder struct */
        if (trx->keys) gu_free(trx->keys);
        gu_free(trx);
        trx = trx_info.active_seqnos = next_trx;
        trx_info.list_len--;
        trx_info.list_size -= sizeof(struct seqno_list);
        trx_info.list_size -= all_keys_len;
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
        struct seqno_list *next_trx = trx->next;

        if (trx->keys) gu_free(trx->keys);
        gu_free(trx);
        trx = trx_info.active_seqnos = next_trx;
        trx_info.list_len--;
        trx_info.list_size -= sizeof(struct seqno_list);
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
    key_index = wsdb_hash_open(64000, hash_fun, hash_cmp);
    cert_trx_file = version_file_open(
        (work_dir)  ? work_dir  : DEFAULT_WORK_DIR,
        (base_name) ? base_name : DEFAULT_CERT_FILE,
        DEFAULT_BLOCK_SIZE, DEFAULT_FILE_SIZE
    );

    /* open table level locks hash */
    table_index = wsdb_hash_open(1000, hash_fun, hash_cmp);

    return WSDB_OK;
}

int wsdb_certification_test(
    struct wsdb_write_set *ws, trx_seqno_t trx_seqno
) {
    uint32_t i;
    uint32_t all_keys_len;

    char *all_keys = ws->key_composition;
    if (!ws->key_composition) {
        (void)serialize_all_keys(&all_keys, ws);
        ws->key_composition = all_keys;
    }
    all_keys_len = (*(uint32_t *)all_keys);
    all_keys +=4;

    /* certification test */
    for (i = 0; i < ws->item_count; i++) {
        struct wsdb_item_rec *item = &ws->items[i];
        struct index_rec *match;

        uint16_t full_key_len;
        char * full_key;

        full_key_len = (*(uint16_t *)all_keys);
        all_keys += 2;
        full_key = all_keys;

         /* check first against table level locks */
        match = (struct index_rec *) wsdb_hash_search (
            table_index, item->key->dbtable_len, item->key->dbtable
        );
        if (match && match->trx_seqno > ws->last_seen_trx && 
                match->trx_seqno < trx_seqno
        ) {
            GU_DBUG_PRINT("wsdb",
                ("trx: %llu conflicting table lock: %llu",
		    (unsigned long long)trx_seqno, match->trx_seqno)
            );
            return WSDB_CERTIFICATION_FAIL;
        }

        /* continue with row level lock checks */
        match = (struct index_rec *)wsdb_hash_search(
            key_index, full_key_len, full_key
        );
        all_keys += full_key_len;

        if (match && match->trx_seqno > ws->last_seen_trx && 
                match->trx_seqno < trx_seqno
        ) {
            GU_DBUG_PRINT("wsdb",
                   ("trx: %llu conflicting: %llu", trx_seqno, match->trx_seqno)
            );
            return WSDB_CERTIFICATION_FAIL;
        }
    }
    return WSDB_OK;
}

static int update_index(
    struct wsdb_write_set *ws, trx_seqno_t trx_seqno
) {
    uint32_t i;
    char *all_keys        = ws->key_composition;
    //uint32_t all_keys_len = (*(uint32_t *)all_keys);
    all_keys += 4;

    for (i = 0; i < ws->item_count; i++) {
        struct index_rec *match, *new_trx = NULL;
        
        uint16_t key_len = (*(uint16_t *)all_keys);
        all_keys += 2;
        
        match = (struct index_rec *)wsdb_hash_search(
            key_index, key_len, all_keys
        );

        if (!match) {
            int rcode;
            new_trx = (struct index_rec *) gu_malloc (sizeof(struct index_rec));
            new_trx->trx_seqno = trx_seqno;
            rcode = wsdb_hash_push(
                key_index, key_len, all_keys, (void *)new_trx
            );
            if (rcode) {
              gu_error("cert index push failed: %d", rcode);
            }

        } else {
          //gu_info("cert index re-used for: %llu, key: %d, old seqno: %llu", 
          //          trx_seqno, i, match->trx_seqno);
            match->trx_seqno = trx_seqno;
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
static int write_to_file(struct wsdb_write_set *ws, trx_seqno_t trx_seqno) {
    uint32_t i;
    struct trx_hdr trx;
    char rec_type = REC_TYPE_TRX;
    uint16_t len = sizeof(struct trx_hdr);
    
    trx.trx_seqno = trx_seqno;
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

int wsdb_append_write_set(trx_seqno_t trx_seqno, struct wsdb_write_set *ws) {
    int rcode;
    my_bool *persistency = (my_bool *)wsdb_conf_get_param(
        GALERA_CONF_WS_PERSISTENCY, GALERA_TYPE_INT
    );
    uint32_t key_size;

    /* certification test */
    rcode = wsdb_certification_test(ws, trx_seqno); 
    if (rcode) {
        gu_free(ws->key_composition);
        ws->key_composition = NULL;
        return rcode;
    }

    if (persistency && *persistency) {
        gu_debug("writing trx WS in file");
        /* append write set */
        write_to_file(ws, trx_seqno);
    }

    /* update index */
    rcode = update_index(ws, trx_seqno); 
    if (rcode) {
        gu_free(ws->key_composition);
        ws->key_composition = NULL;
        return rcode;
    }

    /* mem usage test */
    key_size = *(uint32_t *)ws->key_composition;

    if (key_size > 50000) {
        gu_info("key length: %lu for trx: %llu, not stored in RAM", 
                key_size, trx_seqno
        );
        gu_free(ws->key_composition);
        ws->key_composition = NULL;
    }

    /* remember the serialized keys */
    add_active_seqno(trx_seqno, ws->item_count, ws->key_composition);

    return WSDB_OK;
}

/*
 * @brief: determine if trx can be purged
 * @return 1 if purging is ok, otherwise 0
 */
static int delete_verdict(void *ctx, void *data, void **new_data) {
    struct index_rec *entry = (struct index_rec *)data;

    if (entry && entry->trx_seqno < (*(trx_seqno_t *)ctx)) {
        *new_data= (void *)NULL;
        gu_free(entry);
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

int wsdb_purge_trxs_upto(trx_seqno_t trx_id) {
    switch(choose_purge_method(trx_id)) {
    case PURGE_METHOD_FULL_SCAN:
        purge_seqno_list(trx_id);
        purge_seqnos_by_scan(trx_id);
        break;
    case PURGE_METHOD_BY_KEYS:
        purge_active_seqnos(trx_id);
        break;
    }

    return 0;
}

/*
 * @brief: free all entries
 */
static int shutdown_verdict(void *ctx, void *data, void **new_data) {
    struct index_rec *entry = (struct index_rec *)data;

    /* find among the entries for this key, the one with last seqno */
    if (entry) {
        gu_free(entry);
    }
    return 1;
}
int wsdb_cert_close() {
    int deleted;

    uint32_t mem_usage;
    mem_usage = wsdb_hash_report(table_index);
    gu_info("mem usage for table_idex: %u", mem_usage);
    mem_usage = wsdb_hash_report(key_index);
    gu_info("mem usage for key_idex: %u", mem_usage);

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

    purge_seqno_list(GALERA_ABORT_SEQNO);

    return WSDB_OK;
}

/* @todo: this function seems to be obsolete */
int wsdb_delete_global_trx(trx_seqno_t trx_seqno) 
{

    return WSDB_OK;
}

