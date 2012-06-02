// Copyright (C) 2012 Codership Oy <info@codership.com>

/*!
 * @file Benchmark for different hash implementations:
 *       fnv32, fnv64, fnv128, mmh3, md5 from libssl and md5 from crypto++
 *
 * To compile:
 * g++ -DHAVE_ENDIAN_H -DHAVE_BYTESWAP_H -O3 -Wall -Werror -march=core2 \
 *     gu_fnv_bench.c gu_mmh3.c -lssl/-lcrypto -lcrypto++
 *
 * To run:
 * gu_fnv_bench <buffer size> <N loops>
 */

#include "gu_fnv.h"
#include "gu_mmh3.h"
#include "gu_spooky.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <errno.h>

#include <openssl/md5.h>

#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1
#include <crypto++/md5.h>
//#include <cryptopp/md5.h>

enum algs
{
    FNV32,
    FNV64,
    FNV128,
    MMH32,
    MMH128,
    SPOOKYS,
    SPOOKY,
    MD5SSL,
    MD5CPP
};

static int timer (const void* const buf, ssize_t const len,
                  long long const loops, enum algs const type)
{
    double begin, end;
    struct timeval tv;
    const char* alg = "undefined";
    size_t volatile h; // this variable serves to prevent compiler from
                       // optimizing out the calls

    gettimeofday (&tv, NULL); begin = (double)tv.tv_sec + 1.e-6 * tv.tv_usec;

    long long i;

#ifdef EXTERNAL_LOOP
#define EXTERNAL_LOOP_BEGIN for (i = 0; i < loops; i++) {
#define EXTERNAL_LOOP_END   }
#define INTERNAL_LOOP_BEGIN
#define INTERNAL_LOOP_END
#else
#define EXTERNAL_LOOP_BEGIN
#define EXTERNAL_LOOP_END
#define INTERNAL_LOOP_BEGIN for (i = 0; i < loops; i++) {
#define INTERNAL_LOOP_END   }
#endif

    EXTERNAL_LOOP_BEGIN
    switch (type) {
    case FNV32:
    {
        alg = "fnv32a";
        INTERNAL_LOOP_BEGIN
            uint32_t hash = GU_FNV32_SEED;
            gu_fnv32a (buf, len, &hash);
            h = hash;
        INTERNAL_LOOP_END
        break;
    }
    case FNV64:
    {
        alg = "fnv64a";
        INTERNAL_LOOP_BEGIN
            uint64_t hash = GU_FNV64_SEED;;
            gu_fnv64a (buf, len, &hash);
            h = hash;
        INTERNAL_LOOP_END
        break;
    }
    case FNV128:
    {
        alg = "fnv128";
        INTERNAL_LOOP_BEGIN
            gu_uint128_t hash = GU_FNV128_SEED;
            gu_fnv128a (buf, len, &hash);
#if defined(__SIZEOF_INT128__)
            h = hash;
#else
            h = hash.u32[GU_32LO];
#endif
        INTERNAL_LOOP_END
        break;
    }
    case MMH32:
    {
        alg = "mmh32";
        INTERNAL_LOOP_BEGIN
            uint32_t hash;
            gu_mmh32 (buf, len, &hash);
            h = hash;
        INTERNAL_LOOP_END
        break;
    }
    case MMH128:
    {
        alg = "mmh128";
        INTERNAL_LOOP_BEGIN
            gu_uint128_t hash;
            gu_mmh128 (buf, len, &hash);
#if defined(__SIZEOF_INT128__)
            h = hash;
#else
            h = hash.u32[GU_32LO];
#endif
        INTERNAL_LOOP_END
        break;
    }
    case SPOOKYS:
    {
        alg = "SpookyS";
        INTERNAL_LOOP_BEGIN
            uint64_t h1 = 0;
            uint64_t h2 = 0;
            gu_spooky_short (buf, len, &h1, &h2);
            h = h1;
        INTERNAL_LOOP_END
        break;
    }
    case SPOOKY:
    {
        alg = "Spooky";
        INTERNAL_LOOP_BEGIN
            uint64_t h1 = 0;
            uint64_t h2 = 0;
            gu_spooky (buf, len, &h1, &h2);
            h = h1;
        INTERNAL_LOOP_END
        break;
    }
    case MD5SSL:
    {
        alg = "md5ssl";
        INTERNAL_LOOP_BEGIN
            unsigned char md[MD5_DIGEST_LENGTH];
            MD5 ((const unsigned char*)buf, len, md);
        INTERNAL_LOOP_END
        break;
    }
    case MD5CPP:
    {
        alg = "md5cpp";
        INTERNAL_LOOP_BEGIN
            unsigned char md[16];
            CryptoPP::Weak::MD5().CalculateDigest(md, (const byte*)buf, len);
        INTERNAL_LOOP_END
        break;
    }
    }
    EXTERNAL_LOOP_END

    gettimeofday (&tv, NULL); end   = (double)tv.tv_sec + 1.e-6 * tv.tv_usec;

    end -= begin;
    return printf ("%s: %lld loops, %6.3f seconds, %8.3f Mb/sec%s\n",
                   alg, loops, end, (double)(loops * len)/end/1024/1024,
                   h ? "" : " ");
}

int main (int argc, char* argv[])
{
    ssize_t buf_size = (1<<20); // 1Mb
    long long loops = 10000;

    if (argc > 1) buf_size = strtoll (argv[1], NULL, 10);
    if (argc > 2) loops    = strtoll (argv[2], NULL, 10);

    /* initialization of data buffer */
    ssize_t buf_size_int = buf_size / sizeof(int) + 1;
    int* buf = (int*) malloc (buf_size_int * sizeof(int));
    if (!buf) return ENOMEM;
    while (buf_size_int) buf[--buf_size_int] = rand();

    timer (buf, buf_size, loops, FNV32);
    timer (buf, buf_size, loops, FNV64);
    timer (buf, buf_size, loops, FNV128);
    timer (buf, buf_size, loops, MMH32);
    timer (buf, buf_size, loops, MMH128);
    timer (buf, buf_size, loops, SPOOKYS);
    timer (buf, buf_size, loops, SPOOKY);
    timer (buf, buf_size, loops, MD5SSL);
    timer (buf, buf_size, loops, MD5CPP);

    return 0;
}

