
/**
 * @file addr.h
 *
 * Copyright (C) 2007 Codership Oy <info@codership.com>
 * 
 * @author Teemu Ollakka <teemu.ollakka@codership.com>
 */

#ifndef ADDR_H
#define ADDR_H

#include <gcomm/types.h>


/**
 * Address is defined as 32 bit integer. 
 * 
 * - Process is single application instance
 * - Group defines communication channel in N-to-N communication
 * - Service defines service port in 1-to-1 communication
 *
 * Address is divided in 3 parts (from least significant bytes)
 * - Two first bytes are process identifier 
 * - Third byte is group or service identifier
 * - Fourth byte is segment identifier
 *
 * There can be total of 254 segments (0 and 255 are reserved values)
 * There can be total of 254 groups in segment (0 and 255 reserved)
 * There can be total of 254 services in segment (0 and 255 reserved)
 * There can be total of 65534 processes in segment (0 and 65535 reserved)
 *
 * If this is not enough address space for cluster, please switch to 
 * 64-bit integer.
 *
 */
typedef uint32_t addr_t;
#define ADDR_INVALID (addr_t)-1
#define ADDR_ANY 0

typedef uint16_t proc_id_t;
#define PROC_ID_INVALID (proc_id_t)-1
#define PROC_ID_ANY 0

typedef uint8_t group_id_t;
#define GROUP_ID_INVALID (group_id_t)-1
#define GROUP_ID_ANY 0

typedef uint8_t segment_id_t;
#define SEGMENT_ID_INVALID (segment_id_t)-1
#define SEGMENT_ID_ANY 0

#define ADDR_ALL_MASK        0xffffffffU
#define ADDR_PROC_ID_MASK    0x0000ffffU
#define ADDR_GROUP_ID_MASK   0x00ff0000U
#define ADDR_SEGMENT_ID_MASK 0xff000000U

#define ADDR_PROC_ID(_a) (proc_id_t)((_a) & ADDR_PROC_ID_MASK)
#define ADDR_GROUP_ID(_a) (group_id_t)(((_a) & ADDR_GROUP_ID_MASK) >> 16)
#define ADDR_SEGMENT_ID(_a) (segment_id_t)(((_a) & ADDR_SEGMENT_ID_MASK) >> 24)

/**
 * Ordered set of unique addresses.
 */
typedef struct addr_set_ addr_set_t;

/**
 * Address set iterator.
 */
typedef struct addr_set_iter_ addr_set_iter_t;

/**
 * Allocate new address set.
 *
 * @return Pointer to address set.
 */
addr_set_t *addr_set_new(void);

/**
 * Copy address set.
 */
addr_set_t *addr_set_copy(const addr_set_t *);

/**
 * Free address set.
 */
void addr_set_free(addr_set_t *);

/**
 * Insert new address to address set. 
 *
 * @return Iterator pointing to set element
 * @return NULL if address is invalid
 * @return NULL if address already exists in set
 */
const addr_set_iter_t *addr_set_insert(addr_set_t *, const addr_t);

/**
 * Erase address from address set.
 */
void addr_set_erase(addr_set_t *, const addr_t);

/**
 * Return iterator to the first address in set.
 */
const addr_set_iter_t *addr_set_first(const addr_set_t *);

/**
 * Find address from address set. 
 *
 * TODO: Should wildcards be allowed? For example if addr is 
 *       0x01010000 find would return iterator to the first 
 *       process in (segment 1, group 1)...
 */
addr_set_iter_t *addr_set_find(const addr_set_t *, const addr_t);

/**
 * Return iterator pointing to next entry in set, or NULL if not found.
 *
 * TODO: Should masking be allowed? For example, if current addr is 
 *       0x01010003 and it is the last process in (segment 1, group 1),
 *       next would return NULL even if there were further addresses...
 */
const addr_set_iter_t *addr_set_next(const addr_set_iter_t *);

const addr_set_iter_t *addr_set_nth(const addr_set_t *, int);

/**
 * Cast iterator to addr_t.
 */
addr_t addr_cast(const addr_set_iter_t *);

/**
 * Compare address sets and return true if they are equal.
 */
bool addr_set_equal(const addr_set_t *, const addr_set_t *);

/**
 * Check if address set a is subset of b.
 */
bool addr_set_is_subset(const addr_set_t *a, const addr_set_t *b);

/**
 * Create union of two address sets.
 */
addr_set_t *addr_set_union(const addr_set_t *, const addr_set_t *);

/**
 * Create intersection of two address sets.
 */
addr_set_t *addr_set_intersection(const addr_set_t *, const addr_set_t *);

/**
 * Get the size (number of addresses) of address set.
 */
size_t addr_set_size(const addr_set_t *);



/**
 * Read address set from buffer starting from offset.
 *
 * @param buf Buffer to read from
 * @param buflen Total length of buffer
 * @param offset Offset from start of buffer
 * @param addr_set Location where newly allocated address set is stored
 *
 * @return Offset pointing past last byte that was read
 * @return Zero if read failed
 */
size_t addr_set_read(const void *buf, const size_t buflen, const size_t offset, 
		     addr_set_t **addr_set);

/**
 * Write address set into buffer starting from offset.
 *
 * @param addr_set Ponter to addr_set to write
 * @param buf Buffer to write into
 * @param buflen Total length of buffer
 * @param offset Offset to start from
 *
 * @return Offset pointing past last byte that was written
 * @return Zero in write failed (invalid addr set or not enough space in buffer)
 */
size_t addr_set_write(const addr_set_t *addr_set, void *buf, 
			 const size_t buflen, const size_t offset);

/**
 * Return buffer space needed to store address set.
 *
 * @return Number of bytes
 * @return Zero in case of invalid address set
 */
size_t addr_set_len(const addr_set_t *);



#endif /* ADDR_H */
