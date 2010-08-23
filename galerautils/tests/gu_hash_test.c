/* Copyright (C) 2009 Codership Oy <info@codership.com> */
// THIS IS UNFINISHED!
#include <string.h>
#include <stdio.h>
#include "../src/galerautils.h"
#include "../src/gu_fnv.h"

#define SAMPLE_LEN (1<<16)

int s0[SAMPLE_LEN] = { 0, };
int s1[SAMPLE_LEN] = { 0, };
int s2[SAMPLE_LEN] = { 0, };
int s3[SAMPLE_LEN] = { 0, };

int*   samples[]   = { s0, s1, s2, s3 };
size_t samples_len = sizeof(samples)/sizeof(samples[0]);

void clear()
{
    size_t i;
    for (i = 0; i < samples_len; i++)
        memset (samples[i], 0, SAMPLE_LEN*sizeof(*samples[i]));
}

#define DIST_LEN SAMPLE_LEN
int dist[DIST_LEN];

long print_histogram (int dist[], long max)
{
    size_t i;
    int    max_dist  = dist[1];
    int    max_scale = 10;
    double scale     = 60; // hist width in symbols

    for (i = 1; i < DIST_LEN; i++)
        max_dist = max_dist < dist[i] ? dist[i] : max_dist;

    while (max_scale < max_dist) max_scale *= 10;

    scale /= max_scale;
    max = max < DIST_LEN ? max : DIST_LEN - 1;

    printf ("Distribution:\n");
    for (i = 1; i <= max; i++) {
        int j;
        printf ("%5zu: %5d ", i, dist[i]);
        for (j = 0; j < (int)(scale * dist[i] + 0.5); j++) printf ("#");
        printf ("\n");
    }

    return 0;
}

static long analyze_sample (int s[], uint64_t tries)
{
    size_t i;
    long max = 0;
    size_t max_i = 0;
    long total = 0;

    memset (dist, 0, DIST_LEN*sizeof(dist[0]));

    for (i = 0; i < SAMPLE_LEN; i++) {
        s[i]  -= (s[i] != 0); // remove single occurences
        total +=  s[i];
        if (s[i] < DIST_LEN) dist[s[i]]++;
        max = max < s[i] ? (max_i = i, s[i]) : max;
    }

    printf ("Total collisions: %ld (%f)\n", total, ((double)total)/tries);
    printf ("Max collisions: %ld at %04zX\n", max, max_i);
    print_histogram (dist, max);
    return max;
}

void analyze (uint64_t tries)
{
    size_t i;

    for (i = 0; i < samples_len; i++) analyze_sample (samples[i], tries);
}

#if 0
static void validate (uint16_t* h, uint64_t hsh)
{
    uint16_t h0 =  hash & 0x000000000000ffffULL;
    uint16_t h1 = (hash & 0x00000000ffff0000ULL) >> 16;
    uint16_t h2 = (hash & 0x0000ffff00000000ULL) >> 32;
    uint16_t h3 = (hash & 0xffff000000000000ULL) >> 48;

    if (h[0] != h0 || h[1] != h1 || h[2] != h2 || h[3] != h3) {
        printf ("h[0] = %04hX, h0 = %04hX\n", h[0], h0);
        printf ("h[1] = %04hX, h1 = %04hX\n", h[1], h1);
        printf ("h[2] = %04hX, h2 = %04hX\n", h[2], h2);
        printf ("h[3] = %04hX, h3 = %04hX\n", h[3], h3);
        puts ("-");
    }
}
#endif

#define TEST_DIST(hash_name,hash_len)                                   \
    {                                                                   \
        clock_t  start;                                                 \
        double   spent;                                                 \
        long     iter;                                                  \
        long     iter_max = 1ULL << 8;                                  \
        uint64_t msg[] = { GU_FNV_64_INIT, GU_FNV_64_INIT };            \
        size_t   msg_size = 8;                                          \
        uint64_t p = 1;                                                 \
        clear();                                                        \
        start = clock();                                                \
        for (iter = 0; iter < iter_max; iter++, p <<= 1, val ^= p) {    \
            size_t i;                                                   \
            uint64_t hash = hash_name(msg, msg_size);                   \
            for (i = 0; i < (hash_len >> 1); i++) {                     \
                samples[i][(uint16_t)hash]++;                           \
                hash >>= 16;                                            \
            }                                                           \
            if (!p) { msg[0] *= msg[0]; p = 1; }                        \
        }                                                               \
        spent = gu_clock_diff (clock(), start);                         \
        printf ("Spent %f seconds (%f bytes/second)\n",                 \
                spent, (1.0/spent) * iter_max * val_size);              \
        analyze (iter_max);                                             \
    }

#define TEST_SPEED(hash_name, msg, msg_len)                             \
    {                                                                   \
        volatile uint64_t hash = 0;                                     \
        clock_t  start;                                                 \
        double   spent;                                                 \
        long     iter;                                                  \
        long     iter_max = 1ULL << 28;                                 \
        start = clock();                                                \
        for (iter = 0; iter < iter_max; iter++) {                       \
            hash = hash_name(msg, msg_len);                             \
        }                                                               \
        spent = gu_clock_diff (clock(), start);                         \
        printf ("Spent %f seconds (%f bytes/second)\n",                 \
                spent, (1.0/spent) * iter_max * msg_len);               \
    }

int main (int argc, char* argv[])
{
    uint64_t msg[32] = { 1, 2, };

    printf ("FNV-1a\n");
    TEST_SPEED (gu_fnv1a, msg, sizeof(msg));

//    printf ("FNV-1a optimized\n");
//    TEST (gu_fnv1a_opt, 0);

    printf ("FNV-1a 2-byte opt\n");
    TEST_SPEED (gu_fnv1a_2, msg, sizeof(msg));

    return 0;
}
