/*
 * Copyright (C) 2010-2024 Codership Oy <info@codership.com>
 */

/*! @file page file class */

#ifndef _gcache_page_hpp_
#define _gcache_page_hpp_

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
            assert (used_ > 0);
#ifndef NDEBUG
            if (debug_) { log_info << name() << " freed " << bh; }
#endif
            if (bh->seqno_g <= 0) // ordered buffers get discarded in discard()
            {
                used_--;
#ifndef NDEBUG
                if (debug_) { log_info << name() << " decremented ref count to "
                                       << used_; }
#endif
                return true;
            }
            return false;
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
#ifndef NDEBUG
            if (debug_) { log_info << name() << " repossessed " << bh; }
#endif
        }

        void  repossess(BufferHeader* bh) { assert(0); repossess(bh, NULL); }

        void discard (BufferHeader* bh)
        {
            assert(BH_is_released(bh));
#ifndef NDEBUG
            if (debug_) { log_info << name() << " discarded " << bh; }
#endif
            assert(bh->seqno_g > 0); /* unordered buffers should be freed() */
            assert(used_ > 0);
            used_--;
#ifndef NDEBUG
            if (debug_)
            {
                log_info << name() << " decremented ref count to " << used_;
            }
#endif
        }

        void xcrypt(wsrep_encrypt_cb_t    encrypt_cb,
                    void*                 app_ctx,
                    const void*           from,
                    void*                 to,
                    size_type             size,
                    wsrep_enc_direction_t dir);

        size_t used () const { return used_; }

        size_t size() const { return fd_.size(); } /* size on storage */

        const std::string& name() const { return fd_.name(); }

        void reset ();

        void  seqno_lock(seqno_t) {}

        void  seqno_unlock() {}

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
        void*              ps_;
        uint8_t*           next_;
        size_t             space_;
        size_t             used_;
        int                debug_;

        GU_COMPILE_ASSERT(ALIGNMENT % GU_MIN_ALIGNMENT == 0,
                          page_alignment_is_not_multiple_of_min_alignment);

        inline uint8_t*
        start() { return static_cast<uint8_t*>(mmap_.ptr); }

        inline const uint8_t*
        start() const { return static_cast<const uint8_t*>(mmap_.ptr); }

        void close(); /* close page for allocation */

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
