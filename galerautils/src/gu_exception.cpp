/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#include <sstream>
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

    Exception::Exception (const char* msg_str, int err,
                          const char* file, const char* func, int line) throw()
        : _errno(err)
    {
        std::ostringstream tmp;

        if (file)    tmp << file;
        if (func)    tmp << ':'  << func;
        if (line)    tmp << ':'  << line;
        if (msg_str) tmp << ": " << msg_str;

        strncpy (msg, tmp.str().c_str(), GU_EXCEPTION_MSG_SIZE);

        msg[GU_EXCEPTION_MSG_SIZE - 1] = '\0';
    }
}
