/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#include <cstring>

#include "gu_utils.hpp"
#include "gu_exception.hpp"

namespace gu
{
    void Exception::trace (const char* file, const char* func, int line)
    {
        msg.reserve (msg.length() + ::strlen(file) + ::strlen(func) + 15);
        msg += "\n\t at ";
        msg += file;
        msg += ':';
        msg += func;
        msg += "():";
        msg += to_string(line);
    }
}
