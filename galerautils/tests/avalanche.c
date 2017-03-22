/*
 * Copyright (c) 2012 Codership Oy <www.codership.com>
 *
 * This program is to measure avalanche effect of different hash
 * implementations, for that it uses 1M of random 8-byte keys.
 * Use #define macro below to define the implementation to test.
 *
 * Compilation:

g++ -DHAVE_ENDIAN_H -DHAVE_BYTESWAP_H -O3 -Wall -Wno-unused avalanche.c \
gu_mmh3.c gu_spooky.c -o avalanche && time ./avalanche

 * Visualization in gnuplot:

unset cbtics
set xrange [-0.5:64.5]
set yrange [-0.5:64.5]
set cbrange [0.0:1.0]
set xlabel 'Hash bit'
set ylabel 'Flipped bit in message'
set cblabel 'Hash bit flip probability [0.0 - 1.0]'
set palette rgbformula 7,7,7
plot 'avalanche.out' matrix with image

 */

#include "gu_hash.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

uint64_t flip_count[64*64] = { 0, };

//#define HASH gu_mmh128_64
#define HASH gu_fast_hash64

int main (int argc, char* argv[])
{
    int n_keys = 1 << 20;

    int i, j, k;

    /* collect statistics */
    for (k = 0; k < n_keys; k++)
    {
        uint64_t key_part   = rand();
        uint64_t const key  = (key_part << 32) + rand();
        uint64_t const hash = HASH (&key, sizeof(key));

        for (j = 0; j < 64; j++)
        {
            uint64_t const flipped_key  = key ^ (GU_LONG_LONG(0x01) << j);
            uint64_t const flipped_hash =
                HASH (&flipped_key, sizeof(flipped_key));
            uint64_t flipped_bits = hash ^ flipped_hash;

            for (i = 0; i < 64; i++)
            {
                int const idx = j * 64 + i;
                flip_count[idx] += flipped_bits & GU_LONG_LONG(0x01);
                flipped_bits >>= 1;
            }
        }
    }

    /* print statistics */
    char out_name [256] = { 0, };
    snprintf(out_name, sizeof(out_name) - 1, "%s.out", argv[0]);
    FILE* const out = fopen(out_name, "w");
    if (!out)
    {
        fprintf (stderr, "Could not open file for writing: '%s': %d (%s)",
                 out_name, errno, strerror(errno));
        return errno;
    }

    uint64_t base = n_keys;
    double min_stat = 1.0;
    double max_stat = 0.0;
    for (j = 0; j < 64; j++)
    {
        for (i = 0; i < 64; i++)
        {
            int const idx = j * 64 + i;
            double stat = (((double)(flip_count[idx]))/base);
            min_stat = min_stat > stat ? stat : min_stat;
            max_stat = max_stat < stat ? stat : max_stat;
            fprintf (out, "%6.4f%c", stat, 63 == i ? '\n' : '\t');
        }
    }

    fclose(out);
    printf ("%6.4f : %6.4f (delta: %6.4f)\n",
            min_stat, max_stat, max_stat - min_stat);
    return 0;
}
