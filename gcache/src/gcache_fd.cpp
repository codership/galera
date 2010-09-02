/*
 * Copyright (C) 2009-2010 Codership Oy <info@codership.com>
 *
 */

#include "gcache_fd.hpp"

#include <galerautils.hpp>

#include <cerrno>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace gcache
{
    static const int OPEN_FLAGS   = O_RDWR | O_NOATIME;
    static const int CREATE_FLAGS = OPEN_FLAGS | O_CREAT | O_TRUNC;
 
    FileDescriptor::FileDescriptor (const std::string& fname, bool sync)
        throw (gu::Exception)
        : value (open (fname.c_str(), OPEN_FLAGS, S_IRUSR | S_IWUSR)),
          name  (fname),
          size  (lseek (value, 0, SEEK_END)),
          sync  (sync)
    {
        constructor_common();
    }

    FileDescriptor::FileDescriptor (const std::string& fname,
                                    size_t             length,
                                    bool               allocate,
                                    bool               sync)
        throw (gu::Exception)
        : value (open (fname.c_str(), CREATE_FLAGS, S_IRUSR | S_IWUSR)),
          name  (fname),
          size  (length),
          sync  (sync)
    {
        constructor_common();

        if (allocate)
        {
            prealloc ();           // reserve space
        }
        else
        {
            write_byte (size - 1); // reserve size
        }
    }

    void
    FileDescriptor::constructor_common() throw (gu::Exception)
    {
        if (value < 0) {
            gu_throw_error(errno) << "Failed to open file '" + name + '\'';
        }

        log_debug << "Opened file '" << name << "'";
        log_debug << "File descriptor: " << value;
    }

    FileDescriptor::~FileDescriptor ()
    {
        if (sync && fsync(value) != 0)
        {
            int err = errno;
            log_error << "Failed to flush file '" << name << "': "
                      << gu::to_string(err) << " (" << strerror(err) << '\'';
        }

        if (close(value) != 0)
        {
            int err = errno;
            log_error << "Failed to close file '" << name << "': "
                      << gu::to_string(err) << " (" << strerror(err) << '\'';
        }
        else
        {
            log_debug << "Closed  file '" << name << "'";
        }
    }

    void
    FileDescriptor::flush () const throw (gu::Exception)
    {
        log_debug << "Flushing file '" << name << "'";

        if (fsync (value) < 0) {
            gu_throw_error(errno) << "fsync() failed on '" + name + '\'';
        }

        log_debug << "Flushed file '" << name << "'";
    }

    bool
    FileDescriptor::write_byte (ssize_t offset) throw (gu::Exception)
    {
        unsigned char const byte = 0;

        if (lseek (value, offset, SEEK_SET) != offset)
            gu_throw_error(errno) << "lseek() failed on '" << name << '\'';

        if (write (value, &byte, sizeof(byte)) != sizeof(byte))
            gu_throw_error(errno) << "write() failed on '" << name << '\'';

        return true;
    }

    void
    FileDescriptor::prealloc() throw (gu::Exception)
    {
        size_t  const page_size = sysconf (_SC_PAGE_SIZE);

        size_t  offset = page_size - 1; // last byte of the page

        log_info << "Preallocating " << size << " bytes in '" << name
                 << "'...";

        while (offset < size && write_byte (offset))
        {
            offset += page_size;
        }

        if (offset > size && write_byte (size - 1) && fsync (value) == 0) {
            log_info << "Preallocating " << size << " bytes in '" << name
                     << "' done.";
            return;
        }

        gu_throw_error (errno) << "File preallocation failed";
    }
}
