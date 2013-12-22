/*
 * Copyright (C) 2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gu_crc32c.h"
#include "gu_log.h"

#include <assert.h>

CRC32CFunctionPtr gu_crc32c_func = crc32cSlicingBy8; // some sensible default

void
gu_crc32c_configure()
{
    gu_crc32c_func = detectBestCRC32C();

#if !defined(CRC32C_NO_HARDWARE)
    if (gu_crc32c_func == crc32cHardware64 ||
        gu_crc32c_func == crc32cHardware32) {
        gu_info ("CRC-32C: using hardware acceleration.");
    }
    else
#endif /* !CRC32C_NO_HARDWARE */
    if (gu_crc32c_func == crc32cSlicingBy8) {
        gu_info ("CRC-32C: using \"slicing-by-8\" algorithm.");
    }
    else {
        gu_fatal ("unexpected CRC-32C implementation.");
        abort();
    }
}
