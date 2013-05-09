//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#define _FILE_OFFSET_BITS 64

#include "mapped_buffer.hpp"

#include "gu_throw.hpp"
#include "gu_logger.hpp"

#include "gu_macros.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

#include <limits>

// MAP_FAILED is defined as (void *) -1
#pragma GCC diagnostic ignored "-Wold-style-cast"

using namespace std;
using namespace gu;


galera::MappedBuffer::MappedBuffer(const std::string& working_dir,
                                   size_t threshold)
    :
    working_dir_  (working_dir),
    file_         (),
    fd_           (-1),
    threshold_    (threshold),
    buf_          (0),
    buf_size_     (0),
    real_buf_size_(0)
{

}


galera::MappedBuffer::~MappedBuffer()
{
    if (fd_ != -1)
    {
        struct stat st;
        fstat(fd_, &st);
        log_debug << "file size " << st.st_size;
    }
    clear();
}


void galera::MappedBuffer::reserve(size_t sz)
{
    if (real_buf_size_ >= sz)
    {
        // no need for reallocation
        return;
    }

    if (sz > threshold_)
    {
        // buffer size exceeds in-memory threshold, have to mmap

        if (gu_unlikely(std::numeric_limits<size_t>::max() - sz < threshold_))
        {
            sz = std::numeric_limits<size_t>::max();
        }
        else
        {
            sz = (sz/threshold_ + 1)*threshold_;
        }

        if (gu_unlikely(sz >
                        static_cast<size_t>(std::numeric_limits<off_t>::max())))
        {
            gu_throw_error(EINVAL) << "size exceeds maximum of off_t";
        }

        if (fd_ == -1)
        {
            file_ = working_dir_ + "/gmb_XXXXXX";
            fd_ = mkstemp(&file_[0]);
            if (fd_ == -1)
            {
                gu_throw_error(errno) << "mkstemp(" << file_ << ") failed";
            }
            if (ftruncate(fd_, sz) == -1)
            {
                gu_throw_error(errno) << "ftruncate() failed";
            }
            byte_t* tmp(reinterpret_cast<byte_t*>(
                            mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE,
                                 fd_, 0)));
            if (tmp == MAP_FAILED)
            {
                free(buf_);
                buf_ = 0;
                clear();
                gu_throw_error(ENOMEM) << "mmap() failed";
            }
            copy(buf_, buf_ + buf_size_, tmp);
            free(buf_);
            buf_ = tmp;
        }
        else
        {
            if (munmap(buf_, real_buf_size_) != 0)
            {
                gu_throw_error(errno) << "munmap() failed";
            }
            if (ftruncate(fd_, sz) == -1)
            {
                gu_throw_error(errno) << "fruncate() failed";
            }
            byte_t* tmp(reinterpret_cast<byte_t*>(
                            mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd_, 0)));
            if (tmp == MAP_FAILED)
            {
                buf_ = 0;
                clear();
                gu_throw_error(ENOMEM) << "mmap() failed";
            }
            buf_ = tmp;
        }
    }
    else
    {
        sz = min(threshold_, sz*2);
        byte_t* tmp(reinterpret_cast<byte_t*>(realloc(buf_, sz)));
        if (tmp == 0)
        {
            gu_throw_error(ENOMEM) << "realloc failed";
        }
        buf_ = tmp;
    }
    real_buf_size_ = sz;
}


void galera::MappedBuffer::resize(size_t sz)
{
    reserve(sz);
    buf_size_ = sz;
}


void galera::MappedBuffer::clear()
{
    if (fd_ != -1)
    {
        if (buf_ != 0) munmap(buf_, real_buf_size_);
        while (close(fd_) == EINTR) { }
        unlink(file_.c_str());
    }
    else
    {
        free(buf_);
    }

    fd_            = -1;
    buf_           = 0;
    buf_size_      = 0;
    real_buf_size_ = 0;
}
