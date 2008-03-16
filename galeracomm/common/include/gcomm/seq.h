/**
 * @file seq.h
 *
 * Generic sequence number operations. 
 *
 * Note that sequence numbers used are 64-bit... probably overkill
 * in most of the cases.
 *
 * Copyright (C) 2007 Codership Oy <info@codership.com>
 * 
 * @author Teemu Ollakka <teemu.ollakka@codership.com>
 */

#ifndef SEQ_H
#define SEQ_H

#include <gcomm/types.h>

/**
 * 64-bit sequence number
 */
typedef uint64_t seq_t;
#define SEQ_INVALID (seq_t)-1


/**
 * Ordered set of unique seq
 */
typedef struct seq_set_ seq_set_t;

/**
 * Sequence Number set iterator.
 */
typedef struct seq_set_iter_ seq_set_iter_t;

/**
 * Allocate new sequence number set.
 *
 * @return Pointer to sequence number set.
 */
seq_set_t *seq_set_new(void);

/**
 * Copy sequence number set.
 */
seq_set_t *seq_set_copy(const seq_set_t *);

/**
 * Free sequence number set.
 */
void seq_set_free(seq_set_t *);

/**
 * Insert new sequence number to sequence number set. 
 *
 * @return Iterator pointing to set element
 * @return NULL if sequence number is invalid
 * @return NULL if sequence number already exists in set
 */
const seq_set_iter_t *seq_set_insert(seq_set_t *, const seq_t);

/**
 * Erase sequence number from sequence number set.
 */
void seq_set_erase(seq_set_t *, const seq_t);

/**
 * Return iterator to the first sequence number in set.
 */
const seq_set_iter_t *seq_set_first(const seq_set_t *);

/**
 * Find sequence number from sequence number set. 
 */
const seq_set_iter_t *seq_set_find(const seq_set_t *, const seq_t);

/**
 * Return iterator pointing to next entry in set, or NULL if not found.
 *
 */
const seq_set_iter_t *seq_set_next(const seq_set_iter_t *);

/**
 * Cast iterator to seq_t.
 */
seq_t seq_cast(const seq_set_iter_t *);

/**
 * Compare sequence number sets and return true if they are equal.
 */
bool seq_set_equal(const seq_set_t *, const seq_set_t *);

/**
 * Check if sequence number set a is subset of b.
 */
bool seq_set_is_subset(const seq_set_t *a, const seq_set_t *b);

/**
 * Create union of two sequence number sets.
 */
seq_set_t *seq_set_union(const seq_set_t *, const seq_set_t *);

/**
 * Create intersection of two sequence number sets.
 */
seq_set_t *seq_set_intersection(const seq_set_t *, const seq_set_t *);

/**
 * Get the size (number of sequence numberes) of sequence number set.
 */
size_t seq_set_size(const seq_set_t *);



/**
 * Read sequence number set from buffer starting from offset.
 *
 * @param buf Buffer to read from
 * @param buflen Total length of buffer
 * @param offset Offset from start of buffer
 * @param seq_set Location where newly allocated sequence number set is stored
 *
 * @return Offset pointing past last byte that was read
 * @return Zero if read failed
 */
size_t seq_set_read(const void *buf, const size_t buflen, const size_t offset, 
		     seq_set_t **seq_set);

/**
 * Write sequence number set into buffer starting from offset.
 *
 * @param seq_set Ponter to seq_set to write
 * @param buf Buffer to write into
 * @param buflen Total length of buffer
 * @param offset Offset to start from
 *
 * @return Offset pointing past last byte that was written
 * @return Zero in write failed (invalid addr set or not enough space in buffer)
 */
size_t seq_set_write(const seq_set_t *seq_set, void *buf, 
			 const size_t buflen, const size_t offset);

/**
 * Return buffer space needed to store sequence number set.
 *
 * @return Number of bytes
 * @return Zero in case of invalid sequence number set
 */
size_t seq_set_len(const seq_set_t *);

#endif /* SEQ_H */
