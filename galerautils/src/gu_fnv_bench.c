// Copyright (C) 2012 Codership Oy <info@codership.com>

/*!
 * @file Benchmark for different hash implementations:
 *       fnv32, fnv64, fnv128, mmh3, md5 from libssl and md5 from crypto++
 *
 * To compile on Ubuntu:
  g++ -DHAVE_ENDIAN_H -DHAVE_BYTESWAP_H -DGALERA_LOG_H_ENABLE_CXX \
  -O3 -march=native -msse4 -Wall -Werror -I../.. gu_fnv_bench.c \
  gu_mmh3.c gu_spooky.c gu_log.c gu_crc32c.c gu_crc32c_x86.c \
  -lssl -lcrypto -lcrypto++ -o gu_fnv_bench
 *
 * on CentOS some play with -lcrypto++ may be needed (also see includes below)
 *
 * To run:
 * gu_fnv_bench <buffer size> <N loops>
 */

#include "gu_crc32c.h"
#include "gu_fnv.h"
#include "gu_mmh3.h"
#include "gu_spooky.h"
#include "gu_hash.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <errno.h>

#include <openssl/md5.h>

#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1
#include <crypto++/md5.h>

enum algs
{
    CRC32sw,
    CRC32hw,
    FNV32,
    FNV64,
    FNV128,
    MMH32,
    MMH128,
    SPOOKYS,
    SPOOKY,
    MD5SSL,
    MD5CPP,
    FAST128,
    TABLE
};

static int timer (const void* const buf, ssize_t const length,
                  size_t const loops, enum algs const type)
{
    double begin, end;
    struct timeval tv;
    const char* alg = "undefined";
    size_t volatile h; // this variable serves to prevent compiler from
                       // optimizing out the calls
    size_t   const modulo    = sizeof(void*) - 1;
    size_t   const len       = length - modulo; // correct length for offset
    const uint8_t* const ptr = (const uint8_t*)buf;

    gettimeofday (&tv, NULL); begin = (double)tv.tv_sec + 1.e-6 * tv.tv_usec;

#ifdef EXTERNAL_LOOP
#define EXTERNAL_LOOP_BEGIN for (size_t i = 0; i < loops; i++) { \
        size_t const off = i & modulo;
#define EXTERNAL_LOOP_END   }
#define INTERNAL_LOOP_BEGIN
#define INTERNAL_LOOP_END
#else
#define EXTERNAL_LOOP_BEGIN
#define EXTERNAL_LOOP_END
#define INTERNAL_LOOP_BEGIN for (size_t i = 0; i < loops; i++) { \
        size_t const off = i & modulo;
#define INTERNAL_LOOP_END   }
#endif

    EXTERNAL_LOOP_BEGIN
    switch (type) {
    case CRC32sw:
    case CRC32hw:
    {
        if (CRC32sw == type) alg = "crc32sw"; else alg = "crc32hw";
        INTERNAL_LOOP_BEGIN
            h = gu_crc32c (ptr + off, len);
        INTERNAL_LOOP_END
        break;
    }
    case FNV32:
    {
        alg = "fnv32a";
        INTERNAL_LOOP_BEGIN
            uint32_t hash = GU_FNV32_SEED;
            gu_fnv32a_internal (ptr + off, len, &hash);
            h = hash;
        INTERNAL_LOOP_END
        break;
    }
    case FNV64:
    {
        alg = "fnv64a";
        INTERNAL_LOOP_BEGIN
            uint64_t hash = GU_FNV64_SEED;;
            gu_fnv64a_internal (ptr + off, len, &hash);
            h = hash;
        INTERNAL_LOOP_END
        break;
    }
    case FNV128:
    {
        alg = "fnv128";
        INTERNAL_LOOP_BEGIN
            gu_uint128_t hash = GU_FNV128_SEED;
            gu_fnv128a_internal (ptr + off, len, &hash);
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
            h = gu_mmh32 (ptr + off, len);
        INTERNAL_LOOP_END
        break;
    }
    case MMH128:
    {
        alg = "mmh128";
        INTERNAL_LOOP_BEGIN
            gu_uint128_t hash;
            gu_mmh128 (ptr + off, len, &hash);
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
            uint64_t hash[2];
            gu_spooky_short (ptr + off, len, hash);
            h = hash[0];
        INTERNAL_LOOP_END
        break;
    }
    case SPOOKY:
    {
        alg = "Spooky";
        INTERNAL_LOOP_BEGIN
            uint64_t hash[2];
            gu_spooky_inline (ptr + off, len, hash);
            h = hash[0];
        INTERNAL_LOOP_END
        break;
    }
    case MD5SSL:
    {
        alg = "md5ssl";
        INTERNAL_LOOP_BEGIN
            unsigned char md[MD5_DIGEST_LENGTH];
            MD5 (ptr + off, len, md);
        INTERNAL_LOOP_END
        break;
    }
    case MD5CPP:
    {
        alg = "md5cpp";
        INTERNAL_LOOP_BEGIN
            unsigned char md[16];
            CryptoPP::Weak::MD5().CalculateDigest(md, ptr + off, len);
        INTERNAL_LOOP_END
        break;
    }
    case FAST128:
    {
        alg = "fast128";
        INTERNAL_LOOP_BEGIN
            uint64_t hash[2];
            gu_fast_hash128 (ptr + off, len, hash);
            h = hash[0];
        INTERNAL_LOOP_END
        break;
    }
    case TABLE:
    {
        alg = "table";
        INTERNAL_LOOP_BEGIN
            h = gu_table_hash (ptr + off, len);
        INTERNAL_LOOP_END
        break;
    }
    }
    EXTERNAL_LOOP_END

    gettimeofday (&tv, NULL); end   = (double)tv.tv_sec + 1.e-6 * tv.tv_usec;

    end -= begin;
    return printf ("%s: %zu loops, %6.3f seconds, %8.3f Mb/sec%s\n",
                   alg, loops, end, ((double)loops * len)/end/1024/1024,
                   h ? "" : " ");
}

int main (int argc, char* argv[])
{
    ssize_t buf_size = (1<<20); // 1Mb
    long long loops = 1000;

    if (argc > 1) buf_size = strtoll (argv[1], NULL, 10);
    if (argc > 2) loops    = strtoll (argv[2], NULL, 10);

    /* initialization of data buffer */
    ssize_t buf_size_int = buf_size / sizeof(int) + 1;
    int* buf = (int*) malloc (buf_size_int * sizeof(int));
    if (!buf) return ENOMEM;
    while (buf_size_int) buf[--buf_size_int] = rand();

    gu_crc32c_configure(); // compute lookup tables
#ifndef GU_CRC32C_NO_HARDWARE
    gu_crc32c_func = gu_crc32c_hardware(); // try hardware CRC32C
    if (gu_crc32c_func) timer(buf, buf_size, loops, CRC32hw);
    gu_crc32c_func = gu_crc32c_slicing_by_8; // force software CRC32C
#endif
    timer (buf, buf_size, loops, CRC32sw);

    timer (buf, buf_size, loops, FNV32);
    timer (buf, buf_size, loops, FNV64);
    timer (buf, buf_size, loops, FNV128);
    timer (buf, buf_size, loops, MMH32);
    timer (buf, buf_size, loops, MMH128);
//    timer (buf, buf_size, loops, SPOOKYS);
    timer (buf, buf_size, loops, SPOOKY);
//    timer (buf, buf_size, loops, MD5SSL);
    timer (buf, buf_size, loops, MD5CPP);
    timer (buf, buf_size, loops, FAST128);
    timer (buf, buf_size, loops, TABLE);

    return 0;
}

