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
    MMap::MMap (const FileDescriptor& fd)
        : value (mmap (NULL, fd.get_size(), PROT_READ|PROT_WRITE,
                       MAP_PRIVATE|MAP_NORESERVE|MAP_POPULATE, fd.get(), 0)),
          size  (fd.get_size())
    {
        if (value == MAP_FAILED) {
            int err = errno;
            std::string msg ("mmap() on '" + fd.get_name() + "' failed: " +
                             strerror(err));
            throw Exception (msg.c_str(), err);
        }

        mapped = true;

        log_debug << "Memory mapped: " << value << " (" << size << " bytes)";
    }

    void
    MMap::sync () const
    {
        log_info << "Flushing memory map to disk...";

        if (msync (value, size, MS_SYNC) < 0) {
            int err = errno;
            std::ostringstream msg;
            msg << "msync(" << value << ", " << size << ") failed: "
                << strerror(err);
            throw Exception (msg.str().c_str(), err);
        }
    }

    void
    MMap::unmap ()
    {
        if (munmap (value, size) < 0) {
            int err = errno;
            std::ostringstream msg;
            msg << "munmap(" << value << ", " << size << ") failed: "
                << strerror(err);
            throw Exception (msg.str().c_str(), err);
        }

        mapped = false;

        log_debug << "Memory unmapped: " << value << " (" << size <<" bytes)";
    }

    MMap::~MMap ()
    {
        if (mapped) unmap();
    }
}
