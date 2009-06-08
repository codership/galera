/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#include <cstring>

#include "Exception.hpp"

namespace gcache
{
    Exception::Exception (const char* msg_str, int errno) throw()
        : _errno(errno)
    {
        strncpy (msg, msg_str, EXCEPTION_MSG_SIZE);
        msg[EXCEPTION_MSG_SIZE - 1] = '\0';
    }
///*
    Exception::Exception (const char* msg_str) throw()
        : _errno(0)
    {
        strncpy (msg, msg_str, EXCEPTION_MSG_SIZE);
        msg[EXCEPTION_MSG_SIZE - 1] = '\0';
    }
//*/
}
