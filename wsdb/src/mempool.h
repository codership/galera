// Copyright (C) 2007 Codership Oy <info@codership.com>

#ifndef MEMPOOL_INCLUDED
#define MEMPOOL_INCLUDED
#include "wsdb_priv.h"

struct mempool;

enum pooltype {
  MEMPOOL_DYNAMIC, // reuse freed items for following mallocs
  MEMPOOL_STICKY,  // don't reuse items, free block when all entries released
};

/*
 * @brief creates a mempool for buffering small fixed sized mem allocations
 *     You can allocate fixed sized (of item_size) memory buffers from this
 *     this allocator. Allocated buffer should be properly aligned to 
 *     store any data type.
 *     
 * @param item_size Size of the memory buffer you can ask from this allocator
 * @param items_in_block This parameter determines how big memory blocks the 
 *     allocator will malloc from system. Allocator will malloc a number of 
 *     memory blocks, from where it returns pointers to fixed memory locations
 *     for the user.
 * @param pool_type MEMPOOL_DYNAMIC or MEMPOOL_STICKY
 * @param use_mutex should mutex be used to control access allocator
 * @param owner free form name for the allocator
 *
 * @return address to the allocated mempool
 */
struct mempool *mempool_create(
    int item_size, int items_in_block, enum pooltype pool_type, bool use_mutex,
    char * owner
);

/*
 * @brief to close the memory buffer. All data is freed
 */
int mempool_close(struct mempool *pool);

/*
 * @brief returns pointer to memory buffer of the fixed size
 * the length parameter is just a safeguard to check that the user
 * does not expect too large memory buffer to be returned.
 * mempool will return the pointer from one of the large memory blocks
 * it has malloced from the system. If all blocks are 'full', it will
 * malloc one more memory block.
 *
 * @param pool the memory pool where to return buffer
 * @param length users expectation of the memory buffer length
 */
void *mempool_alloc(struct mempool *pool, int length);

/*
 * @brief returns the allocated buffer to the pool
 * mempool check that the pointer belongs to one of his blocks
 * and marks the buffer free. It will be returned for next mempool_alloc()
 * caller. If one large memory block becomes totally free after this operation,
 * the memory block is freed back to the system.
 *
 * @param pool the mempool to use
 * @param buf the buffer to return 
 * @return 0 if good, -1 if buf is not part of mempool
 */
int mempool_free(struct mempool *pool, void *buf);

/*
 * @brief prints allocation information from pool
 *     Function returns total size allocated and can optionally
 *     print detailed report to stdout
 * @param pool the mempool to use
 * @param print to print report in stdout or not
 * @return total memory allocated in pool
 */
uint32_t mempool_report(struct mempool *pool, bool print);

#endif
