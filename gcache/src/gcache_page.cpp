/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

/*! @file page file class implementation */

#include "gcache_page.hpp"

static size_t
check_size (ssize_t size)
{
    if (size < 0)
        gu_throw_error(EINVAL) << "Negative page size: " << size;

    return (size + sizeof(gcache::BufferHeader));
}

gcache::Page::Page (const std::string& name, ssize_t size) throw (gu::Exception)
    :
    fd_   (name, check_size(size), false, false),
    mmap_ (fd_),
    next_ (static_cast<uint8_t*>(mmap_.ptr)),
    size_ (mmap_.size),
    count_(0)
{
    log_debug << "Created a page of size " << size_ << " bytes";
    BH_clear (reinterpret_cast<BufferHeader*>(next_));
}

void*
gcache::Page::malloc (ssize_t size) throw ()
{
    ssize_t const buf_size = size + sizeof(BufferHeader);

    if (buf_size <= size_)
    {
        BufferHeader* bh(BH_cast(next_));

        bh->size  = buf_size;
        bh->store = BUFFER_IN_PAGE;
        bh->ctx   = this;

        size_ -= buf_size;
        next_ += buf_size;

        if (size_ >= static_cast<ssize_t>(sizeof(BufferHeader)))
        {
            BH_clear (BH_cast(next_));

            assert (reinterpret_cast<uint8_t*>(bh + 1) < next_);
        }

        count_++;

        assert (next_ <= static_cast<uint8_t*>(mmap_.ptr) + mmap_.size);

        return (bh + 1);
    }
    else
    {
        log_debug << "Failed to allocate " << buf_size << " bytes, space left: "
                  << size_ << " bytes, total allocated: "
                  << next_ - static_cast<uint8_t*>(mmap_.ptr);
        return 0;
    }
}

void*
gcache::Page::realloc (void* ptr, ssize_t size) throw ()
{
    BufferHeader* bh(ptr2BH(ptr));

    ssize_t old_size = bh->size - sizeof(BufferHeader);

    if (bh == BH_cast(next_ - bh->size))
    { // last buffer, can both shrink and expand
        ssize_t diff_size = size - old_size;

        if (gu_likely (diff_size < size_))
        {
            bh->size += diff_size;
            size_    -= diff_size;
            next_    += diff_size;
            BH_clear (BH_cast(next_));

            return ptr;
        }
        else return 0; // not enough space in this page
    }
    else
    {
        if (gu_likely(size > old_size))
        {
            void* ret = malloc (size);

            if (ret)
            {
                memcpy (ret, ptr, old_size);
                count_--;
            }

            return ret;
        }
        else
        {
            // do nothing, we can't shrink the buffer, it is locked
            return ptr;
        }
    }
}
