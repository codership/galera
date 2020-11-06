/*
 * Copyright (C) 2020 Codership Oy <info@codership.com>
 */

/**
 * @file Hardware-accelerated implementation of CRC32C algorithm using arm64
 *       instructions.
 *
 * Defines gu_crc32c_hardware() that returns pointer to gu_crc32c_func_t if
 * available on a given CPU.
 */

#include "gu_crc32c.h"

#if defined(GU_CRC32C_ARM64)

#include "gu_log.h"

#include <assert.h>
#include <stdbool.h>

#include <arm_acle.h>

static inline gu_crc32c_t
crc32c_arm64_tail7(gu_crc32c_t state, const uint8_t* ptr, size_t len)
{
    assert(len < 8);

    if (len >= 4)
    {
        state = __crc32cw(state, *(uint32_t *)ptr);
        ptr += 4;
        len -= 4;
    }

    switch (len)
    {
    case 3:
        state = __crc32cb(state, *ptr);
        ptr++;
        /* fall through */
    case 2:
        state = __crc32ch(state, *(uint16_t*)ptr);
        break;
    case 1:
        state = __crc32cb(state, *ptr);;
    }

    return state;
}

gu_crc32c_t
gu_crc32c_arm64(gu_crc32c_t state, const void* data, size_t len)
{
    static size_t const arg_size = sizeof(uint64_t);
    const uint8_t* ptr = (const uint8_t*)data;

    /* apparently no ptr misalignment protection is needed */
    while (len >= arg_size)
    {
        state = __crc32cd(state, *(uint64_t*)ptr);
        len -= arg_size;
        ptr += arg_size;
    }

    assert(len < 8);

    return crc32c_arm64_tail7(state, ptr, len);
}

#include <asm/hwcap.h>
#include <sys/auxv.h>

#if defined(HWCAP_CRC32)
#    define GU_AT_HWCAP    AT_HWCAP
#    define GU_HWCAP_CRC32 HWCAP_CRC32
#elif defined(HWCAP2_CRC32)
#    define GU_AT_HWCAP    AT_HWCAP2
#    define GU_HWCAP_CRC32 HWCAP2_CRC32
#endif /* HWCAP_CRC32 */

gu_crc32c_func_t
gu_crc32c_hardware()
{
#if defined(GU_AT_HWCAP)
    unsigned long int const hwcaps = getauxval(GU_AT_HWCAP);
    if (hwcaps & GU_HWCAP_CRC32)
    {
        gu_info ("CRC-32C: using hardware acceleration.");
        return gu_crc32c_arm64;
    }
    else
    {
        gu_info ("CRC-32C: hardware does not have CRC-32C capabilities.");
        return NULL;
    }
#else
    gu_info ("CRC-32C: compiled without hardware acceleration support.");
    return NULL;
#endif /* GU_AT_HWCAP */
}

#endif /* GU_CRC32C_ARM64 */
