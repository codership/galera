//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#include "mapped_buffer.hpp"

#include "gu_throw.hpp"
#include "gu_logger.hpp"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

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
    
    if (sz >= threshold_)
    {
        sz = (sz/threshold_ + 1)*threshold_;
        // buffer size exceeds in-memory threshold, have to mmap
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
                gu_throw_error(errno) << "fruncate() failed";
            }
            byte_t* tmp(reinterpret_cast<byte_t*>(
                            mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE, 
                                 fd_, 0)));
            if (tmp == 0)
            {
                gu_throw_error(ENOMEM) << "mmap() failed";
            }
            copy(buf_, buf_ + buf_size_, tmp);
            free(buf_);
            buf_ = tmp;
        }
        else
        {
            if (ftruncate(fd_, sz) == -1)
            {
                gu_throw_error(errno) << "fruncate() failed";
            }
            byte_t* tmp(reinterpret_cast<byte_t*>(
                            mremap(buf_, real_buf_size_, sz, MREMAP_MAYMOVE)));
            if (tmp == 0)
            {
                gu_throw_error(ENOMEM) << "mremap failed";
            }
            buf_ = tmp;
        }
    }
    else
    {
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
        munmap(buf_, real_buf_size_);
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
