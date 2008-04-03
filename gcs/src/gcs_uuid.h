/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*
 * Universally Unique IDentifier for Group.
 * Serves to guarantee that nodes from two different
 * groups will never merge by mistake.
 *
 */

#ifndef _gcs_uuid_h_
#define _gcs_uuid_h_

#include <stdint.h>

/** UUID internally is represented as a BE integer which allows using
 *  memcmp() as comparison function and straightforward printing */
typedef struct {
    uint8_t data[16];
} gcs_uuid_t;

extern const gcs_uuid_t GCS_UUID_NIL;

/** Macros for pretty printing */
#define GCS_UUID_FORMAT \
"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x"

#define GCS_UUID_ARGS(uuid) \
(uuid)->data[ 0], (uuid)->data[ 1], (uuid)->data[ 2], (uuid)->data[ 3],\
(uuid)->data[ 4], (uuid)->data[ 5], (uuid)->data[ 6], (uuid)->data[ 7],\
(uuid)->data[ 8], (uuid)->data[ 9], (uuid)->data[10], (uuid)->data[11],\
(uuid)->data[12], (uuid)->data[13], (uuid)->data[14], (uuid)->data[15]

/** 
 * Generates new UUID.
 * If node is NULL, will generate random (if /dev/urand is present) or
 * pseudorandom data instead.
 * @param uuid
 *        pointer to uuid_t
 * @param node
 *        some unique data that goes in place of "node" field in the UUID
 * @paran node_len
 *        length of the node buffer
 */
extern void
gcs_uuid_generate (gcs_uuid_t*    uuid,
		   const uint8_t* node,
		   size_t         node_len);

/** 
 * Compare two UUIDs according to RFC
 * @return -1, 0, 1 if left is respectively less, equal or greater than right
 */
extern int
gcs_uuid_compare (const gcs_uuid_t* left,
                  const gcs_uuid_t* right);

/** 
 * Compare ages of two UUIDs
 * @return -1, 0, 1 if left is respectively less, equal or greater than right
 */
extern int
gcs_uuid_older (const gcs_uuid_t* left,
		const gcs_uuid_t* right);


#endif /* _gcs_uuid_h_ */
