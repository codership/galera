// Copyright (C) 2007 Codership Oy <info@codership.com>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "wsdb_priv.h"
#include "file_cache.h"
#include "wsdb_file.h"

#define CACHE_SIZE 10000
enum cache_state {
    BLOCK_SWAPPABLE,
    BLOCK_SWAPPED,
    BLOCK_ACTIVE,
    BLOCK_DELETED,
};

struct cache_block {
    struct cache_block *next;
    void               *data;
};

#define IDENT_cache_entry 'c'
struct cache_entry {
    char                ident;
    struct cache_entry *next;
    struct cache_entry *prev;
    enum cache_state    state;
    cache_id_t          cache_id;
    file_addr_t         file_addr;
    void               *data;
};

struct cache_array {
    uint32_t            max_size;
    uint32_t            curr_size;
    struct cache_entry *entries;
};

static uint32_t hash_fun(uint32_t max_size, uint16_t len, char *key) {
    return (*(cache_id_t *)key) % max_size;
}

static int hash_cmp(uint16_t len1, char *key1, uint16_t len2, char *key2) {
    if (*(cache_id_t*)key1 < *(cache_id_t*)key2) {
        return -1; 
    } else if (*(cache_id_t*)key1 > *(cache_id_t*)key2) {
        return 1;
    } else {
        return 0;
    }
}

int file_cache_close(struct file_cache *cache) {
    CHECK_OBJ(cache, file_cache);
    wsdb_hash_close(cache->hash);
    return WSDB_OK;
}

struct file_cache *file_cache_open(
    struct wsdb_file *file, uint32_t mem_limit, uint32_t max_elems
){
    /* allocate cache info */
    struct file_cache *cache; 
    uint16_t           max_blocks;

    MAKE_OBJ(cache, file_cache);
    
    cache->file              = file;
    cache->entries_active    = NULL;
    cache->entries_swappable = NULL;
    cache->entries_swapped   = NULL;
    cache->entries_deleted   = NULL;
    cache->last_cache_id     = 0;
    cache->block_size        = file->get_block_size(file);

    max_blocks = mem_limit / cache->block_size;
    if (max_blocks == 0) {
        gu_error ("bad cache limit: 0");
        return NULL;
    }

    /* allocate hash array */
    cache->hash = wsdb_hash_open(
        (max_blocks > max_elems) ? max_blocks : max_elems, hash_fun, hash_cmp
    );
    cache->curr_size  = 0;
    cache->max_size   = (max_blocks > max_elems) ? max_blocks : max_elems;
    
    /* initialize the mutex */
    gu_mutex_init (&cache->mutex, NULL);

    return cache;
}

/* remove entry from cache entry list */
#define RM_FROM_LIST_ADDR(list, entry) { \
    if (entry->prev) {                   \
        entry->prev->next = entry->next; \
    } else {                             \
        (*list) = entry->next;           \
    }                                    \
    if (entry->next) {                   \
        entry->next->prev = entry->prev; \
    }                                    \
}
/* remove entry from cache entry list */
#define RM_FROM_LIST(list, entry)  RM_FROM_LIST_ADDR(&list, entry)

#ifdef REMOVED
/* remove entry from cache entry list */
#define RM_FROM_LIST(list, entry) {      \
    if (entry->prev) {                   \
        entry->prev->next = entry->next; \
    } else {                             \
        list = entry->next;              \
    }                                    \
    if (entry->next) {                   \
        entry->next->prev = entry->prev; \
    }                                    \
}
#endif

/* add entry in cache list */
#define ADD_TO_LIST(list, entry) {       \
    entry->prev = NULL;                  \
    entry->next = list;                  \
    if (list) {                          \
        list->prev = entry;              \
    }                                    \
    list = entry;                        \
}

static void swap_entry_from_list(
    struct file_cache *cache, struct cache_entry **list, 
    struct cache_entry *entry
) {
    struct cache_entry *swapped = *list;

    CHECK_OBJ(swapped, cache_entry);
    GU_DBUG_PRINT("wsdb",
                  ("swap: %d over %d",entry->cache_id, swapped->cache_id)
    );
    /* steal block from swapped entry */
    swapped->state = BLOCK_SWAPPED;
    entry->data    = swapped->data;

    /* write swapped to disk */
    if (swapped->file_addr) {
        GU_DBUG_PRINT("wsdb",("writing to cache file"));
        cache->file->write_block(
            cache->file, swapped->file_addr, 
            cache->block_size, swapped->data
        );
    } else {
        GU_DBUG_PRINT("wsdb",("appending to cache file"));
        swapped->file_addr = cache->file->append_block(
            cache->file, cache->block_size, swapped->data
        );
    }
    swapped->data  = NULL;

    /* remove swapped from swappable list */
    RM_FROM_LIST_ADDR(list, swapped);

    /* put swapped in swapped list */
    ADD_TO_LIST((cache->entries_swapped), swapped);
}

static void swap_entry(struct file_cache *cache, struct cache_entry *entry) {
    GU_DBUG_ENTER("swap_entry");

    if (cache->entries_deleted) {
        struct cache_entry *deleted = cache->entries_deleted;

        CHECK_OBJ(deleted, cache_entry);
        GU_DBUG_PRINT("wsdb",
            ("swap: %d over deleted %d",entry->cache_id, deleted->cache_id)
        );
        /* steal block from deleted */
        entry->data    = deleted->data;

        /* remove deleted from deleted list */
        RM_FROM_LIST((cache->entries_deleted), deleted);

        /* finally delete the entry */
        gu_free(deleted);

    } else if (cache->entries_swappable) {
#ifdef REMOVED
        struct cache_entry *swapped = cache->entries_swappable;

        CHECK_OBJ(swapped, cache_entry);
        GU_DBUG_PRINT("wsdb",
                   ("swap: %d over %d",entry->cache_id, swapped->cache_id)
        );
        /* steal block from swapped */
        swapped->state = BLOCK_SWAPPED;
        entry->data    = swapped->data;

        /* write swapped to disk */
        if (swapped->file_addr) {
            GU_DBUG_PRINT("wsdb",("writing to cache file"));
            cache->file->write_block(
                cache->file, swapped->file_addr, 
                cache->block_size, swapped->data
            );
        } else {
            GU_DBUG_PRINT("wsdb",("appending to cache file"));
            swapped->file_addr = cache->file->append_block(
                cache->file, cache->block_size, swapped->data
            );
        }
        swapped->data  = NULL;

        /* remove swapped from swappable list */
        RM_FROM_LIST((cache->entries_swappable), swapped);

        /* put swapped in swapped list */
        ADD_TO_LIST((cache->entries_swapped), swapped);
#endif
        swap_entry_from_list(cache, &cache->entries_swappable, entry);

    } else if (cache->entries_active) {
        swap_entry_from_list(cache, &cache->entries_active, entry);
    } else {

        /* cache full, need to wait */
        GU_DBUG_PRINT("wsdb",("cache full %lu ", entry->cache_id));
        assert(0);
    }
    GU_DBUG_VOID_RETURN;
}

void *file_cache_new(struct file_cache *cache, cache_id_t id) {
    struct cache_entry *entry;
    
    CHECK_OBJ(cache, file_cache);
    gu_mutex_lock(&cache->mutex);
    entry = (struct cache_entry *)wsdb_hash_search(
        cache->hash, sizeof(cache_id_t), (char *)&id
    );
    if (entry) {
        gu_error("file cache entry exists: %lu-%lu", id, entry->cache_id);
        gu_mutex_unlock(&cache->mutex);
        return NULL;
    }

    MAKE_OBJ(entry, cache_entry);
    entry->cache_id  = id;
    entry->file_addr = 0;
    entry->data      = NULL;

    wsdb_hash_push(
        cache->hash, sizeof(cache_id_t), (void *)&id, (void *)entry
    );

    /* create or reuse cache data block */
    if (cache->curr_size < cache->max_size) {
        entry->data = (void *) gu_malloc (cache->block_size);
        cache->curr_size++;
    } else {
        /* replace entry with some victim */
        swap_entry(cache, entry);
    }
    assert(entry->data);

    /* initialize entry */
    memset(entry->data, '#', cache->block_size);

    /* put entry in active list */
    entry->state   = BLOCK_ACTIVE;
    ADD_TO_LIST((cache->entries_active), entry);

    gu_mutex_unlock(&cache->mutex);

    return entry->data;
}

void *file_cache_get(struct file_cache *cache, cache_id_t id) {
    struct cache_entry *entry;

    GU_DBUG_ENTER("file_cache_get");

    CHECK_OBJ(cache, file_cache);
    gu_mutex_lock(&cache->mutex);
    entry = (struct cache_entry *)wsdb_hash_search(
        cache->hash, sizeof(cache_id_t), (char *)&id
    );
    if (!entry) {
        //return file_cache_new(cache, id);
        gu_mutex_unlock(&cache->mutex);
        GU_DBUG_RETURN(NULL);
   } else {
        CHECK_OBJ(entry, cache_entry);
    }
    if (!entry->data) {
        if (cache->curr_size < cache->max_size) {
            entry->data = (void *) gu_malloc (cache->block_size);
            cache->curr_size++;
        } else {
            /* replace entry with some victim */
            swap_entry(cache, entry);
        }
        /* read block from file */
        if (entry->file_addr) {
            GU_DBUG_PRINT("wsdb",("reading block from file: %lu", id));
            cache->file->read_block(
                cache->file, entry->file_addr, 
                cache->block_size, entry->data
            );
        } else {
            gu_error("No cached ws entry for: %d", entry->cache_id);
        }
    }

    /* remove from current queue */
    switch (entry->state) {
    case BLOCK_ACTIVE:
        break;
    case BLOCK_DELETED:
        gu_error("trying to access deleted cache block");
        assert(0);
        break;
    case BLOCK_SWAPPABLE: RM_FROM_LIST((cache->entries_swappable), entry);
        break;
    case BLOCK_SWAPPED:   RM_FROM_LIST((cache->entries_swapped), entry);
        break;
    }

    /* put entry first in active list */
    if (entry->state != BLOCK_ACTIVE) {
        entry->state = BLOCK_ACTIVE;
        ADD_TO_LIST((cache->entries_active), entry);
    }
    gu_mutex_unlock(&cache->mutex);

    GU_DBUG_RETURN(entry->data);
}

int file_cache_forget(struct file_cache *cache, cache_id_t id) {
    struct cache_entry *entry;

    GU_DBUG_ENTER("file_cache_forget");
    CHECK_OBJ(cache, file_cache);
    gu_mutex_lock(&cache->mutex);
    entry = (struct cache_entry *)wsdb_hash_search(
        cache->hash, sizeof(cache_id_t), (char *)&id
    );
    if (entry->state != BLOCK_ACTIVE) {
        gu_mutex_unlock(&cache->mutex);
        GU_DBUG_RETURN(WSDB_OK);
    }

    GU_DBUG_PRINT("wsdb",("cache forget for: %d", id));
    entry->state     = BLOCK_SWAPPABLE;

    /* remove entry from active list */
    RM_FROM_LIST((cache->entries_active), entry);

    /* add in swappable list */
    ADD_TO_LIST((cache->entries_swappable), entry);

    gu_mutex_unlock(&cache->mutex);

    /* keep data block */

    GU_DBUG_RETURN(WSDB_OK);
}

int file_cache_delete(struct file_cache *cache, cache_id_t id) {
    struct cache_entry *entry;
    
    GU_DBUG_ENTER("file_cache_delete");
    CHECK_OBJ(cache, file_cache);
    gu_mutex_lock(&cache->mutex);
    entry = (struct cache_entry *)wsdb_hash_delete(
        cache->hash, sizeof(cache_id_t), (char *)&id
    );
    if (!entry) {
        GU_DBUG_PRINT("wsdb",("del entry missing: %d", id));
        gu_mutex_unlock(&cache->mutex);
        GU_DBUG_RETURN(WSDB_OK);
    }
    GU_DBUG_PRINT("wsdb",("deleting cache entry: %d", id));

    /* remove entry from active list */
    switch (entry->state) {
    case BLOCK_ACTIVE:    RM_FROM_LIST((cache->entries_active), entry);
        break;
    case BLOCK_SWAPPABLE: RM_FROM_LIST((cache->entries_swappable), entry);
        break;
    case BLOCK_SWAPPED:   RM_FROM_LIST((cache->entries_swapped), entry);
        break;
    case BLOCK_DELETED:
        gu_error("trying to delete a deleted cache block");
        break;
    default:
        break;
    }

    /* add in deleted list */
    ADD_TO_LIST((cache->entries_deleted), entry);

    gu_mutex_unlock(&cache->mutex);
    GU_DBUG_RETURN(WSDB_OK);
}

cache_id_t file_cache_allocate_id(struct file_cache *cache) {
    CHECK_OBJ(cache, file_cache);
    cache_id_t retval;

    gu_mutex_lock(&cache->mutex);

    if (cache->last_cache_id == UINT_MAX) {
        cache->last_cache_id = 0;
    }
    retval =  ++cache->last_cache_id;

    gu_mutex_unlock(&cache->mutex);
    return retval;
}
