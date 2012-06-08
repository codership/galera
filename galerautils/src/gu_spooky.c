// Copyright (C) 2012 Codership Oy <info@codership.com>

/**
 * @file external Spooky hash implementation to avoid code bloat
 *
 * $Id$
 */

#include "gu_spooky.h"

void
gu_spooky128 (const void* const msg, size_t const len, void* res)
{
    gu_spooky_inline (msg, len, res);
}
