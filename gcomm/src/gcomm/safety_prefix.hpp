/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef GCOMM_SAFETY_PREFIX
#define GCOMM_SAFETY_PREFIX

namespace gcomm
{
    enum SafetyPrefix
    {
        SP_DROP       = 0,
        SP_UNRELIABLE = 1,
        SP_FIFO       = 2,
        SP_AGREED     = 3,
        SP_SAFE       = 4
    };
}

#endif // GCOMM_SAFETY_PREFIX
