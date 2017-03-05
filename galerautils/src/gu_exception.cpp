/*
 * Copyright (C) 2009-2015 Codership Oy <info@codership.com>
 *
 */

#include <cstring>

#include "gu_utils.hpp"
#include "gu_exception.hpp"

namespace gu
{
    void Exception::trace (const char* file, const char* func, int line) const
    {
        msg_.reserve (msg_.length() + ::strlen(file) + ::strlen(func) + 15);
        msg_ += "\n\t at ";
        msg_ += file;
        msg_ += ':';
        msg_ += func;
        msg_ += "():";
        msg_ += to_string(line);
    }
}
