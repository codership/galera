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
#include "gu_byteswap.hpp"

#include <string>
#include <ostream>
#include <vector>

namespace gcache
{
    class Page : public MemOps
    {
    public:

        class Nonce
        {
        public:
            Nonce();                             /* constructs random nonce */
            Nonce(const void* buf, size_t size); /* reads nonce from buffer */
            size_t write(void* buf, size_t size)const;/* write nonce to buffer */
            const wsrep_enc_iv_t* iv() const { return &d.iv; }
            const void* ptr() const { return &d; }
            static size_t size()
            {
                return sizeof(Nonce::d);
            }
            Nonce& operator +=(uint64_t i)
            {
                d.l[0] = gu::htog<uint64_t>(gu::gtoh<uint64_t>(d.l[0]) + i);
                return *this;
            }

        private:
            union { wsrep_enc_iv_t iv; uint32_t i[8]; uint64_t l[4]; } d;

            GU_COMPILE_ASSERT(sizeof(d.iv) == sizeof(d.l), size_fail1);
            GU_COMPILE_ASSERT(sizeof(d.i)  == sizeof(d.l), size_fail2);
        };

        typedef std::vector<uint8_t> EncKey;

        Page (void*              ps,
              const std::string& name,
              const EncKey&      key,
              const Nonce&       nonce,
              size_t             size,
              int                dbg);

        ~Page () {}

        void* malloc  (size_type size);

        /* should not be used */
        void* realloc (void* ptr, size_type size);
        /* returns true in case of success */
        bool  realloc (void* ptr, size_type old_size, size_type new_size);

        bool  free    (BufferHeader* bh, const void* ptr)
        {
            if (ptr)
            {
                assert(ptr2BH(ptr) >= mmap_.ptr);
                assert(static_cast<void*>(ptr2BH(ptr)) <=
                       // checks that bh is within page
                       (static_cast<uint8_t*>(mmap_.ptr) + mmap_.size -
                        sizeof(BufferHeader)));
            }

            assert(bh->size >= sizeof(BufferHeader));
            assert(bh->store == BUFFER_IN_PAGE);
            assert(bh->ctx == reinterpret_cast<BH_ctx_t>(this));
            assert(BH_is_released(bh));
            assert(!closed_);
            assert(used_ > 0);
            used_--;
#ifndef NDEBUG
            if (debug_) { log_info << name() << " freed " << bh << ", used: "
                                   << used_ << ", mapped: " << mapped_; }
#endif
            return (bh->seqno_g <= 0);
        }

        void  free    (BufferHeader* bh) { free(bh, NULL); }

        void  repossess(BufferHeader* bh, const void* ptr)
        {
            if (ptr)
            {
                assert(ptr >= mmap_.ptr);
                assert(static_cast<const uint8_t*>(ptr) + aligned_size(bh->size)
                       <= next_);
            }
            assert(bh->size >= sizeof(BufferHeader));
            assert(bh->seqno_g >= 0);
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

        void  repossess(BufferHeader* bh) { assert(0); repossess(bh, NULL); }

        void discard (BufferHeader* bh)
        {
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

        void xcrypt(wsrep_encrypt_cb_t    encrypt_cb,
                    void*                 app_ctx,
                    const void*           from,
                    void*                 to,
                    size_type             size,
                    wsrep_enc_direction_t dir);

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
            if (space_ >= sizeof(BufferHeader)) BH_clear(BH_cast(next_));
        }

        /* Drop filesystem cache on the file */
        void drop_fs_cache() const;

        void* parent() const { return ps_; }

        void print(std::ostream& os) const;

        void set_debug(int const dbg) { debug_ = dbg; }

        static const size_type ALIGNMENT = 16;
        /* typical encryption block size */

        static inline size_type aligned_size(size_type s)
        {
            return GU_ALIGN(s, Page::ALIGNMENT);
        }

        /* amount of space that will be reserved for metadata */
        static size_type meta_size(size_type enc_key_size)
        {
            return Page::aligned_size(sizeof(Nonce)) +
                   Page::aligned_size(enc_key_size);
        }

    private:

        gu::FileDescriptor fd_;
        gu::MMap           mmap_;
        EncKey const       key_;
        Nonce const        nonce_;
        seqno_t            seqno_max_; // highest seqno assigned to buffer
        void* const        ps_;
        uint8_t*           next_;
        size_t             space_;
        size_t             used_;   // allocated - freed buffers
        size_t             mapped_; // buffers mapped in seqno2ptr map
        int                debug_;
        bool               closed_; // page not available any more

        GU_COMPILE_ASSERT(ALIGNMENT % GU_MIN_ALIGNMENT == 0,
                          page_alignment_is_not_multiple_of_min_alignment);

        inline uint8_t*
        start() { return static_cast<uint8_t*>(mmap_.ptr); }

        inline const uint8_t*
        start() const { return static_cast<const uint8_t*>(mmap_.ptr); }

        Page(const gcache::Page&);
        Page& operator=(const gcache::Page&);

    }; /* class Page */

    static inline std::ostream&
    operator <<(std::ostream& os, const gcache::Page& p)
    {
        p.print(os);
        return os;
    }

    static inline gcache::Page::Nonce
    operator +(gcache::Page::Nonce n, uint64_t const i)
    {
        n += i;
        return n;
    }

} /* namespace gcache */

#endif /* _gcache_page_hpp_ */
