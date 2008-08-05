// Copyright (C) 2007 Codership Oy <info@codership.com>

#ifndef WSDB_FILE_CACHE
#define WSDB_FILE_CACHE

#include "wsdb_priv.h"
#include "wsdb_file.h"

struct cache_id {
    uint32_t    cache_id;
    file_addr_t file_addr;
};

typedef uint32_t cache_id_t;
#define INVALID_CACHE_ID 0

/*!
 * @struct
 */
struct file_cache {
    char                ident;
    struct wsdb_hash   *hash;
    struct wsdb_file   *file;
    cache_id_t          last_cache_id;
    uint16_t            block_size;

    uint32_t            max_size;
    uint32_t            curr_size;

    struct cache_entry *entries_active;    //!< list of active entries
    struct cache_entry *entries_swapped;   //!< entries swapped in disk
    struct cache_entry *entries_swappable; //!< entries volunteering to swap
    struct cache_entry *entries_deleted;   //!< deleted entries

    gu_mutex_t          mutex;             //!< some privacy needed
};
#define IDENT_file_cache 'C'
int file_cache_close(struct file_cache *cache);

/*
 * @brief reports the memory allocation for a file cache
 *
 * @param cache the file cache to be reported
 * @return bytes currently allocated for cache 
 */
uint32_t file_cache_report(struct file_cache *cache);

/*
 * @brief opens cache for the given file with given memory limit
 *
 * @param file open wsdb file, access to this file is cached
 * @param mem_limit memory limit for the cache (in bytes)
 * @param max_elems estimated max number of elems
 * @return pointer to the cache
 */
struct file_cache *file_cache_open(
    struct wsdb_file *file, uint32_t mem_limit, uint32_t max_elems
);

/*
 * @brief marks the given cache object to free 
 * @param id cache object to be freed
 * @return success code
 */
int file_cache_forget(struct file_cache *cache, cache_id_t id);

/*
 * @brief @fixme: 
 * @param id @fixme:
 * @return success code
 */
int file_cache_delete(struct file_cache *cache, cache_id_t id);

/*
 * @brief accesses cache object by id
 * @param cache
 * @param id cache id of the object to be searched for
 */
void *file_cache_get(struct file_cache *cache, cache_id_t id);

/*
 * @brief allocates new cache object
 * @param cache
 * @return id for the new object
 */
cache_id_t file_cache_allocate_id(struct file_cache *cache);

/*
 * @brief creates new cache object
 * @param cache file cache
 * @param id id of the cache object to be created, previously allocated
 * @return pointer to the new object
 */
void *file_cache_new(struct file_cache *cache, cache_id_t id);
#endif
