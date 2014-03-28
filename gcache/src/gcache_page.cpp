/*
 * Copyright (C) 2010-2014 Codership Oy <info@codership.com>
 */

/*! @file page file class implementation */

#include "gcache_page.hpp"

// for posix_fadvise()
#if !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 600
#endif
#include <fcntl.h>

static ssize_t
check_size (ssize_t size)
{
    if (size < 0)
        gu_throw_error(EINVAL) << "Negative page size: " << size;

    return size;
}

void
gcache::Page::reset ()
{
    if (gu_unlikely (used_ > 0))
    {
        log_fatal << "Attempt to reset a page '" << name()
                  << "' used by " << used_ << " buffers. Aborting.";
        abort();
    }

    space_ = mmap_.size;
    next_  = static_cast<uint8_t*>(mmap_.ptr);
}

void
gcache::Page::drop_fs_cache() const
{
    mmap_.dont_need();

#if !defined(__APPLE__)
    int const err (posix_fadvise (fd_.get(), 0, fd_.size(),
                                  POSIX_FADV_DONTNEED));
    if (err != 0)
    {
        log_warn << "Failed to set POSIX_FADV_DONTNEED on " << fd_.name()
                 << ": " << err << " (" << strerror(err) << ")";
    }
#endif
}

gcache::Page::Page (void* ps, const std::string& name, ssize_t size)
    :
    fd_   (name, check_size(size), false, false),
    mmap_ (fd_),
    ps_   (ps),
    next_ (static_cast<uint8_t*>(mmap_.ptr)),
    space_(mmap_.size),
    used_ (0)
{
    log_info << "Created page " << name << " of size " << space_
             << " bytes";
    BH_clear (reinterpret_cast<BufferHeader*>(next_));
}

void*
gcache::Page::malloc (ssize_t size)
{
    if (size <= space_)
    {
        BufferHeader* bh(BH_cast(next_));

        bh->size    = size;
        bh->seqno_g = SEQNO_NONE;
        bh->seqno_d = SEQNO_ILL;
        bh->ctx     = this;
        bh->flags   = 0;
        bh->store   = BUFFER_IN_PAGE;

        space_ -= size;
        next_  += size;
        used_++;

#ifndef NDEBUG
        if (space_ >= static_cast<ssize_t>(sizeof(BufferHeader)))
        {
            BH_clear (BH_cast(next_));
            assert (reinterpret_cast<uint8_t*>(bh + 1) < next_);
        }

        assert (next_ <= static_cast<uint8_t*>(mmap_.ptr) + mmap_.size);
#endif
        return (bh + 1);
    }
    else
    {
        log_debug << "Failed to allocate " << size << " bytes, space left: "
                  << space_ << " bytes, total allocated: "
                  << next_ - static_cast<uint8_t*>(mmap_.ptr);
        return 0;
    }
}

void*
gcache::Page::realloc (void* ptr, ssize_t size)
{
    BufferHeader* bh(ptr2BH(ptr));

    if (bh == BH_cast(next_ - bh->size)) // last buffer, can shrink and expand
    {
        ssize_t const diff_size (size - bh->size);

        if (gu_likely (diff_size < space_))
        {
            bh->size += diff_size;
            space_   -= diff_size;
            next_    += diff_size;
            BH_clear (BH_cast(next_));

            return ptr;
        }
        else return 0; // not enough space in this page
    }
    else
    {
        if (gu_likely(size > bh->size))
        {
            void* const ret (malloc (size));

            if (ret)
            {
                memcpy (ret, ptr, bh->size - sizeof(BufferHeader));
                used_--;
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
