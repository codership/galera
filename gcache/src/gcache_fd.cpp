/*
 * Copyright (C) 2009-2010 Codership Oy <info@codership.com>
 *
 */

#include "gcache_fd.hpp"

#include <galerautils.hpp>

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include <cerrno>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef O_CLOEXEC // CentOS does not have it
#define O_CLOEXEC 0
#endif

namespace gcache
{
    static const int OPEN_FLAGS   = O_RDWR | O_NOATIME | O_CLOEXEC;
    static const int CREATE_FLAGS = OPEN_FLAGS | O_CREAT /*| O_TRUNC*/;
 
    FileDescriptor::FileDescriptor (const std::string& fname,
                                    bool               sync_)
        throw (gu::Exception)
        : value (open (fname.c_str(), OPEN_FLAGS, S_IRUSR | S_IWUSR)),
          name  (fname),
          size  (lseek (value, 0, SEEK_END)),
          sync  (sync_)
    {
        constructor_common();
    }

    FileDescriptor::FileDescriptor (const std::string& fname,
                                    size_t             length,
                                    bool               allocate,
                                    bool               sync_)
        throw (gu::Exception)
        : value (open (fname.c_str(), CREATE_FLAGS, S_IRUSR | S_IWUSR)),
          name  (fname),
          size  (length),
          sync  (sync_)
    {
        constructor_common();

        off_t const current_size(lseek (value, 0, SEEK_END));

        if (current_size < size)
        {
            if (allocate)
            {
                // reserve space that hasn't been reserved
                prealloc (current_size);
            }
            else
            {
                write_byte (size - 1); // reserve size
            }
        }
        else if (current_size > size)
        {
            log_info << "Truncating '" << name << "' to " << size << " bytes.";

            if (ftruncate(value, size))
            {
                gu_throw_error(errno) << "Failed to truncate '" << name
                                      << "' to " << size << " bytes.";
            }
        }
        else
        {
            log_info << "Reusing existing '" << name << "'.";
        }
    }

    void
    FileDescriptor::constructor_common() throw (gu::Exception)
    {
        if (value < 0) {
            gu_throw_error(errno) << "Failed to open file '" + name + '\'';
        }
/* benefits are questionable
        int err(posix_fadvise (value, 0, size, POSIX_FADV_SEQUENTIAL));

        if (err != 0)
        {
            log_warn << "Failed to set POSIX_FADV_SEQUENTIAL on "
                     << name << ": " << err << " (" << strerror(err) << ")";
        }
*/
        log_debug << "Opened file '" << name << "'";
        log_debug << "File descriptor: " << value;
    }

    FileDescriptor::~FileDescriptor ()
    {
        if (sync && fsync(value) != 0)
        {
            int const err (errno);
            log_error << "Failed to flush file '" << name << "': "
                      << gu::to_string(err) << " (" << strerror(err) << '\'';
        }

        if (close(value) != 0)
        {
            int const err (errno);
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
    FileDescriptor::write_byte (off_t offset) throw (gu::Exception)
    {
        unsigned char const byte (0);

        if (lseek (value, offset, SEEK_SET) != offset)
            gu_throw_error(errno) << "lseek() failed on '" << name << '\'';

        if (write (value, &byte, sizeof(byte)) != sizeof(byte))
            gu_throw_error(errno) << "write() failed on '" << name << '\'';

        return true;
    }

#if 0
    void
    FileDescriptor::prealloc(off_t const start) throw (gu::Exception)
    {
        off_t const page_size (sysconf (_SC_PAGE_SIZE));

        // last byte of the start page
        off_t offset = (start / page_size) * page_size - 1;
        off_t const diff (size - offset);

        log_info << "Preallocating " << diff << '/' << size << " bytes in '"
                 << name << "'...";

        while (offset < size && write_byte (offset))
        {
            offset += page_size;
        }

        if (offset > size && write_byte (size - 1) && fsync (value) == 0) {
            log_info << "Preallocating " << diff << '/' << size
                     << " bytes in '" << name << "' done.";
            return;
        }

        gu_throw_error (errno) << "File preallocation failed";
    }
#endif

    void
    FileDescriptor::prealloc(off_t const start) throw (gu::Exception)
    {
        off_t const diff (size - start);

        log_info << "Preallocating " << diff << '/' << size << " bytes in '"
                 << name << "'...";

        if (0 != posix_fallocate (value, start, diff))
        {
            gu_throw_error (errno) << "File preallocation failed";
        }
    }
}
