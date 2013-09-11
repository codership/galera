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

    if (gu_crc32c_func == crc32cHardware64 ||
        gu_crc32c_func == crc32cHardware32) {
        gu_info ("CRC-32C: using HW-accelerated implementation.");
    }
    else if (gu_crc32c_func == crc32cSlicingBy8) {
        gu_info ("CRC-32C: using \"slicing-by-8\" implementation.");
    }
    else {
        gu_warn ("unexpected CRC-32C implementation.");
        abort();
    }
}

