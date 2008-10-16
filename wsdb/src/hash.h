// Copyright (C) 2007 Codership Oy <info@codership.com>

#ifndef WSDB_HASH_INCLUDED
#define WSDB_HASH_INCLUDED
#include "wsdb_priv.h"

struct wsdb_hash;

/*!
 * @brief calculates the hash value from a character array 
 * @param max_size max number of elemnts in the hash
 * @param len length of the data array
 * @param data the data to be hashed
 * @return the hash value
 */
typedef uint32_t (* hash_fun_t)(uint32_t, uint16_t, char *);

/*!
 * @brief compares tow hash data arrays
 * @param len1 length of the first data array
 * @param data1 the first data
 * @param len2 length of the second data array
 * @param data2 the second data
 * @return the comparision result
 * @retval -1 data1 < data2
 * @retval 0  data1 == data2
 * @retval 1  data1 > data2
 */
typedef int (* hash_cmp_t)(uint16_t, char *, uint16_t, char *);

/*!
 * @brief initializes a hash index
 *
 * @param max_size max number of elements in the hash
 * @param hash_fun function for calculating hash value
 * @param hash_cmp comparision function 
 * @param unique is the element required to be uniqueue or not
 *
 * @retun pointer to initialized hash index
 */
struct wsdb_hash *wsdb_hash_open(
    uint32_t max_size, hash_fun_t hash_fun, hash_cmp_t hash_cmp, bool unique
);

/*! 
 * @brief closes and frees a hash index
 *
 * @param hash the hash to close
 */
int wsdb_hash_close(struct wsdb_hash *hash);

/*!
 * @brief searches hash with the given key
 *
 * @param hash the hash index being searched
 * @param key_len length of the key
 * @param key the key value
 *
 * @return pointer to the data or NULL
 */
void *wsdb_hash_search(struct wsdb_hash *hash, uint16_t key_len, char *key);

/*!
 * @brief deletes an element from hash
 *
 * @param hash the hash index to delete from
 * @param key_len length of the key value
 * @param key the key value
 *
 * @return pointer to the data or NULL
 */
void *wsdb_hash_delete(struct wsdb_hash *hash, uint16_t key_len, char *key);

/*!
 * @brief pushes an element in the hash
 * @param hash the hash index
 * @param key_len length of the key value
 * @param key key value
 * @param data data for the element
 * @return WSDB_OK, WSDB_ERR_HASH_DUPLICATE 
 */
int wsdb_hash_push(
    struct wsdb_hash *hash, uint16_t key_len, char *key, void *data
);
/*!
 * @brief gives verdict for pruning given entry from index
 * @param key pointer to key value of the entry, candidate for purging 
 * @param data pointer to the data value in this entry, you can free this
 * @param **data address of the data pointer, you can overwrite new value here
 * @return 1=entry to be purged, 0=entry must remain in hash
 */
typedef int (* hash_verdict_fun_t)(void *, void *, void **);

/*!
 * @brief deletes a range of elements from hash
 *
 * function calls verdict function to let the caller determine 
 * if each entry should be deleted
 * 
 * 
 * @param hash the hash index to delete from
 * @param verdcit decision function to be called for each entry
 *
 * @return number of entries deleted
 */
int wsdb_hash_delete_range(
    struct wsdb_hash *hash, void *ctx, hash_verdict_fun_t verdict
);

/*!
 * @brief callback for each entry found in the hash
 * @param key pointer to key value of the entry
 * @param data pointer to the data value in this entry
 */
typedef void (* hash_scan_fun_t)(void *, void *);

/*!
 * @brief scans all elements in hash calling callback for each
 *
 * function calls callback function to let the caller collect
 * all elements from hash
 * 
 * 
 * @param hash the hash index to delete from
 * @param scan_cb callback function to be called for each entry
 *
 * @return number of entries found
 */
int wsdb_hash_scan(
    struct wsdb_hash *hash, void *ctx, hash_scan_fun_t scan_cb
);

uint32_t wsdb_hash_report(struct wsdb_hash *hash);

#endif
