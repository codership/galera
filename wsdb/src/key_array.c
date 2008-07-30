// Copyright (C) 2007 Codership Oy <info@codership.com>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "wsdb_priv.h"
#include "key_array.h"

struct key_array_entry {
    struct key_array_entry *next;
    char                   *key;
    char                   *data;
};

#ifdef REMOVED
static int key_array_cmp(
    struct key_array_entry *e1,
    struct key_array_entry *e2
) {
    int cmp = strcmp(e1->key, e2->key);
    if (cmp < 0)      return -1;
    else if (cmp > 0) return 1;
    else              return 0;
}
#endif
static int key_array_cmp(
    char *key1, char *key2, uint16_t len
) {
    int cmp = strncmp(key1, key2, (size_t)len);
    if (cmp < 0)      return -1;
    else if (cmp > 0) return  1;
    else              return  0;
}

void key_array_open(
    struct key_array *array
) {
    array->elem_count = 0;
    array->elems      = NULL;
}

int key_array_close(struct key_array *array) {
    uint32_t elem_count = 0;
    struct key_array_entry *entry;
    
    if (!array) {
        gu_error("Empty key array size on delete");
        return WSDB_ERR_ARRAY_EMPTY;
    }

    entry = array->elems;

    while (entry) {
        gu_free(entry->key);
        gu_free(entry->data);

        entry = entry->next;
        elem_count++;
    }

    if (elem_count != array->elem_count) {
        gu_error("Bad key array size on delete: %d-%d",
                 elem_count, array->elem_count);
        return WSDB_ERR_ARRAY_SIZE;
    }

    return WSDB_OK;
}

static struct key_array_entry *create_key_array_entry(
    char *key,  uint16_t key_len,
    char *data, uint16_t data_len
) {
    struct key_array_entry *entry = (struct key_array_entry *) gu_malloc (
        sizeof(struct key_array_entry)
    );
    
    entry->key = (char *) gu_malloc (key_len + 1);
    strncpy(entry->key, key, key_len);
    entry->key[key_len] = '\0';

    entry->data = (char *) gu_malloc (data_len + 1);
    strncpy(entry->data, data, data_len);
    entry->data[data_len] = '\0';

    entry->next = NULL;

    return entry;
}
int key_array_replace(
    struct key_array *array, char *key, uint16_t key_len,
    char *data, uint16_t data_len
) {
    struct key_array_entry *e, *entry, *prev;

    GU_DBUG_ENTER("key_array_entry_replace");
    e     = array->elems;
    prev  = NULL;
    entry = NULL;
    while (e && !entry) {
        switch(key_array_cmp(key, e->key, key_len)) {
        case -1:
            prev = e;
            e    = e->next;
            break;
        case 0:
            entry = e;
        case 1:
            e = NULL;
        }
    }

    if (entry) {
        gu_free(entry->data);
        entry->data = (char *) gu_malloc (data_len + 1);
        strncpy(entry->data, data, data_len);
        entry->data[data_len] = '\0';
    } else {
        entry = create_key_array_entry(key, key_len, data, data_len);
        array->elem_count++;
        if (prev) {
            entry->next = prev->next;
            prev->next  = entry;
        } else {
            entry->next  = array->elems;
            array->elems = entry;
        }
    }

    GU_DBUG_RETURN(WSDB_OK);
}

int key_array_delete_entry(
    struct key_array *array, char *key, uint16_t key_len
) {
    GU_DBUG_ENTER("key_array_delete_entry");

    struct key_array_entry *e, *entry, *prev;

    e     = array->elems;
    prev  = NULL;
    entry = NULL;
    while (e && !entry) {
        switch(key_array_cmp(key, e->key, key_len)) {
        case -1:
            prev = e;
            e    = e->next;
            break;
        case 0:
            entry = e;
        case 1:
            e = NULL;
        }
    }
    if (entry) {
        if (prev) {
            prev->next = entry->next;
        } else {
            array->elems = entry->next;
        }
        gu_free(entry->key);
        gu_free(entry->data);
        array->elem_count--;
    }
    
    GU_DBUG_RETURN(WSDB_OK);
}

int key_array_scan_entries(
    struct key_array *array, key_array_fun_t handler, void *context
) {
    struct key_array_entry *e;

    GU_DBUG_ENTER("key_array_scan_entries");
    e = array->elems;
    while (e) {
        if(handler(context, e->key, e->data)) {
            GU_DBUG_RETURN(WSDB_ERR_ARRAY_FAIL);
        }
        e = e->next;
    }
    GU_DBUG_RETURN(WSDB_OK);
}

int key_array_get_size(struct key_array *array) {
    return array->elem_count;
}
