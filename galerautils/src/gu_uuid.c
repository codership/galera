/*
 * Copyright (C) 2008-2017 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*
 * Universally Unique IDentifier. RFC 4122.
 * Time-based implementation.
 *
 */

#include "gu_uuid.h"
#include "gu_byteswap.h"
#include "gu_log.h"
#include "gu_assert.h"
#include "gu_threads.h"
#include "gu_time.h"
#include "gu_rand.h"

#include <stdlib.h>   // for rand_r()
#include <string.h>   // for memcmp()
#include <stdio.h>    // for fopen() et al
#include <sys/time.h> // for gettimeofday()
#include <unistd.h>   // for getpid()
#include <errno.h>    // for errno
#include <stddef.h>

#define UUID_NODE_LEN 6

/** Returns 64-bit system time in 100 nanoseconds */
static uint64_t
uuid_get_time ()
{
    static long long check = 0;
    static gu_mutex_t mtx = GU_MUTEX_INITIALIZER;
    long long t;

    gu_mutex_lock (&mtx);

    do {
        t = gu_time_calendar() / 100;
    }
    while (check == t);

    check = t;

    gu_mutex_unlock (&mtx);

    return (t + 0x01B21DD213814000LL);
    //          offset since the start of 15 October 1582
}

#ifndef UUID_URAND
// This function can't be called too often,
// apparently due to lack of entropy in the pool.
/** Fills node part of the uuid with true random data from /dev/urand */
static int
uuid_urand_node (uint8_t* node, size_t node_len)
{
    static const char urand_name[] = "/dev/urandom";
    FILE*       urand;
    size_t      i = 0;
    int         c;

    urand = fopen (urand_name, "r");

    if (NULL == urand) {
        gu_debug ("Failed to open %s for reading (%d).", urand_name, -errno);
        return -errno;
    }

    while (i < node_len && (c = fgetc (urand)) != EOF) {
        node[i] = (uint8_t) c;
        i++;
    }
    fclose (urand);

    return 0;
}
#else
#define uuid_urand_node(a,b) true
#endif

/** Fills node part with pseudorandom data from rand_r() */
static void
uuid_rand_node (uint8_t* node, size_t node_len)
{
    unsigned int seed = gu_rand_seed_int (gu_time_calendar(), node, getpid());
    size_t       i;

    for (i = 0; i < node_len; i++) {
        uint32_t r = (uint32_t) rand_r (&seed);
        /* combine all bytes into the lowest byte */
        node[i] = (uint8_t)((r) ^ (r >> 8) ^ (r >> 16) ^ (r >> 24));
    }
}

static inline void
uuid_fill_node (uint8_t* node, size_t node_len)
{
    if (uuid_urand_node (node, node_len)) {
        uuid_rand_node (node, node_len);
    }
}

void
gu_uuid_generate (gu_uuid_t* uuid, const void* node, size_t node_len)
{
    GU_ASSERT_ALIGNMENT(*uuid);
    assert (NULL != uuid);
    assert (NULL == node || 0 != node_len);

    uint32_t*  uuid32 = (uint32_t*) uuid->data;
    uint16_t*  uuid16 = (uint16_t*) uuid->data;
    uint64_t   uuid_time = uuid_get_time ();
    uint16_t   clock_seq = gu_rand_seed_int (uuid_time, &GU_UUID_NIL, getpid());

    /* time_low */
    uuid32[0] = gu_be32 (uuid_time & 0xFFFFFFFF);
    /* time_mid */
    uuid16[2] = gu_be16 ((uuid_time >> 32) & 0xFFFF);
    /* time_high_and_version */
    uuid16[3] = gu_be16 (((uuid_time >> 48) & 0x0FFF) | (1 << 12));
    /* clock_seq_and_reserved */
    uuid16[4] = gu_be16 ((clock_seq & 0x3FFF) | 0x8000);
    /* node */
    if (NULL != node && 0 != node_len) {
        memcpy (&uuid->data[10], node, node_len > UUID_NODE_LEN ?
                UUID_NODE_LEN : node_len);
    } else {
        uuid_fill_node (&uuid->data[10], UUID_NODE_LEN);
        uuid->data[10] |= 0x02; /* mark as "locally administered" */
    }

    return;
}

/**
 * Compare two UUIDs
 * @return -1, 0, 1 if left is respectively less, equal or greater than right
 */
int
gu_uuid_compare (const gu_uuid_t* left,
                 const gu_uuid_t* right)
{
    return memcmp (left, right, sizeof(gu_uuid_t));
}

static uint64_t
uuid_time (const gu_uuid_t* uuid)
{
    uint64_t uuid_time;

    union
    {
        uint16_t u16[4];
        uint32_t u32[2];
    } tmp;

    memcpy(&tmp, uuid, sizeof(tmp));

    /* time_high_and_version */
    uuid_time = gu_be16(tmp.u16[3]) & 0x0FFF;
    /* time_mid */
    uuid_time = (uuid_time << 16) + gu_be16(tmp.u16[2]);
    /* time_low */
    uuid_time = (uuid_time << 32) + gu_be32(tmp.u32[0]);

    return uuid_time;
}

/** 
 * Compare ages of two UUIDs
 * @return -1, 0, 1 if left is respectively younger, equal or older than right
 */
int
gu_uuid_older (const gu_uuid_t* left,
               const gu_uuid_t* right)
{
    GU_ASSERT_ALIGNMENT(*left);
    GU_ASSERT_ALIGNMENT(*right);

    uint64_t time_left  = uuid_time (left);
    uint64_t time_right = uuid_time (right);

    if (time_left < time_right) return 1;
    if (time_left > time_right) return -1;
    return 0;
}


ssize_t gu_uuid_print(const gu_uuid_t* uuid, char* buf, size_t buflen)
{
    GU_ASSERT_ALIGNMENT(*uuid);
    if (buflen < GU_UUID_STR_LEN) return -1;
    return sprintf(buf, GU_UUID_FORMAT, GU_UUID_ARGS(uuid));
}


ssize_t gu_uuid_scan(const char* buf, size_t buflen, gu_uuid_t* uuid)
{
    GU_ASSERT_ALIGNMENT(*uuid);
    ssize_t ret;
    if (buflen < GU_UUID_STR_LEN) return -1;
    ret = sscanf(buf, GU_UUID_FORMAT_SCANF, GU_UUID_ARGS_SCANF(uuid));
    if (ret != sizeof(uuid->data)) return -1;
    return ret;
}
