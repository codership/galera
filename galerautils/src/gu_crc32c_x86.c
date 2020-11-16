/*
 * Copyright (C) 2020 Codership Oy <info@codership.com>
 */

/**
 * @file Hardware-accelerated implementation of CRC32C algorithm using Intel's
 *       x86 instructions.
 *
 * Defines gu_crc32c_hardware() that returns pointer to gu_crc32c_func_t if
 * available on a given CPU.
 */

#include "gu_crc32c.h"

#if defined(GU_CRC32C_X86)

#include "gu_log.h"

#include <assert.h>
#include <stdbool.h>

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

#include <cpuid.h>

static uint32_t
x86_cpuid(uint32_t input)
{
    uint32_t eax, ebx, ecx, edx;
    if (__get_cpuid(input, &eax, &ebx, &ecx, &edx))
        return ecx;
    else
        return 0;
}

gu_crc32c_func_t
gu_crc32c_hardware()
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
