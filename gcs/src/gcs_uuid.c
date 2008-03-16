// Copyright (C) 2007 Codership Oy <info@codership.com>
/*
 * Universally Unique IDentifier for Group.
 * Serves to guarantee that nodes from two different
 * groups will never merge by mistake
 *
 */

#include <stdlib.h>   // for rand_r()
#include <string.h>   // for memcmp()
#include <stdio.h>    // for fopen() et al
#include <sys/time.h> // for gettimeofday()
#include <unistd.h>   // for getpid()
#include <errno.h>    // for errno
#include <stddef.h>

#include <galerautils.h>
#include "gcs_uuid.h"

const gcs_uuid_t GCS_UUID_NIL = {{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}};

#define UUID_NODE_LEN 6

/** Returns 64-bit system time in 100 nanoseconds */
static uint64_t
uuid_get_time ()
{
    struct timeval t;
    gettimeofday (&t, NULL);
    return ((t.tv_sec * 10000000) + (t.tv_usec * 10) +
	    0x01B21DD213814000LL); // offset since the start of 15 October 1582
}

/** Fills node part of the uuid with true random data from /dev/urand */
static int
uuid_urand_node (uint8_t* node, size_t node_len)
{
    static const char urand_name[] = "/dev/urand";
    FILE*       urand;
    size_t      i = 0;
    int         c;

    urand = fopen (urand_name, "r");

    if (NULL == urand) {
	gu_warn ("Failed to open %s for reading (%d).", urand_name, -errno);
	return -errno;
    }

    while (i < node_len && (c = fgetc (urand)) != EOF) {
	node[i] = (uint8_t) c;
	i++;
    }
    fclose (urand);

    return 0;
}

/** Fills node part with pseudorandom data from rand_r() */
static void
uuid_rand_node (uint8_t* node, size_t node_len)
{
    uint64_t pid    = (uint64_t) getpid();
    uint64_t addr1  = (uint64_t)(ptrdiff_t)node;
    uint64_t addr2  = (uint64_t)(ptrdiff_t)&addr1;
    uint64_t seed64 = pid ^ addr1 ^ addr2;
    struct timeval time;
    unsigned int   seed;
    size_t         i;

    gettimeofday (&time, NULL);
    /* use both high and low part of struct timeval and seed64 to
     * construct the final rand_r() seed */
    seed = (((uint32_t)time.tv_sec) ^ ((uint32_t)time.tv_usec) ^
	    ((uint32_t)seed64)      ^ ((uint32_t)(seed64 >> 32)));

    for (i = 0; i < node_len; i++) {
	uint32_t r = (uint32_t) rand_r (&seed);
	/* combine all bytes into the lowest byte */
	node[i] = (uint8_t)((r) ^ (r >> 8) ^ (r >> 16) ^ (r >> 24));
    }
}

static void
uuid_fill_node (uint8_t* node, size_t node_len)
{
    if (uuid_urand_node (node, node_len)) {
	gu_warn ("Node part of the UUID will be pseudorandom.");
	uuid_rand_node (node, node_len);
    }
}

void
gcs_uuid_generate (gcs_uuid_t* uuid, const uint8_t* node, size_t node_len)
{
    uint32_t*  uuid32 = (uint32_t*) uuid->data;
    uint16_t*  uuid16 = (uint16_t*) uuid->data;
    uint64_t   uuid_time;
    uint16_t   clock_seq = 0; // for possible future use

    assert (NULL != uuid);
    assert (NULL == node || 0 != node_len);

    /* system time */
    uuid_time = uuid_get_time ();

    /* time_low */
    uuid32[0] = gu_be32 (uuid_time & 0xFFFFFFFF);
    /* time_mid */
    uuid16[2] = gu_be16 ((uuid_time >> 32) & 0xFFFF);
    /* time_high_and_version */
    uuid16[3] = gu_be16 (((uuid_time >> 48) & 0x0FFF) | (1 << 12));
    /* clock_seq_and_reserved */
    uuid16[4] = gu_be16 ((clock_seq & 0x3FFF) | 0x0800);
    /* node */
    if (NULL != node && 0 != node_len) {
	memcpy (&uuid->data[10], node, node_len > UUID_NODE_LEN ?
		UUID_NODE_LEN : node_len);
    } else {
	uuid_fill_node (&uuid->data[10], UUID_NODE_LEN);
    }
    return;
}

/** 
 * Compare two UUIDs
 * @return -1, 0, 1 if left is respectively less, equal or greater than right
 */
int
gcs_uuid_compare (const gcs_uuid_t* left,
                  const gcs_uuid_t* right)
{
    return memcmp (left, right, sizeof(gcs_uuid_t));
}

static uint64_t
uuid_time (const gcs_uuid_t* uuid)
{
    uint64_t uuid_time;

    /* time_high_and_version */
    uuid_time = gu_be16 (((uint16_t*)uuid->data)[3]) & 0x0FFF;
    /* time_mid */
    uuid_time = (uuid_time << 16) + gu_be16 (((uint16_t*)uuid->data)[2]);
    /* time_low */
    uuid_time = (uuid_time << 32) + gu_be32 (((uint32_t*)uuid->data)[0]);

    return uuid_time;
}

/** 
 * Compare ages of two UUIDs
 * @return -1, 0, 1 if left is respectively older, equal or younger than right
 */
int
gcs_uuid_older (const gcs_uuid_t* left,
		const gcs_uuid_t* right)
{
    uint64_t time_left  = uuid_time (left);
    uint64_t time_right = uuid_time (right);

    if (time_left < time_right) return -1;
    if (time_left > time_right) return  1;
    return 0;
}

