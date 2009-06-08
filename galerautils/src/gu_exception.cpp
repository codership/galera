/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#include <cstring>

#include "gu_exception.hpp"

namespace gu
{
    Exception::Exception (const char* msg_str, int err) throw()
        : _errno(err)
    {
        strncpy (msg, msg_str, GU_EXCEPTION_MSG_SIZE);
        msg[GU_EXCEPTION_MSG_SIZE - 1] = '\0';
    }
}
