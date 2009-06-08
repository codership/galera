/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#include <cerrno>
#include <sys/mman.h>

#include <galerautils.hpp>
#include "MMap.hpp"

namespace gcache
{
    MMap::MMap (const FileDescriptor& fd)
        : ptr (mmap (NULL, fd.get_size(), PROT_READ|PROT_WRITE,
                       MAP_SHARED|MAP_NORESERVE|MAP_POPULATE, fd.get(), 0)),
          size  (fd.get_size())
    {
        if (ptr == MAP_FAILED) {
            int err = errno;
            std::string msg ("mmap() on '" + fd.get_name() + "' failed: " +
                             strerror(err));
            throw gu::Exception (msg.c_str(), err);
        }

        mapped = true;

        log_debug << "Memory mapped: " << ptr << " (" << size << " bytes)";
    }

    void
    MMap::sync () const
    {
        log_info << "Flushing memory map to disk...";

        if (msync (ptr, size, MS_SYNC) < 0) {
            int err = errno;
            std::ostringstream msg;
            msg << "msync(" << ptr << ", " << size << ") failed: "
                << strerror(err);
            throw gu::Exception (msg.str().c_str(), err);
        }
    }

    void
    MMap::unmap ()
    {
        if (munmap (ptr, size) < 0) {
            int err = errno;
            std::ostringstream msg;
            msg << "munmap(" << ptr << ", " << size << ") failed: "
                << strerror(err);
            throw gu::Exception (msg.str().c_str(), err);
        }

        mapped = false;

        log_debug << "Memory unmapped: " << ptr << " (" << size <<" bytes)";
    }

    MMap::~MMap ()
    {
        if (mapped) unmap();
    }
}
