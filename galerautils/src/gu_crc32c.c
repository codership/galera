/*
 * Copyright (C) 2013-2020 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gu_crc32c.h"
#include "gu_log.h"
#include "gu_arch.h"     // GU_ASSERT_ALIGNMENT()
#include "gu_byteswap.h" // gu_le32()

static uint32_t crc32c_lut[8][256]; /* CRC32C lookup tables */

static void
crc32c_compute_lut()
{
    static uint32_t const P = 0x82f63b78; /* CRC32C polynomial */

    /* Generate LUT 0 */
    for (int i = 0; i < 256; i++)
    {
        uint32_t val = i;

        for (int j = 0; j < 8; j++) val = (val >> 1) ^ ((val & 1) * P);

        crc32c_lut[0][i] = val;
    }

    /* Generate LUTs  1 2 3 4 5 6 7 */
    for (int j = 0; j < 7; j++)
    {
        for (int i = 0; i < 256; i++)
        {
            uint32_t const val = crc32c_lut[j][i];
            crc32c_lut[j+1][i] = (val >> 8) ^ crc32c_lut[0][val & 0xFF];
        }
    }
}

#define GU_CRC32C_1BYTE_BLOCK(state, ptr)                               \
    state = (state >> 8) ^ crc32c_lut[0][(state ^ *ptr) & 0xFF];

/** Original one-byte-at-a-time lookup algorithm */
gu_crc32c_t
gu_crc32c_sarwate(gu_crc32c_t state, const void* data, size_t len)
{
    const uint8_t* ptr = (const uint8_t*)data;
    const uint8_t* const end = ptr + len;

    while (ptr < end)
    {
        GU_CRC32C_1BYTE_BLOCK(state, ptr);
        ptr++;
    }

    return state;
}

/** Unrolled processing of data shorter than 4 bytes */
static inline gu_crc32c_t
crc32c_3bytes(gu_crc32c_t state, const uint8_t* ptr, size_t len)
{
    assert(len < 4);

    switch (len)
    {
    case 3:
        GU_CRC32C_1BYTE_BLOCK(state, ptr);
        ptr++;
        /* fall through */
    case 2:
        GU_CRC32C_1BYTE_BLOCK(state, ptr);
        ptr++;
        /* fall through */
    case 1:
        GU_CRC32C_1BYTE_BLOCK(state, ptr);
        /* fall through */
    }

    return state;
}

/** Process initial misaligned bytes (there can be at most 3) and adjust
 *  ptr pointer and remaining length accordingly. */
static inline gu_crc32c_t
crc32c_lead_in(gu_crc32c_t state, const uint8_t** ptr, size_t* len)
{
    assert(*len >= 4);
    size_t const lead_in = (4 - (intptr_t)(*ptr)) & 0x3;
    assert((uintptr_t)(*ptr) & 0x3 || lead_in == 0);

    state = crc32c_3bytes(state, *ptr, lead_in);

    *len -= lead_in;
    *ptr += lead_in;

    return state;
}

#define GU_CRC32C_4BYTE_BLOCK(state, base)       \
    state =                                      \
    crc32c_lut[base + 3][(state      ) & 0xFF] ^ \
    crc32c_lut[base + 2][(state >>  8) & 0xFF] ^ \
    crc32c_lut[base + 1][(state >> 16) & 0xFF] ^ \
    crc32c_lut[base    ][(state >> 24)       ];

gu_crc32c_t
gu_crc32c_slicing_by_4(gu_crc32c_t state, const void* data, size_t len)
{
    const uint8_t* ptr = (const uint8_t*)data;

    if (len >= 4)
    {
        /* handle lead-in misaligned bytes */
        state = crc32c_lead_in(state, &ptr, &len);

        while (len >= 4)
        {
            const uint32_t* slice = (const uint32_t*)ptr;
            GU_ASSERT_ALIGNMENT(*slice);

            state ^= gu_le32(*slice);
            GU_CRC32C_4BYTE_BLOCK(state, 0);

            len -= 4;
            ptr += 4;
        }
    }

    assert(len < 4);

    /* handle trailing misalignment */
    return crc32c_3bytes(state, ptr, len);
}

gu_crc32c_t
gu_crc32c_slicing_by_8(gu_crc32c_t state, const void* data, size_t len)
{
    const uint8_t* ptr = (const uint8_t*)data;

    if (len >= 4)
    {
        /* handle lead-in misaligned bytes */
        state = crc32c_lead_in(state, &ptr, &len);

        while (len >= 8)
        {
            const uint32_t* slices = (const uint32_t*)ptr;
            GU_ASSERT_ALIGNMENT(*slices);

            gu_crc32c_t state0 = gu_le32(slices[0]) ^ state;
            GU_CRC32C_4BYTE_BLOCK(state0, 4);

            gu_crc32c_t state1 = gu_le32(slices[1]);
            GU_CRC32C_4BYTE_BLOCK(state1, 0);

            state = state0 ^ state1;

            len -= 8;
            ptr += 8;
        }

        if (len >= 4)
        {
            const uint32_t* slice = (const uint32_t*)ptr;
            GU_ASSERT_ALIGNMENT(*slice);

            state ^= gu_le32(*slice);
            GU_CRC32C_4BYTE_BLOCK(state, 0);

            len -= 4;
            ptr += 4;
        }
    }

    assert(len < 4);

    /* handle trailing misalignment */
    return crc32c_3bytes(state, ptr, len);
}

#if defined(GU_CRC32C_X86)
/*
 * CRC32C implementation using Intel's x86 instructions
 */

static inline gu_crc32c_t
crc32c_x86_tail3(gu_crc32c_t state, const uint8_t* ptr, size_t len)
{
    assert(len < 4);

    switch (len)
    {
    case 3:
        state = __builtin_ia32_crc32qi(state, *ptr);
        ptr++;
        /* fall through */
    case 2:
        state = __builtin_ia32_crc32hi(state, *(uint16_t*)ptr);
        break;
    case 1:
        state = __builtin_ia32_crc32qi(state, *ptr);
    }

    return state;
}

static inline gu_crc32c_t
crc32c_x86(gu_crc32c_t state, const uint8_t* ptr, size_t len)
{
    static size_t const arg_size = sizeof(uint32_t);

    /* apparently no ptr misalignment protection is needed */
    while (len >= arg_size)
    {
        state = __builtin_ia32_crc32si(state, *(uint32_t*)ptr);
        len -= arg_size;
        ptr += arg_size;
    }

    assert(len < 4);

    return crc32c_x86_tail3(state, ptr, len);
}

gu_crc32c_t
gu_crc32c_x86(gu_crc32c_t state, const void* data, size_t len)
{
    return crc32c_x86(state, (const uint8_t*)data, len);
}

#if defined(GU_CRC32C_X86_64)
gu_crc32c_t
gu_crc32c_x86_64(gu_crc32c_t state, const void* data, size_t len)
{
    const uint8_t* ptr = (const uint8_t*)data;

#ifdef __LP64__
    static size_t const arg_size = sizeof(uint64_t);
    uint64_t state64 = state;

    while (len >= arg_size)
    {
        state64 = __builtin_ia32_crc32di(state64, *(uint64_t*)ptr);
        len -= arg_size;
        ptr += arg_size;
    }

    state = (uint32_t)state64;
#endif /* __LP64__ */

    return crc32c_x86(state, ptr, len);
}
#endif /* GU_CRC32C_X86_64 */

static uint32_t
x86_cpuid(uint32_t input)
{
    uint32_t eax, ebx, ecx, edx;

    /* The code below adapted from http://en.wikipedia.org/wiki/CPUID
     * and seems to work for both PIC and non-PIC cases */
    __asm__ __volatile__(
#if defined(GU_CRC32C_X86_64)
        "pushq %%rbx     \n\t" /* save %rbx */
#else /* 32-bit */
        "pushl %%ebx     \n\t" /* save %ebx */
#endif

        "cpuid              \n\t"
        "movl %%ebx, %[ebx] \n\t" /* copy %ebx contents into output var */

#if defined(GU_CRC32C_X86_64)
        "popq %%rbx \n\t"      /* restore %rbx */
#else /* 32-bit */
        "popl %%ebx \n\t"      /* restore %ebx */
#endif
        : "=a"(eax), [ebx] "=r"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(input)
    );

    return ecx;
}

static gu_crc32c_func_t
crc32c_x86_hardware()
{
    static uint32_t const SSE42_BIT = 1 << 20;
    uint32_t const cpuid = x86_cpuid(1);
    bool const SSE42_present = cpuid & SSE42_BIT;

    if (SSE42_present)
    {
#if defined(GU_CRC32C_X86_64)
        gu_info ("CRC-32C: using 64-bit x86 acceleration.");
        return gu_crc32c_x86_64;
#else
        gu_info ("CRC-32C: using 32-bit x86 acceleration.");
        return gu_crc32c_x86;
#endif
    }
    else
    {
        return NULL;
    }
}
#endif /* GU_CRC32C_X86 */

static gu_crc32c_func_t
crc32c_best_algorithm()
{
    gu_crc32c_func_t ret = NULL;

#if defined(GU_CRC32C_X86)
    ret = crc32c_x86_hardware();
#endif /* GU_CRC32C_X86 */

    if (!ret)
    {
#if defined(__arm__) && GU_WORDSIZE == 32
        /* On 32-bit ARM slicing-by-4 seems to outperform slicing-by-8
         * by 1.1-1.15x */
        gu_info ("CRC-32C: using \"slicing-by-4\" algorithm.");
        ret = gu_crc32c_slicing_by_4;
#else
        /* On x86 slicing-by-8 seems to outperform slicing-by-4 by 1.2-1.7x */
        gu_info ("CRC-32C: using \"slicing-by-8\" algorithm.");
        ret = gu_crc32c_slicing_by_8;
#endif /* __arm__ */
    }

    return ret;
}

gu_crc32c_func_t gu_crc32c_func = NULL;

void
gu_crc32c_configure()
{
    crc32c_compute_lut();
    gu_crc32c_func = crc32c_best_algorithm();
}
