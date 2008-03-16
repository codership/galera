// Copyright (C) 2007 Codership Oy <info@codership.com>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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

struct index_rec {
    struct index_rec *next;
    trx_seqno_t trx_seqno;
};

static struct wsdb_file *cert_trx_file;

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
    uint16_t i;
    
    /* certification test */
    for (i = 0; i < ws->item_count; i++) {
        struct wsdb_item_rec *item = &ws->items[i];
        struct index_rec *match;
        char *serial_key = NULL;
        uint16_t key_len;
        
        /* check first against table level locks */
        match = (struct index_rec *) wsdb_hash_search (
            table_index, item->key->dbtable_len, item->key->dbtable
        );
        while (match) {
            if (match->trx_seqno > ws->last_seen_trx && 
                match->trx_seqno < trx_seqno
            ) {
                GU_DBUG_PRINT("wsdb",
                   ("trx: %llu conflicting table lock: %llu",
		    (unsigned long long)trx_seqno, match->trx_seqno)
		);
                return WSDB_CERTIFICATION_FAIL;
            }
            match = match->next;
        }

        /* continue with row level lock checks */

        /* convert key to serial order for hashing */
        key_len = serialize_full_key(&serial_key, item->key);

        match = (struct index_rec *)wsdb_hash_search(
            key_index, key_len, serial_key
        );
        gu_free(serial_key);

        while (match) {
            if (match->trx_seqno > ws->last_seen_trx && 
                match->trx_seqno < trx_seqno
            ) {
                GU_DBUG_PRINT("wsdb",
                   ("trx: %llu conflicting: %llu", trx_seqno, match->trx_seqno)
                );
                return WSDB_CERTIFICATION_FAIL;
            }
            match = match->next;
        }
    }
    return WSDB_OK;
}

static int update_index(struct wsdb_write_set *ws, trx_seqno_t trx_seqno) {
    uint16_t i;
    
    for (i = 0; i < ws->item_count; i++) {
        struct wsdb_item_rec *item = &ws->items[i];
        char *serial_key;
        struct index_rec *match, *prev, *new_trx = NULL;
        uint16_t key_len;
        
        key_len = serialize_full_key(&serial_key, item->key);
        
        prev = match = (struct index_rec *)wsdb_hash_search(
            key_index, key_len, serial_key
        );
        while (match && match->trx_seqno < trx_seqno) {
            prev = match;
            match = match->next;
        }
        if (match && match->trx_seqno == trx_seqno) {
	  gu_warn ("duplicate index entry: %llu", trx_seqno);
          //gu_free(serial_key);
          //return WSDB_OK;
	} else {
          new_trx = (struct index_rec *) gu_malloc (sizeof(struct index_rec));
          new_trx->trx_seqno = trx_seqno;
          if (prev) {
            prev->next = new_trx;
            new_trx->next = match;
          } else {
            new_trx->next = NULL;
            wsdb_hash_push(key_index, key_len, serial_key, (void *)new_trx);
          }
        }
        gu_free(serial_key);
    }
    return WSDB_OK;
}
static int write_to_file(struct wsdb_write_set *ws, trx_seqno_t trx_seqno) {
    uint16_t i;
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
          
    /* certification test */
    rcode = wsdb_certification_test(ws, trx_seqno); 
    if (rcode) {
        return rcode;
    }
    /* append write set */
    write_to_file(ws, trx_seqno);
      
    /* update index */
    rcode = update_index(ws, trx_seqno); 
    if (rcode) {
        return rcode;
    }
    return WSDB_OK;
}

int wsdb_delete_global_trx( trx_seqno_t trx_seqno ) {
    return WSDB_OK;
}
