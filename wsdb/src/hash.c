// Copyright (C) 2007 Codership Oy <info@codership.com>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "wsdb_priv.h"
#include "hash.h"
//#define USE_MEMPOOL

struct hash_entry {
    struct hash_entry *next;
    uint16_t           key_len;
    void              *key;
    void              *data;
};
struct entry_match {
    struct hash_entry *entry;
    struct hash_entry *prev;
    uint32_t           idx;
};

struct wsdb_hash {
    char               ident;
    bool               unique;     // is index unique
    gu_mutex_t         mutex;
    uint32_t           array_size; // number of many bucket lists allocated
    uint32_t           elem_count; // number of elements currently in hash
    hash_fun_t         hash_fun;
    hash_cmp_t         hash_cmp;
#ifdef USE_MEMPOOL
    struct mempool     *entry_pool; // memory pool for entry structs
    struct mempool     *key_pool;   // memory pool for key allocations
    int                key_pool_limit; // limit for key len stored in key_pool
#endif
    struct hash_entry *elems[];
};

#define IDENT_wsdb_hash 'h'

struct wsdb_hash *wsdb_hash_open(
    uint32_t max_size, hash_fun_t hash_fun, hash_cmp_t hash_cmp, bool unique
) {
    struct wsdb_hash *hash;
    int i;
    MAKE_OBJ_SIZE(hash, wsdb_hash, max_size * sizeof(struct hash_entry *));

    hash->unique     = unique;
    for (i=0; i<max_size; i++) hash->elems[i] = NULL;
    hash->array_size = max_size;
    hash->elem_count = 0;
    hash->hash_fun   = hash_fun;
    hash->hash_cmp   = hash_cmp;
    hash->unique     = unique;

    /* initialize the mutex */
    gu_mutex_init(&hash->mutex, NULL);

#ifdef USE_MEMPOOL
    /* create mempool for hash entries and optional short keys */
    hash->entry_pool = mempool_create(
        sizeof(struct hash_entry), 32000, MEMPOOL_STICKY, false, "hash entry"
    );
    if (!hash->entry_pool) {
        gu_error("failed to create mempool for hash entries");
    }

    hash->key_pool_limit = 64;
    hash->key_pool       = mempool_create(
        hash->key_pool_limit, 32000, MEMPOOL_STICKY, false, "hash keys"
    );
    if (!hash->key_pool) {
        gu_error("failed to create mempool for hash keys");
    }
#endif
    return hash;
}

int wsdb_hash_close(struct wsdb_hash *hash) {
    int i;
    CHECK_OBJ(hash, wsdb_hash);
    /* Should we lock here? gu_mutex_lock(&hash->mutex); */
    for (i = 0; i < hash->array_size; i++) {
	if (hash->elems[i]) {
	    struct hash_entry *e = (struct hash_entry *) hash->elems[i];
	    struct hash_entry *e_next;
	    for (; e; e = e_next) {
#ifdef USE_MEMPOOL
                int rcode;
#endif
		e_next = e->next;
		if (e->key_len > 4) {
#ifndef USE_MEMPOOL
                    gu_free(e->key);
#else
                    if (e->key_len <= hash->key_pool_limit) {
                        rcode = mempool_free(hash->key_pool, e->key);
                        if (rcode) {
                            gu_error("hash key free failed: %d", rcode);
                            return WSDB_FATAL;
                        }
                    } else {
                        gu_free(e->key);
                    }
#endif
                }
#ifndef USE_MEMPOOL
                gu_free(e);
#else
                rcode = mempool_free(hash->entry_pool, e);
                if (rcode) {
                    gu_error("hash entry free failed: %d", rcode);
                    return WSDB_FATAL;
                }
#endif
	    }
	}
	hash->elems[i] = NULL;
    }
#ifdef USE_MEMPOOL
    /* release mempools */
    mempool_close(hash->entry_pool);
    mempool_close(hash->key_pool);
#endif
    gu_free(hash);
    return WSDB_OK;
}

static void hash_search_entry(
    struct entry_match *match, struct wsdb_hash *hash, 
    uint16_t key_len, char key[]
) {
    struct hash_entry *e;
    
    CHECK_OBJ(hash, wsdb_hash);

    match->entry = match->prev = NULL;
    match->idx = hash->hash_fun(hash->array_size, key_len, key);

    if (match->idx >= hash->array_size) {
        gu_error("hash array size exceeded: %d", match->idx);
        assert(0);
    }

    e = hash->elems[match->idx];
    while (e) {
        switch(hash->hash_cmp(
                   key_len, key, e->key_len, 
                   (e->key_len > 4) ? e->key : (char *)&e->key)
        ) {
        case -1:
            match->prev = e;
            break;
        case 0:
            match->entry = e;
            return;
        case 1:
            return;
        }
        e = e->next;
    }
    return;
}

int wsdb_hash_push(
    struct wsdb_hash *hash, uint16_t key_len, char *key, void *data
) {
    struct hash_entry *entry;
    struct entry_match match;

    CHECK_OBJ(hash, wsdb_hash);
    
    gu_mutex_lock(&hash->mutex);

    /* locate the point in bucket lists */
    hash_search_entry(&match, hash, key_len, key);

    if (hash->unique && match.entry) {
        /* duplicate found, bail out */
        gu_mutex_unlock(&hash->mutex);
        return WSDB_ERR_HASH_DUPLICATE;
    }

    //if (hash->curr_size == hash->max_size) return WSDB_ERROR;
    hash->elem_count++;

#ifndef USE_MEMPOOL
    entry = (struct hash_entry *) gu_malloc (sizeof (struct hash_entry));
#else
    entry = (struct hash_entry *)mempool_alloc(
        hash->entry_pool, sizeof (struct hash_entry)
    );
#endif

    /* use directly key pointer as key value if key is shorter 
     * than pointer size 
     */
    if (key_len <= 4) {
        char *e, *k;
        uint16_t i;
        entry->key = NULL;
        for (i=0, e=(char *)&entry->key, k=key; i<key_len; i++, e++, k++) {
            *e=*k;
        }
    } else {
#ifndef USE_MEMPOOL
        entry->key = (void *) gu_malloc (key_len);
#else
        if (key_len <= hash->key_pool_limit) {
            entry->key = (void *) mempool_alloc(hash->key_pool, key_len);
        } else {
            entry->key = (void *) gu_malloc (key_len);
        }
#endif
        memcpy(entry->key, key, key_len);
    }
    entry->key_len = key_len;
    entry->data    = data;

    if (match.prev) {
        entry->next = match.prev->next;
        match.prev->next = entry;
    } else if (match.entry) {
        entry->next = match.entry;
        hash->elems[match.idx] = entry;
    } else {
        entry->next = hash->elems[match.idx];
        hash->elems[match.idx] = entry;
    }
    gu_mutex_unlock(&hash->mutex);
    return WSDB_OK;
}

void *wsdb_hash_search(struct wsdb_hash *hash, uint16_t key_len, char key[]) {
    struct entry_match match;
    gu_mutex_lock(&hash->mutex);
    hash_search_entry(&match, hash, key_len, key);
    gu_mutex_unlock(&hash->mutex);
    if (match.entry) {
        return match.entry->data;
    } else {
        return NULL;
    }
}

void *wsdb_hash_delete(struct wsdb_hash *hash, uint16_t key_len, char key[]) {
    struct entry_match match;

    GU_DBUG_ENTER("wsdb_hash_delete");

    gu_mutex_lock(&hash->mutex);
    hash_search_entry(&match, hash, key_len, key);
    if (match.entry) {
#ifdef USE_MEMPOOL
        int rcode;
#endif
        void *data = match.entry->data;
        if (match.prev) {
            match.prev->next = match.entry->next;
        } else {
            hash->elems[match.idx] = match.entry->next;
        }
        if (key_len > 4) {
#ifndef USE_MEMPOOL
          gu_free(match.entry->key);
#else
            if (key_len <= hash->key_pool_limit) {
                rcode = mempool_free(hash->key_pool, match.entry->key);
                if (rcode) {
                    gu_error("hash key free failed: %d", rcode);
                }
            } else {
                gu_free(match.entry->key);
            }
#endif
        }
#ifndef USE_MEMPOOL
        gu_free(match.entry);
#else
        rcode = mempool_free(hash->entry_pool, match.entry);
        if (rcode) {
            gu_error("hash entry free failed: %d", rcode);
        }
#endif
        hash->elem_count--;
        gu_mutex_unlock(&hash->mutex);
        if (!data) {
            GU_DBUG_PRINT("wsdb",("del entry null: %d, %s", key_len, key));
        }
        GU_DBUG_RETURN(data);
    } else {
        GU_DBUG_PRINT("wsdb",("del entry not found: %d, %s", key_len, key));
        gu_mutex_unlock(&hash->mutex);
        GU_DBUG_RETURN(NULL);
    }
}

int wsdb_hash_delete_range(
    struct wsdb_hash *hash, void *ctx,
    hash_verdict_fun_t verdict
) {
    int deleted = 0;
    int i;
#ifdef USE_MEMPOOL
    int rcode;
#endif
    GU_DBUG_ENTER("wsdb_hash_delete_range");

    gu_mutex_lock(&hash->mutex);

    for (i=0; i<hash->array_size; i++) {
        struct hash_entry *entry = hash->elems[i];
        struct hash_entry *prev  = NULL;

        while (entry) {
            /* caller determines, if index entry must be purged */
            if (verdict(ctx, entry->data, &(entry->data))) {
                /* to delete */
                if (prev) {
                    prev->next = entry->next;
                } else {
                    hash->elems[i] = entry->next;
                }
                if (entry->key_len > 4) {
#ifndef USE_MEMPOOL
                    gu_free(entry->key);
#else
                    if (entry->key_len <= hash->key_pool_limit) {
                        rcode = mempool_free(hash->key_pool, entry->key);
                        if (rcode) {
                            gu_error("hash key free failed: %d", rcode);
                            return WSDB_FATAL;
                        }
                    } else {
                        gu_free(entry->key);
                    }
#endif
                }
                hash->elem_count--;
                // prev will not be stepped ahead
                {
                    struct hash_entry *entry_next = entry->next;
#ifndef USE_MEMPOOL
                    gu_free(entry);
#else
                    rcode = mempool_free(hash->entry_pool, entry);
                    if (rcode) {
                        gu_error("hash entry free failed: %d", rcode);
                        return WSDB_FATAL;
                    }
#endif
                    entry = entry_next;
                }
                deleted++;
            } else {
                prev  = entry;
                entry = entry->next;
            }
        }
    }

    gu_mutex_unlock(&hash->mutex);
    GU_DBUG_RETURN(deleted);
}

int wsdb_hash_scan(
    struct wsdb_hash *hash, void *ctx, hash_scan_fun_t scan_cb
) {
    int count = 0;
    int i;

    GU_DBUG_ENTER("wsdb_hash_scan");

    gu_mutex_lock(&hash->mutex);

    for (i=0; i<hash->array_size; i++) {
        struct hash_entry *entry = hash->elems[i];

        while (entry) {
            scan_cb(ctx, entry->data);
            count++;
            entry = entry->next;
        }
    }

    gu_mutex_unlock(&hash->mutex);
    GU_DBUG_RETURN(count);
}

uint32_t wsdb_hash_report(
    struct wsdb_hash *hash
) {
    int i;
    uint32_t mem_usage;

    GU_DBUG_ENTER("wsdb_hash_report");
    gu_mutex_lock(&hash->mutex);
    mem_usage = hash->array_size * sizeof(struct hash_entry *);

    for (i=0; i<hash->array_size; i++) {
        struct hash_entry *entry = hash->elems[i];

        while (entry) {
            mem_usage += sizeof(entry);
            if (entry->key_len > 4) mem_usage += entry->key_len;
            entry = entry->next;
        }
    }

    gu_mutex_unlock(&hash->mutex);
    GU_DBUG_RETURN(mem_usage);
}
