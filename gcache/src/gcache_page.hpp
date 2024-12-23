/*
 * Copyright (C) 2010-2025 Codership Oy <info@codership.com>
 */

/*! @file page file class */

#ifndef _gcache_page_hpp_
#define _gcache_page_hpp_

#include "gcache_seqno.hpp"
#include "gcache_memops.hpp"
#include "gcache_bh.hpp"

#include "gu_fdesc.hpp"
#include "gu_mmap.hpp"
#include "gu_logger.hpp"

#include <string>
#include <ostream>

namespace gcache
{
    class Page : public MemOps
    {
    public:

        Page (void*              ps,
              const std::string& name,
              size_t             size,
              int                dbg);
        ~Page () {}

        void* malloc  (size_type size);

        void* realloc (void* ptr, size_type size);

        void  free    (BufferHeader* bh)
        {
            assert(bh >= mmap_.ptr);
            assert(static_cast<void*>(bh) <=
                   (static_cast<uint8_t*>(mmap_.ptr) + mmap_.size -
                    sizeof(BufferHeader)));
            assert(bh->size > 0);
            assert(bh->store == BUFFER_IN_PAGE);
            assert(bh->ctx == reinterpret_cast<BH_ctx_t>(this));
            assert(!closed_);
            assert(used_ > 0);
            used_--;
#ifndef NDEBUG
            if (debug_) { log_info << name() << " freed " << bh << ", used: "
                                   << used_ << ", mapped: " << mapped_; }
#endif
        }

        void  repossess(BufferHeader* bh)
        {
            assert(bh >= mmap_.ptr);
            assert(reinterpret_cast<uint8_t*>(bh) + bh->size <= next_);
            assert(bh->size > 0);
            assert(bh->seqno_g != SEQNO_NONE);
            assert(bh->store == BUFFER_IN_PAGE);
            assert(bh->ctx == reinterpret_cast<BH_ctx_t>(this));
            assert(BH_is_released(bh)); // will be marked unreleased by caller
            assert(!closed_); // minimum available seqno must be adjusted
                              // before closing the page
            used_++;
#ifndef NDEBUG
            if (debug_) { log_info << name() << " repossessed " << bh
                                   << ", used: " << used_ << ", mapped: "
                                   << mapped_; }
#endif
        }

        void discard (BufferHeader* bh)
        {
            assert(bh >= mmap_.ptr);
            assert(reinterpret_cast<uint8_t*>(bh) + bh->size <= next_);
            assert(bh->size > 0);
            assert(bh->seqno_g != SEQNO_NONE);
            assert(bh->store == BUFFER_IN_PAGE);
            assert(bh->ctx == reinterpret_cast<BH_ctx_t>(this));
            assert(BH_is_released(bh));
            assert(mapped_ > 0 || bh->seqno_g == SEQNO_ILL);
            mapped_ -= (bh->seqno_g != SEQNO_ILL);
#ifndef NDEBUG
            if (bh->seqno_g != SEQNO_ILL && 0 == mapped_)
                assert(seqno_max_ == bh->seqno_g);
            if (debug_) { log_info << name() << " discarded " << bh
                                   << ", used: " << used_ << ", mapped: "
                                   << mapped_; }
#endif
        }

        size_t used() const { return used_; }

        size_t size() const { return fd_.size(); } /* size on storage */

        const std::string& name() const { return fd_.name(); }

        void reset ();

        void  seqno_lock(seqno_t) {}

        void  seqno_unlock() {}

        void  seqno_assign(seqno_t const seqno)
        {
            assert(seqno > 0);
            assert(used_ > 0); // cannot assign seqno to unused buffer
            assert(!closed_);  // cannot be closed while used
            seqno_max_ = std::max(seqno_max_, seqno);
            mapped_++;
#ifndef NDEBUG
            if (debug_) { log_info << name() << " seqno_assign(" << seqno
                                   << ") seqno_max: " << seqno_max_
                                   << ", used: " << used_ << ", mapped: "
                                   << mapped_; }
#endif
        }

        seqno_t seqno_max() const { return seqno_max_; }

        void close()
        {
            assert(0 == used_);
            closed_ = true;
        }

        /* Drop filesystem cache on the file */
        void drop_fs_cache() const;

        void* parent() const { return ps_; }

        void print(std::ostream& os) const;

        void set_debug(int const dbg) { debug_ = dbg; }

    private:

        gu::FileDescriptor fd_;
        gu::MMap           mmap_;
        seqno_t            seqno_max_; // highest seqno assigned to buffer
        void* const        ps_;
        uint8_t*           next_;
        size_t             space_;
        size_t             used_;   // allocated - freed buffers
        size_t             mapped_; // buffers mapped in seqno2ptr map
        int                debug_;
        bool               closed_; // page not available any more

        Page(const gcache::Page&);
        Page& operator=(const gcache::Page&);
    };

    static inline std::ostream&
    operator <<(std::ostream& os, const gcache::Page& p)
    {
        p.print(os);
        return os;
    }
}

#endif /* _gcache_page_hpp_ */
