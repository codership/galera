// Copyright (C) 2007 Codership Oy <info@codership.com>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "wsdb_priv.h"
#include "mempool.h"

struct block {
    struct block *next;
    char         *block;
    char         *free_list;
    int           in_use;
    char         *end;
};

struct mempool {
    /* list of active memory blocks */
    struct block *blocks;

    /* number of blocks */
    int block_count;
    int items_in_block;

    /* size of each memory block */
    int block_size;
 
    /* size of individual item inside block */
    int item_size;
    gu_mutex_t         mutex;

    enum pooltype pool_type;
    bool use_mutex;

    char owner[20];
    char ident;
};
#define IDENT_mempool 'p'

static int max_align( ) {
    union union_u {
            int              a;
            long             b;
            long long        c;
            float            d;
            double           e;
            long double      f;
            char            *g;
            struct {char a;}  h;
    };
    struct {
        union union_u u;
        char a;
    } s;
    return ((int)((long)&(s.a) - (long)&(s.u)) - sizeof(union union_u));
}

struct mempool *mempool_create(
    int item_size, int items_in_block, enum pooltype pool_type, bool use_mutex,
    char *owner
) {
    int padding;
    struct mempool *pool;
    int align = (max_align()) ? max_align() : 1;

    MAKE_OBJ(pool, mempool);

    padding = (align - item_size % align) % align;
    item_size += padding;

    pool->item_size      = item_size;
    pool->block_size     = item_size * items_in_block;
    pool->items_in_block = items_in_block;
    pool->block_count    = 0;
    pool->blocks         = NULL;
    pool->pool_type      = pool_type;
    pool->use_mutex      = use_mutex;
    memset(pool->owner, '\0', 20);
    strncpy(pool->owner, owner, 20);

    if (use_mutex) gu_mutex_init(&(pool->mutex), NULL);

    return (pool);
}

int mempool_close(struct mempool *pool) {
    struct block *block;

    CHECK_OBJ(pool, mempool);

    block = pool->blocks;
    while(block) {
        struct block *next = block->next;
        gu_free(block->block);
        gu_free(block);

        block = next;
    }
    return 0;
}

/*
 * @brief initializes pointer chain in memory block
 * @param block     memory block to initialize
 * @param block_len length of the memory block
 * @param item_len  length of items in the block
 */
static void init_block(char *block, int block_len, int item_len) {

    char *item;
    for (item=block; item<block+block_len; item+=item_len) {
      *(char**)item = ((long)item + item_len < (long)block + block_len) ? 
        item + item_len : NULL;
    }
}

static struct block *add_block(struct mempool *pool, struct block *prev) {
    struct block *new_block;

    /* need to add one more block */
    new_block            = (struct block*)gu_malloc(sizeof(struct block));
    new_block->block     = (char *)gu_malloc(pool->block_size);
    new_block->free_list = new_block->block;
    new_block->next      = NULL;
    new_block->end       = new_block->block + pool->block_size;
    if (prev) {
        prev->next = new_block;
    } else {
        pool->blocks = new_block;
    }
    pool->block_count++;

    return new_block;
}

static void *mempool_alloc_sticky(struct mempool *pool, int length) {
    struct block *block;
    struct block *prev = NULL;
    struct block *new_block;

    if (pool->use_mutex) gu_mutex_lock(&(pool->mutex));
    block = pool->blocks;

    while(block) {
        if (block->free_list < block->end) {
            void *mem = (void *)block->free_list;
            block->free_list = block->free_list + pool->item_size;
            block->in_use += 1;
            if (pool->use_mutex) gu_mutex_unlock(&(pool->mutex));
            return mem;
        }
        prev  = block;
        block = block->next;
    }

    new_block = add_block(pool, prev);

    {
        void *mem = (void *)new_block->block;
        new_block->free_list = new_block->block + pool->item_size;
        new_block->in_use    = 1;
        if (pool->use_mutex) gu_mutex_unlock(&(pool->mutex));
        return mem;
    }
}

static void *mempool_alloc_dynamic(struct mempool *pool, int length) {
    struct block *block;
    struct block *prev = NULL;
    struct block *new_block;

    if (pool->use_mutex) gu_mutex_lock(&(pool->mutex));
    block = pool->blocks;

    while(block) {
      if (block->free_list) {
          void *mem = (void *)block->free_list;
          block->free_list = (*(char**)block->free_list);
          block->in_use += 1;
          if (pool->use_mutex) gu_mutex_unlock(&(pool->mutex));
          return mem;
      }
      prev  = block;
      block = block->next;
    }

    /* need to add one more block */
    new_block            = add_block(pool, prev);

    init_block(new_block->block, pool->block_size, pool->item_size);

    {
        void *mem = (void *)new_block->free_list;
        new_block->free_list = (*(char**)new_block->free_list);
        new_block->in_use    = 1;
        if (pool->use_mutex) gu_mutex_unlock(&(pool->mutex));
        return mem;
    }
}
void *mempool_alloc(struct mempool *pool, int length) {

    CHECK_OBJ(pool, mempool);
    switch(pool->pool_type) {
    case MEMPOOL_DYNAMIC:
        return mempool_alloc_dynamic(pool, length);
    case MEMPOOL_STICKY:
        return mempool_alloc_sticky(pool, length);
    }
    assert(0);
    return NULL;
}

int mempool_free(struct mempool *pool, void *buf) {
    struct block *block;
    struct block *prev = NULL;
    bool found = false;

    CHECK_OBJ(pool, mempool);

    if (pool->use_mutex) gu_mutex_lock(&(pool->mutex));
    block = pool->blocks;

    while(block) {
        if ((buf >= (void*)block->block) && (buf < (void*)block->end)) {
            found = true;
            if (pool->pool_type == MEMPOOL_DYNAMIC) {
                *(char **)buf = block->free_list;
                block->free_list = (char *)buf;
            }
            block->in_use--;

            if (block->in_use == 0) {
                if (pool->blocks != block) {
                    if (prev) {
                        prev->next = block->next;
                    } else {
                        pool->blocks = block->next;
                    }
                    gu_free(block->block);
                    gu_free(block);
                    pool->block_count--;
                } else if (pool->pool_type == MEMPOOL_STICKY) {
                    // reuse the first block, avoid freeing memory here
                    block->free_list = block->block;
                }
            }

            if (pool->use_mutex) gu_mutex_unlock(&(pool->mutex));
            return 0;
        }
        prev  = block;
        block = block->next;
    }
    if (!found)
        gu_warn("trying to free pointer, which is not part of this mempool");
    if (pool->use_mutex) gu_mutex_unlock(&(pool->mutex));
    return(-1);
}
