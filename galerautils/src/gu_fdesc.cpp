/*
 * Copyright (C) 2009-2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gu_fdesc.hpp"

#include "gu_logger.hpp"
#include "gu_throw.hpp"

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include <cerrno>
#include <sys/stat.h>
#include <fcntl.h>
//#include <unistd.h>

#ifndef O_CLOEXEC // CentOS 5 does not have it
#define O_CLOEXEC 0
#endif

#ifndef O_NOATIME
#define O_NOATIME 0
#endif

namespace gu
{
    static int const OPEN_FLAGS   = O_RDWR | O_NOATIME | O_CLOEXEC;
    static int const CREATE_FLAGS = OPEN_FLAGS | O_CREAT /*| O_TRUNC*/;

    FileDescriptor::FileDescriptor (const std::string& fname,
                                    bool const          sync)
        throw (Exception)
        : name_(fname),
          fd_  (open (name_.c_str(), OPEN_FLAGS, S_IRUSR | S_IWUSR)),
          size_(lseek (fd_, 0, SEEK_END)),
          sync_(sync)
    {
        constructor_common();
    }

    FileDescriptor::FileDescriptor (const std::string& fname,
                                    size_t const       size,
                                    bool   const       allocate,
                                    bool   const       sync)
        throw (Exception)
        : name_(fname),
          fd_  (open (fname.c_str(), CREATE_FLAGS, S_IRUSR | S_IWUSR)),
          size_(size),
          sync_(sync)
    {
        constructor_common();

        off_t const current_size(lseek (fd_, 0, SEEK_END));

        if (current_size < size_)
        {
            if (allocate)
            {
                // reserve space that hasn't been reserved
                prealloc (current_size);
            }
            else
            {
                write_byte (size_ - 1); // reserve size
            }
        }
        else if (current_size > size_)
        {
            log_debug << "Truncating '" << name_<< "' to " << size_<< " bytes.";

            if (ftruncate(fd_, size_))
            {
                gu_throw_error(errno) << "Failed to truncate '" << name_
                                      << "' to " << size_ << " bytes.";
            }
        }
        else
        {
            log_debug << "Reusing existing '" << name_ << "'.";
        }
    }

    void
    FileDescriptor::constructor_common() throw (Exception)
    {
        if (fd_ < 0) {
            gu_throw_error(errno) << "Failed to open file '" + name_ + '\'';
        }
/* benefits are questionable
        int err(posix_fadvise (value, 0, size, POSIX_FADV_SEQUENTIAL));

        if (err != 0)
        {
            log_warn << "Failed to set POSIX_FADV_SEQUENTIAL on "
                     << name << ": " << err << " (" << strerror(err) << ")";
        }
*/
        log_debug << "Opened file '" << name_ << "'";
        log_debug << "File descriptor: " << fd_;
    }

    FileDescriptor::~FileDescriptor ()
    {
        if (sync_ && fsync(fd_) != 0)
        {
            int const err(errno);
            log_error << "Failed to flush file '" << name_ << "': "
                      << err << " (" << strerror(err) << '\'';
        }

        if (close(fd_) != 0)
        {
            int const err(errno);
            log_error << "Failed to close file '" << name_ << "': "
                      << err << " (" << strerror(err) << '\'';
        }
        else
        {
            log_debug << "Closed  file '" << name_ << "'";
        }
    }

    void
    FileDescriptor::flush () const throw (Exception)
    {
        log_debug << "Flushing file '" << name_ << "'";

        if (fsync (fd_) < 0) {
            gu_throw_error(errno) << "fsync() failed on '" + name_ + '\'';
        }

        log_debug << "Flushed file '" << name_ << "'";
    }

    bool
    FileDescriptor::write_byte (off_t offset) throw (gu::Exception)
    {
        byte_t const byte (0);

        if (lseek (fd_, offset, SEEK_SET) != offset)
            gu_throw_error(errno) << "lseek() failed on '" << name_ << '\'';

        if (write (fd_, &byte, sizeof(byte)) != sizeof(byte))
            gu_throw_error(errno) << "write() failed on '" << name_ << '\'';

        return true;
    }

    /*! prealloc() fallback */
    void
    FileDescriptor::write_file (off_t const start) throw (gu::Exception)
    {
        off_t const page_size (sysconf (_SC_PAGE_SIZE));

        // last byte of the start page
        off_t offset = (start / page_size + 1) * page_size - 1;
//        off_t const diff (size - offset);

//        log_info << "Preallocating " << diff << '/' << size << " bytes in '"
//                 << name << "'...";

        while (offset < size_ && write_byte (offset))
        {
            offset += page_size;
        }

        if (offset > size_ && write_byte (size_ - 1) && fsync (fd_) == 0) {
//            log_info << "Preallocating " << diff << '/' << size
//                     << " bytes in '" << name << "' done.";
            return;
        }

        gu_throw_error (errno) << "File preallocation failed";
    }

    void
    FileDescriptor::prealloc(off_t const start) throw (Exception)
    {
        off_t const diff (size_ - start);

        log_debug << "Preallocating " << diff << '/' << size_ << " bytes in '"
                  << name_ << "'...";

        if (0 != posix_fallocate (fd_, start, diff))
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
