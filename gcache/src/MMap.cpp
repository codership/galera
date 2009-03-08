/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#include <cerrno>
#include <sys/mman.h>

#include "Exception.hpp"
#include "Logger.hpp"
#include "MMap.hpp"

namespace gcache
{
    MMap::MMap (size_t length, const FileDescriptor& fd)
        : value(mmap (NULL, length, PROT_READ|PROT_WRITE,
                      MAP_PRIVATE|MAP_NORESERVE|MAP_POPULATE, fd.get(), 0)),
          length(length)
    {
        if (value == MAP_FAILED) {
            int err = errno;
            std::string msg ("mmap() on '" + fd.get_name() + "' failed: " +
                             strerror(err));
            throw Exception (msg.c_str(), err);
        }

        log_debug << "Memory mapped: " << value << " (" << length << " bytes)";
    }

    MMap::~MMap ()
    {
        int err = munmap (value, length);
        if (err < 0) {
            err = errno;
            std::string msg ("munmap() failed: ");
            msg = msg + strerror(err);
            throw Exception (msg.c_str(), err);
        }

        log_debug << "Memory unmapped: " << value << " (" << length<<" bytes)";
    }
}
