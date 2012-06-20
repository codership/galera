// Copyright (C) 2012 Codership Oy <info@codership.com>

/**
 * @file external Spooky hash implementation to avoid code bloat
 *
 * $Id$
 */

#include "gu_spooky.h"

void
gu_spooky128_host (const void* const msg, size_t const len, uint64_t* res)
{
    gu_spooky_inline (msg, len, res);
}
