/*
 * Copyright (C) 2009-2010 Codership Oy <info@codership.com>
 *
 */

#include "gcache_fd.hpp"

#include <galerautils.hpp>

#if !defined(_XOPEN_SOURCE) && !defined(__APPLE__)
#define _XOPEN_SOURCE 600
#endif

#include <cerrno>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef O_CLOEXEC // CentOS < 6.0 does not have it
#define O_CLOEXEC 0
#endif

#ifndef O_NOATIME
#define O_NOATIME 0
#endif

namespace gcache
{
    static const int OPEN_FLAGS   = O_RDWR | O_NOATIME | O_CLOEXEC;
    static const int CREATE_FLAGS = OPEN_FLAGS | O_CREAT /*| O_TRUNC*/;

    FileDescriptor::FileDescriptor (const std::string& fname,
                                    bool               sync_)
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
    FileDescriptor::constructor_common()
    {
        if (value < 0) {
            gu_throw_error(errno) << "Failed to open file '" + name + '\'';
        }
#if !defined(__APPLE__) /* Darwin does not have posix_fadvise */
/* benefits are questionable
        int err(posix_fadvise (value, 0, size, POSIX_FADV_SEQUENTIAL));

        if (err != 0)
        {
            log_warn << "Failed to set POSIX_FADV_SEQUENTIAL on "
                     << name << ": " << err << " (" << strerror(err) << ")";
        }
*/
#endif
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
    FileDescriptor::flush () const
    {
        log_debug << "Flushing file '" << name << "'";

        if (fsync (value) < 0) {
            gu_throw_error(errno) << "fsync() failed on '" + name + '\'';
        }

        log_debug << "Flushed file '" << name << "'";
    }

    bool
    FileDescriptor::write_byte (off_t offset)
    {
        unsigned char const byte (0);

        if (lseek (value, offset, SEEK_SET) != offset)
            gu_throw_error(errno) << "lseek() failed on '" << name << '\'';

        if (write (value, &byte, sizeof(byte)) != sizeof(byte))
            gu_throw_error(errno) << "write() failed on '" << name << '\'';

        return true;
    }

    /*! prealloc() fallback */
    void
    FileDescriptor::write_file (off_t const start)
    {
        off_t const page_size (sysconf (_SC_PAGE_SIZE));

        // last byte of the start page
        off_t offset = (start / page_size + 1) * page_size - 1;

        log_info << "Preallocating " << (size - start) << '/' << size
                 << " bytes in '" << name << "'...";

        while (offset < size && write_byte (offset))
        {
            offset += page_size;
        }

        if (offset >= size && write_byte (size - 1) && fsync (value) == 0)
        {
            return;
        }

        gu_throw_error (errno) << "File preallocation failed";
    }

    void
    FileDescriptor::prealloc(off_t const start)
    {
        off_t const diff (size - start);

        log_info << "Preallocating " << diff << '/' << size << " bytes in '"
                 << name << "'...";

#if defined(__APPLE__)
        if (0 != fcntl (value, F_SETSIZE, size) && 0 != ftruncate (value, size))
#else
        if (0 != posix_fallocate (value, start, diff))
#endif
        {
            if (EINVAL == errno && start >= 0 && diff > 0)
            {
                // FS does not support the operation, try physical write
                write_file (start);
            }
            else
            {
                gu_throw_error (errno) << "File preallocation failed";
            }
        }
    }
}
