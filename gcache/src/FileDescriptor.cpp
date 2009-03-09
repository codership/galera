/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#include <cerrno>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "Exception.hpp"
#include "Logger.hpp"
#include "FileDescriptor.hpp"

namespace gcache
{
    static const int OPEN_FLAGS   = O_RDWR | O_NOATIME;
    static const int CREATE_FLAGS = OPEN_FLAGS | O_CREAT | O_TRUNC;
 
    FileDescriptor::FileDescriptor (const std::string& fname)
        : value(open (fname.c_str(), OPEN_FLAGS, S_IRUSR | S_IWUSR)),
          name(fname),
          size(lseek (value, 0, SEEK_END))
    {
        constructor_common();
    }

    FileDescriptor::FileDescriptor (const std::string& fname, size_t length)
        : value(open (fname.c_str(), CREATE_FLAGS, S_IRUSR | S_IWUSR)),
          name(fname),
          size(length)
    {
        constructor_common();
        prealloc();
    }

    void
    FileDescriptor::constructor_common()
    {
        if (value < 0) {
            int err = errno;
            std::string msg ("Failed to open cache file '" + name +
                             "': " + strerror (err));
            throw Exception (msg.c_str(), err);
        }

        log_info << "Opened file '" << name << "'";
        log_debug << "File descriptor: " << value;
    }

    FileDescriptor::~FileDescriptor ()
    {
        int err = 0;
        std::string msg;

        log_info << "Syncing file '" << name << "'";

        if (fsync(value) == 0) {
            if (close(value) == 0) {
                log_info << "Closed  file '" << name << "'";
                return;
            }
            else {
                err = errno;
                msg = "Failed to close file '" + name + "': " + strerror(err);
            }
        }
        else {
            err = errno;
            msg = "Failed to flush file '" + name + "': " + strerror(err);
        }

        throw Exception (msg.c_str(), err);
    }

    void
    FileDescriptor::sync () const
    {
        log_debug << "Syncing file '" << name << "'";

        if (fsync (value) < 0) {
            int err = errno;
            std::string msg("fsync() on '" + name + "' failed: " +
                            strerror(err));
            throw Exception (msg.c_str(), err);
        }

        log_debug << "Synced  file '" << name << "'";
    }

    void
    FileDescriptor::prealloc()
    {
        uint8_t const byte      = 0;
        size_t  const page_size = sysconf (_SC_PAGE_SIZE);

        size_t  offset = page_size - 1; // last byte of the page

        log_info << "Preallocating " << size << " bytes in '" << name
                 << "'...";

        while (offset < size &&
               lseek (value, offset, SEEK_SET) > 0 &&
               write (value, &byte, sizeof(byte)) == sizeof(byte)) {
            offset += page_size;
        }

        if (offset > size && fsync (value) == 0) {
            log_info << "Preallocating " << size << " bytes in '" << name
                     << "' done.";
            return;
        }

        int err = errno;
        std::string msg ("File preallocation failed: ");
        msg = msg + strerror (err);
        throw Exception (msg.c_str(), err);
    }
}
