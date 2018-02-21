/*
 * Copyright (C) 2009-2016 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gu_fdesc.hpp"

#include "gu_logger.hpp"
#include "gu_throw.hpp"

extern "C" {
#include "gu_limits.h"
}

#if !defined(_XOPEN_SOURCE) && !defined(__APPLE__)
#define _XOPEN_SOURCE 600
#endif

#include <cerrno>
#include <limits>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef O_CLOEXEC // CentOS < 6.0 does not have it
#define O_CLOEXEC 0
#endif

#ifndef O_NOATIME
#define O_NOATIME 0
#endif

namespace gu
{
    static int const OPEN_FLAGS   = O_RDWR | O_NOATIME | O_CLOEXEC;
    static int const CREATE_FLAGS = OPEN_FLAGS | O_CREAT /*| O_TRUNC*/;

    /* respect user umask by allowing all bits by default */
    static mode_t const CREATE_MODE =
        S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH ;

    FileDescriptor::FileDescriptor (const std::string& fname,
                                    bool const         sync)
        : name_(fname),
          fd_  (open (name_.c_str(), OPEN_FLAGS)),
          size_(fd_ < 0 ? 0 : lseek (fd_, 0, SEEK_END)),
          sync_(sync)
    {
        constructor_common();
    }

    static unsigned long long
    available_storage(const std::string& name, size_t size)
    {
        static size_t const reserve(1 << 20); // reserve 1M free space
        struct statvfs stat;
        int const err(statvfs(name.c_str(), &stat));

        if (0 == err)
        {
            unsigned long long  const free_size(stat.f_bavail * stat.f_bsize);

            if (reserve < free_size)
            {
                return free_size - reserve;
            }
            else
            {
                return 0;
            }
        }
        else
        {
            int const errn(errno);
            log_warn << "statvfs() failed on '" << name << "' partition: "
                     << errn << " (" << strerror(errn) <<"). Proceeding anyway.";
            return std::numeric_limits<unsigned long long>::max();
        }
    }

    FileDescriptor::FileDescriptor (const std::string& fname,
                                    size_t const       size,
                                    bool   const       allocate,
                                    bool   const       sync)
        : name_(fname),
          fd_  (open (fname.c_str(), CREATE_FLAGS, CREATE_MODE)),
          size_(size),
          sync_(sync)
    {
        constructor_common();

        off_t const current_size(lseek (fd_, 0, SEEK_END));

        if (current_size < size_)
        {
            unsigned long long const available(available_storage(name_, size_));

            if (size_t(size_) > available)
            {
                ::close(fd_);
                ::unlink(name_.c_str());
                gu_throw_error(ENOSPC) << "Requested size " << size_ << " for '"
                                       << name_
                                       << "' exceeds available storage space "
                                       << available;
            }

            if (allocate)
            {
                // reserve space that hasn't been reserved
                prealloc (current_size);
            }
            else
            {
                // reserve size or bus error follows mmap()
                write_byte (size_ - 1);
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
    FileDescriptor::constructor_common()
    {
        if (fd_ < 0) {
            gu_throw_error(errno) << "Failed to open file '" + name_ + '\'';
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
        log_debug << "Opened file '" << name_ << "', size: " << size_;
        log_debug << "File descriptor: " << fd_;
    }

    FileDescriptor::~FileDescriptor ()
    {
        if (sync_)
        {
            try { sync(); } catch (Exception& e) { log_error << e.what(); }
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
    FileDescriptor::sync () const
    {
        log_debug << "Flushing file '" << name_ << "'";

        if (fsync (fd_) < 0) {
            gu_throw_error(errno) << "fsync() failed on '" + name_ + '\'';
        }

        log_debug << "Flushed file '" << name_ << "'";
    }

    bool
    FileDescriptor::write_byte (off_t offset)
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
    FileDescriptor::write_file (off_t const start)
    {
        // last byte of the start page
        off_t offset = (start / GU_PAGE_SIZE + 1) * GU_PAGE_SIZE - 1;

        log_info << "Preallocating " << (size_ - start) << '/' << size_
                 << " bytes in '" << name_ << "'...";

        while (offset < size_ && write_byte (offset))
        {
            offset += GU_PAGE_SIZE;
        }

        if (offset >= size_ && write_byte (size_ - 1))
        {
            sync();
            return;
        }

        gu_throw_error (errno) << "File preallocation failed";
    }

    void
    FileDescriptor::prealloc(off_t const start)
    {
        off_t const diff (size_ - start);

        log_debug << "Preallocating " << diff << '/' << size_ << " bytes in '"
                  << name_ << "'...";

#if defined(__APPLE__)
        if (-1 == fcntl (fd_, F_SETSIZE, size_) && -1 == ftruncate (fd_, size_))
        {
#else
        int const ret = posix_fallocate (fd_, start, diff);
        if (0 != ret)
        {
            errno = ret;
#endif
            if ((EINVAL == errno || ENOSYS == errno) && start >= 0 && diff > 0)
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
