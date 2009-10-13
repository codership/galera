/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */
#ifndef SAFETY_PREFIX
#define SAFETY_PREFIX

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

#endif // SAFETY_PREFIX
