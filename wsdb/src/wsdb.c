// Copyright (C) 2007 Codership Oy <info@codership.com>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "wsdb_priv.h"
#include "hash.h"

static struct wsdb_hash *table_name_hash;
static uint32_t s_last_table_id;
static uint32_t s_max_table_id;

static wsdb_conf_param_fun     wsdb_configurator = NULL;

/* djb2
 * This algorithm was first reported by Dan Bernstein
 * many years ago in comp.lang.c
 */
static uint32_t hash_fun(uint32_t max_size, uint16_t len, char *str) {
    uint16_t hash = 5381;
    int c; 
    while (len-- > 0) {
        c = *str++;
        hash = ((hash << 5) + hash) + c; // hash*33 + c
    }
    return hash % max_size;
}

static int hash_cmp(uint16_t len1, char *key1, uint16_t len2, char *key2) {
    uint16_t i = 0;
    while (i<len1 && i<len2) {
        if (key1[i] < key2[i]) {
            return -1;
        } else if (key1[i] > key2[i]) {
            return 1;
        }
        i++;
    }
    if (len1 < len2) {
        return -1;
    } else if (len1 > len2) {
        return 1;
    }
    return 0;
}

int wsdb_init(
    const char *data_dir, wsdb_log_cb_t logger
) {
    gu_conf_set_log_callback(logger);
    
    s_max_table_id = 64000;
    table_name_hash = wsdb_hash_open(s_max_table_id, hash_fun, hash_cmp, true);
    s_last_table_id = 1;

    /* open DB for local state trx */
    local_open(data_dir, NULL, 0, 10000);

    /* open certification database */
    wsdb_cert_init(data_dir, NULL);
    return WSDB_OK;
}

int wsdb_close() {
    gu_info("closing wsdb");
    wsdb_hash_close(table_name_hash);

    local_close();

    /* close certification indexes */
    gu_info("closing certification module");
    wsdb_cert_close();

    return WSDB_OK;
}

static void copy_ptr(char **ptr, char *data, uint16_t len) {
    memcpy(*ptr, data, len);
    *ptr += len;
}
uint16_t serialize_key_do(
    char *data, uint16_t *key_len, struct wsdb_table_key *key, int do_serialize
) {
    uint16_t              i;
    char                 *ptr  = data;
    struct wsdb_key_part *part = key->key_parts;

    if (do_serialize) {
        memset(data, '\0', *key_len);
        copy_ptr(&ptr, (char *)&key->key_part_count, 2);
    } else {
        *key_len = 2;
    }
    for (i=0; i<key->key_part_count; i++) {
        if (do_serialize) {
            copy_ptr(&ptr, (char *)&part->type, 1);
            copy_ptr(&ptr, (char *)&part->length, 2); 
            copy_ptr(&ptr, part->data, part->length);
        } else {
            *key_len += 1 + 2 + part->length;
        }
        part++;
    }
    return *key_len;
}

uint16_t serialize_key(char **data, struct wsdb_table_key *key) {
    uint16_t key_len = 0;

    /* calculate space requirement for key */
    key_len = serialize_key_do(NULL, &key_len, key, 0);
    *data = (char *) gu_malloc (key_len);
    memset(*data, '\0', key_len);

    /* serialize the key in data buffer */
    key_len = serialize_key_do(*data, &key_len, key, 1);
    return key_len;
}

uint16_t serialize_full_key(char **data, struct wsdb_key_rec *key) {
    uint16_t  key_len      = 0;
    uint16_t  full_key_len = 0;
    char *ptr;

    /* calculate space requirement for key */
    key_len      = serialize_key_do(NULL, &key_len, key->key, 0);
    full_key_len = key_len + 2 + key->dbtable_len;

    *data = (char *) gu_malloc (full_key_len);
    memset(*data, '\0', full_key_len);
    ptr = *data;

    copy_ptr(&ptr, (char *)&key->dbtable_len, 2);
    copy_ptr(&ptr, (char *)key->dbtable, key->dbtable_len);

    /* serialize the key in data buffer */
    serialize_key_do(ptr, &key_len, key->key, 1);

    return full_key_len;
}

uint16_t serialize_all_keys(char **data, struct wsdb_write_set *ws) {
    uint32_t  all_keys_len = 0;
    char *ptr;

    int i;

    /* key composition will contain:
     *
     * (uint32_t) length of all keys
     * for each row key: 
     *   (uint16_t) length of full table/row key
     *   (uint16_t) length of table name
     *              the table name of promised length
     *   (uint16_t) number of key parts
     *   for each row key part:
     *       (char)     type of row key
     *       (uint16_t) length of this key part
     *                  the row key
     */
    for (i=0; i<ws->item_count; i++) {
        uint16_t  key_len      = 0;
        /* calculate space requirement for this key */
        key_len = serialize_key_do(NULL, &key_len, ws->items[i].key->key, 0);
        all_keys_len += 2 + key_len + 2 + ws->items[i].key->dbtable_len;
    }

    /* we add the full length of the key */
    all_keys_len += 4;

    *data = (char *) gu_malloc (all_keys_len);
    memset(*data, '\0', all_keys_len);
    ptr = *data;

    /* so, first the total length of all keys */
    copy_ptr(&ptr, (char *)&all_keys_len, 4);

    /* then each individual key */
    for (i=0; i<ws->item_count; i++) {
        uint16_t  key_len      = 0;
        uint16_t full_key_len;
        struct wsdb_item_rec *item = &(ws->items[i]);

        /* calculate space requirement for this key */
        key_len = serialize_key_do(NULL, &key_len, item->key->key, 0);
        full_key_len = key_len + 2 + item->key->dbtable_len;
        copy_ptr(&ptr, (char *)&(full_key_len), 2);
        copy_ptr(&ptr, (char *)&(item->key->dbtable_len), 2);
        copy_ptr(&ptr, (char *)item->key->dbtable, item->key->dbtable_len);
        
        /* serialize the key in data buffer */
        serialize_key_do(ptr, &key_len, item->key->key, 1);
        ptr += key_len;
    }

    return all_keys_len;
}

struct file_row_key *wsdb_key_2_file_row_key(struct wsdb_key_rec *key) {
    struct file_row_key *row_key;
    uint16_t             key_len;
    char                *key_data;
    key_len = serialize_key(&key_data, key->key);
    row_key = (struct file_row_key *) gu_malloc (
        sizeof(struct file_row_key) + key_len
    );
    memcpy(row_key->key, key_data, key_len);
    memset(row_key->dbtable, '\0', MAX_DBTABLE_LEN);
    memcpy(row_key->dbtable, key->dbtable, key->dbtable_len);
    row_key->key_len     = key_len;
    row_key->dbtable_len = key->dbtable_len;
    gu_free(key_data);
    return row_key;
}

struct wsdb_key_rec *file_row_key_2_wsdb_key(struct file_row_key *row_key) {
    struct wsdb_key_rec *wsdb_key;
    wsdb_key = (struct wsdb_key_rec *) gu_malloc (
        sizeof(struct wsdb_key_rec)
    );
    wsdb_key->key = inflate_key(row_key->key, row_key->key_len);
    wsdb_key->dbtable_len = row_key->dbtable_len;
    wsdb_key->dbtable     = gu_malloc (wsdb_key->dbtable_len);
    memcpy(wsdb_key->dbtable, row_key->dbtable, wsdb_key->dbtable_len);

    return wsdb_key;
}

struct wsdb_table_key *inflate_key(
    char *data, uint16_t data_len
) {
    struct wsdb_table_key *key = (struct wsdb_table_key *) gu_malloc (
        sizeof(struct wsdb_table_key)
    );
    int                   pos= 0;
    struct wsdb_key_part *key_part;
    uint16_t             *part_count= (uint16_t *)&data[pos];
    int                   key_size= *part_count * sizeof(struct wsdb_key_part);

    key->key_part_count = *part_count;
    key->key_parts      = (struct wsdb_key_part *) gu_malloc (key_size);
    memset(key->key_parts, 0, key_size);

    key_part = key->key_parts;

    /* create all the key parts */
    pos += 2;
    while (pos < data_len) {
        uint16_t *len = (uint16_t *)&data[pos+1];

        key_part->type = data[pos];
        key_part->length = *len;
        key_part->data = (void *) gu_malloc (*len);
        memcpy(key_part->data, &data[pos+3], *len);
        
        pos += 1 + 2 + *len;
        key_part++;
    }
    return key;
}

uint32_t get_table_id(char *dbtable) {
    uint32_t id = (uint32_t) (size_t) wsdb_hash_search (
        table_name_hash, strlen(dbtable), dbtable
    );

    if (!id) {
        // alex: @fixme: what about overflow?
        id = s_last_table_id++;
        if (wsdb_hash_push(
            table_name_hash, strlen(dbtable), dbtable, (void*)(size_t) id
        )) {
            return WSDB_ERROR;
        }
    }

    return id;
}

void *wsdb_conf_get_param (
    enum wsdb_conf_param_id id, enum wsdb_conf_param_type type
) {
    if (!wsdb_configurator) {
        return(NULL);
    } else {
        return(wsdb_configurator(id, type));
    }
}

void wsdb_set_conf_param_cb(
    wsdb_conf_param_fun configurator
) {
    wsdb_configurator = configurator;
}
