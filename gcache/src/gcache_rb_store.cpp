/*
 * Copyright (C) 2010-2016 Codership Oy <info@codership.com>
 */

#include "gcache_rb_store.hpp"
#include "gcache_page_store.hpp"
#include "gcache_mem_store.hpp"
#include "gcache_limits.hpp"

#include <gu_logger.hpp>
#include <gu_throw.hpp>
#include <gu_progress.hpp>
#include <gu_hexdump.hpp>
#include <gu_hash.h>

#include <cassert>

namespace gcache
{
    static inline size_t check_size (size_t s)
    {
        return s + RingBuffer::pad_size() + sizeof(BufferHeader);
    }

    void
    RingBuffer::reset()
    {
        write_preamble(false);

        first_ = start_;
        next_  = start_;

        BH_clear (BH_cast(next_));

        size_free_ = size_cache_;
        size_used_ = 0;
        size_trail_= 0;

//        mallocs_  = 0;
//        reallocs_ = 0;
    }

    void
    RingBuffer::constructor_common() {}

    RingBuffer::RingBuffer (const std::string& name,
                            size_t             size,
                            seqno2ptr_t&       seqno2ptr,
                            gu::UUID&          gid,
                            bool const         recover)
    :
        fd_        (name, check_size(size)),
        mmap_      (fd_),
        preamble_  (static_cast<char*>(mmap_.ptr)),
        header_    (reinterpret_cast<int64_t*>(preamble_ + PREAMBLE_LEN)),
        start_     (reinterpret_cast<uint8_t*>(header_   + HEADER_LEN)),
        end_       (reinterpret_cast<uint8_t*>(preamble_ + mmap_.size)),
        first_     (start_),
        next_      (first_),
        max_used_  (first_ - static_cast<uint8_t*>(mmap_.ptr) +
                    sizeof(BufferHeader)),
        seqno2ptr_ (seqno2ptr),
        gid_       (gid),
        size_cache_(end_ - start_ - sizeof(BufferHeader)),
        size_free_ (size_cache_),
        size_used_ (0),
        size_trail_(0),
//        mallocs_   (0),
//        reallocs_  (0),
        open_      (true)
    {
        constructor_common ();
        open_preamble(recover);
        BH_clear (BH_cast(next_));
    }

    RingBuffer::~RingBuffer ()
    {
        close_preamble();
        open_ = false;
        mmap_.sync();
    }

    static inline void
    empty_buffer(BufferHeader* const bh) //mark buffer as empty
    {
        bh->seqno_g = gcache::SEQNO_ILL;
    }

    bool
    buffer_is_empty(const BufferHeader* const bh)
    {
        return (SEQNO_ILL == bh->seqno_g);
    }

    /* discard all seqnos preceeding and including seqno */
    bool
    RingBuffer::discard_seqnos(seqno2ptr_t::iterator const i_begin,
                               seqno2ptr_t::iterator const i_end)
    {
        for (seqno2ptr_t::iterator i(i_begin); i != i_end;)
        {
            seqno2ptr_t::iterator j(i); ++i;
            BufferHeader* const bh (ptr2BH (j->second));

            if (gu_likely (BH_is_released(bh)))
            {
                seqno2ptr_.erase (j);
                empty_buffer(bh);

                switch (bh->store)
                {
                case BUFFER_IN_RB:  discard(bh); break;
                case BUFFER_IN_MEM:
                {
                    MemStore* const ms(static_cast<MemStore*>(bh->ctx));
                    ms->discard(bh);
                    break;
                }
                case BUFFER_IN_PAGE:
                {
                    Page*      const page (static_cast<Page*>(bh->ctx));
                    PageStore* const ps   (PageStore::page_store(page));
                    ps->discard(bh);
                    break;
                }
                default:
                    log_fatal << "Corrupt buffer header: " << bh;
                    abort();
                }
            }
            else
            {
                return false;
            }
        }

        return true;
    }

    // returns pointer to buffer data area or 0 if no space found
    BufferHeader*
    RingBuffer::get_new_buffer (size_type const size)
    {
        assert_size_free();

        BH_assert_clear(BH_cast(next_));

        uint8_t* ret(next_);

        size_type const size_next(size + sizeof(BufferHeader));

        Limits::assert_size(size_next);

        if (ret >= first_) {
            assert (0 == size_trail_);
            // try to find space at the end
            size_t const end_size(end_ - ret);

            if (end_size >= size_next) {
                assert(size_free_ >= size);
                goto found_space;
            }
            else {
                // no space at the end, go from the start
                size_trail_ = end_size;
                ret = start_;
            }
        }

        assert (ret <= first_);

        /* Compare with difference to avoid integer overflow: */
        if (static_cast<size_t>(first_ - ret) >= size_next)
        {
            assert(size_free_ >= size);
        }

        while (static_cast<size_t>(first_ - ret) < size_next)
        {
            // try to discard first buffer to get more space
            BufferHeader* bh = BH_cast(first_);

            if (!BH_is_released(bh) /* true also when first_ == next_ */ ||
                (bh->seqno_g > 0 && !discard_seqno (bh->seqno_g)))
            {
                // can't free any more space, so no buffer, next_ is unchanged
                // and revert size_trail_ if it was set above
                if (next_ >= first_) size_trail_ = 0;
                assert_sizes();
                return 0;
            }

            assert (first_ != next_);
            /* buffer is either discarded already, or it must have seqno */
            assert (SEQNO_ILL == bh->seqno_g);

            first_ += bh->size;
            assert_size_free();

            if (gu_unlikely(0 == (BH_cast(first_))->size))
            {
                // empty header: check if we fit at the end and roll over if not
                assert(first_ >= next_);
                assert(first_ >= ret);

                first_ = start_;
                assert_size_free();

                /* Compare with difference to avoid integer overflow: */
                if (static_cast<size_t>(end_ - ret) >= size_next)
                {
                    assert(size_free_ >= size);
                    size_trail_ = 0;
                    goto found_space;
                }
                else
                {
                    size_trail_ = end_ - ret;
                    ret = start_;
                }
            }

            assert(ret <= first_);
        }

        assert (ret <= first_);

#ifndef NDEBUG
        /* Compare with difference to avoid integer overflow: */
        if (static_cast<size_t>(first_ - ret) < size_next)
        {
            log_fatal << "Assertion ((first - ret) >= size_next) failed: "
                      << std::endl
                      << "first offt = " << (first_ - start_) << std::endl
                      << "next  offt = " << (next_  - start_) << std::endl
                      << "end   offt = " << (end_   - start_) << std::endl
                      << "ret   offt = " << (ret    - start_) << std::endl
                      << "size_next  = " << size_next         << std::endl;
            abort();
        }
#endif

    found_space:
        size_used_ += size;
        assert (size_used_ <= size_cache_);
        assert (size_free_ >= size);
        size_free_ -= size;

        BufferHeader* const bh(BH_cast(ret));
        bh->size    = size;
        bh->seqno_g = SEQNO_NONE;
        bh->seqno_d = SEQNO_ILL;
        bh->flags   = 0;
        bh->store   = BUFFER_IN_RB;
        bh->ctx     = this;

        next_ = ret + size;

        size_t max_used=
            next_ - static_cast<uint8_t*>(mmap_.ptr) + sizeof(BufferHeader);

        if (max_used > max_used_)
        {
            max_used_ = max_used;
        }

        assert (next_ + sizeof(BufferHeader) <= end_);
        BH_clear (BH_cast(next_));
        assert_sizes();

        return bh;
    }

    void*
    RingBuffer::malloc (size_type const size)
    {
        Limits::assert_size(size);

        void* ret(NULL);

        // We can reliably allocate continuous buffer which is 1/2
        // of a total cache space. So compare to half the space
        if (size <= (size_cache_ / 2) && size <= (size_cache_ - size_used_))
        {
            BufferHeader* const bh (get_new_buffer (size));

            BH_assert_clear(BH_cast(next_));
//            mallocs_++;

            if (gu_likely (0 != bh)) ret = bh + 1;
        }

        assert_sizes();

        return ret; // "out of memory"
    }

    void
    RingBuffer::free (BufferHeader* const bh)
    {
        assert(BH_is_released(bh));

        assert(size_used_ >= bh->size);
        size_used_ -= bh->size;

        if (SEQNO_NONE == bh->seqno_g)
        {
            empty_buffer(bh);
            discard (bh);
        }
    }

    void*
    RingBuffer::realloc (void* ptr, size_type const size)
    {
        Limits::assert_size(size);

        assert_sizes();
        assert (NULL != ptr);
        assert (size > 0);
        // We can reliably allocate continuous buffer which is twice as small
        // as total cache area. So compare to half the space
        if (size > (size_cache_ / 2)) return 0;

        BufferHeader* const bh(ptr2BH(ptr));

//        reallocs_++;

        // first check if we can grow this buffer by allocating
        // adjacent buffer
        {
            Limits::assert_size(bh->size);
            diff_type const adj_size(size - bh->size);
            if (adj_size <= 0) return ptr;

            uint8_t* const adj_ptr(reinterpret_cast<uint8_t*>(BH_next(bh)));
            if (adj_ptr == next_)
            {
                ssize_type const size_trail_saved(size_trail_);
                void* const adj_buf (get_new_buffer (adj_size));

                BH_assert_clear(BH_cast(next_));

                if (adj_ptr == adj_buf)
                {
                    bh->size = next_ - static_cast<uint8_t*>(ptr) +
                        sizeof(BufferHeader);
                    return ptr;
                }
                else // adjacent buffer allocation failed, return it back
                {
                    next_ = adj_ptr;
                    BH_clear (BH_cast(next_));
                    size_used_ -= adj_size;
                    size_free_ += adj_size;
                    if (next_ < first_) size_trail_ = size_trail_saved;
                }
            }
        }

        BH_assert_clear(BH_cast(next_));
        assert_sizes();

        // find non-adjacent buffer
        void* ptr_new = malloc (size);
        if (ptr_new != 0) {
            memcpy (ptr_new, ptr, bh->size - sizeof(BufferHeader));
            free (bh);
        }

        BH_assert_clear(BH_cast(next_));
        assert_sizes();

        return ptr_new;
    }

    void
    RingBuffer::estimate_space()
    {
        /* Estimate how much space remains */
        if (first_ < next_)
        {
            /* start_  first_      next_    end_
             *   |       |###########|       |
             */
            size_used_ = next_ - first_;
            size_free_ = size_cache_ - size_used_;
            size_trail_ = 0;
        }
        else
        {
            /* start_  next_       first_   end_
             *   |#######|           |#####| |
             *                              ^size_trail_ */
            assert(size_trail_ > 0);
            size_free_ = first_ - next_ + size_trail_ - sizeof(BufferHeader);
            size_used_ = size_cache_ - size_free_;
        }

        assert_sizes();
        assert(size_free_ < size_cache_);
    }

    void
    RingBuffer::seqno_reset()
    {
        write_preamble(false);

        if (size_cache_ == size_free_) return;

        /* Find the last seqno'd RB buffer. It is likely to be close to the
         * end of released buffers chain. */
        BufferHeader* bh(0);

        for (seqno2ptr_t::reverse_iterator r(seqno2ptr_.rbegin());
             r != seqno2ptr_.rend(); ++r)
        {
            BufferHeader* const b(ptr2BH(r->second));
            if (BUFFER_IN_RB == b->store)
            {
#ifndef NDEBUG
                if (!BH_is_released(b))
                {
                    log_fatal << "Buffer "
                              << reinterpret_cast<const void*>(r->second)
                              << ", seqno_g " << b->seqno_g << ", seqno_d "
                              << b->seqno_d << " is not released.";
                    assert(0);
                }
#endif
                bh = b;
                break;
            }
        }

        if (!bh) return; /* no seqno'd buffers in RB */

        assert(bh->size > 0);
        assert(BH_is_released(bh));

        /* Seek the first unreleased buffer.
         * This should be called in isolation, when all seqno'd buffers are
         * freed, and the only unreleased buffers should come only from new
         * configuration. There should be no seqno'd buffers after it. */

        size_t const old(size_free_);

        assert (0 == size_trail_ || first_ > next_);
        first_ = reinterpret_cast<uint8_t*>(bh);

        while (BH_is_released(bh)) // next_ is never released - no endless loop
        {
             first_ = reinterpret_cast<uint8_t*>(BH_next(bh));

             if (gu_unlikely (0 == bh->size && first_ != next_))
             {
                 // rollover
                 assert (first_ > next_);
                 first_ = start_;
             }

             bh = BH_cast(first_);
        }

        BH_assert_clear(BH_cast(next_));

        if (first_ == next_)
        {
            log_info << "GCache DEBUG: RingBuffer::seqno_reset(): full reset";
            /* empty RB, reset it completely */
            reset();
            return;
        }

        assert ((BH_cast(first_))->size > 0);
        assert (first_ != next_);
        assert ((BH_cast(first_))->seqno_g == SEQNO_NONE);
        assert (!BH_is_released(BH_cast(first_)));

        estimate_space();

        log_info << "GCache DEBUG: RingBuffer::seqno_reset(): discarded "
                 << (size_free_ - old) << " bytes";

        /* There is a small but non-0 probability that some released buffers
         * are locked within yet unreleased aborted local actions.
         * Seek all the way to next_, invalidate seqnos and update size_free_ */

        assert(first_ != next_);
        assert(bh == BH_cast(first_));

        long total(1);
        long locked(0);

        bh = BH_next(bh);

        while (bh != BH_cast(next_))
        {
            if (gu_likely (bh->size > 0))
            {
                total++;

                if (bh->seqno_g != SEQNO_NONE)
                {
                    // either released or already discarded buffer
                    assert (BH_is_released(bh));
                    empty_buffer(bh);
                    discard (bh);
                    locked++;
                }
                else
                {
                    assert(!BH_is_released(bh));
                }

                bh = BH_next(bh);
            }
            else // rollover
            {
                assert (BH_cast(next_) < bh);
                bh = BH_cast(start_);
            }
        }

        log_info << "GCache DEBUG: RingBuffer::seqno_reset(): found "
                 << locked << '/' << total << " locked buffers";

        assert_sizes();

        if (next_ > first_ && first_ > start_) BH_clear(BH_cast(start_));
        /* this is needed to avoid rescanning from start_ on recovery */
    }

    size_t RingBuffer::allocated_pool_size ()
    {
       return max_used_;
    }

    void
    RingBuffer::print (std::ostream& os) const
    {
        os  << "\nstart_ : " << reinterpret_cast<void*>(start_)
            << "\nend_   : " << reinterpret_cast<void*>(end_)
            << "\nfirst  : " << first_ - start_
            << "\nnext   : " << next_  - start_
            << "\nsize   : " << size_cache_
            << "\nfree   : " << size_free_
            << "\nused   : " << size_used_;
    }

    std::string const RingBuffer::PR_KEY_VERSION   = "Version:";
    std::string const RingBuffer::PR_KEY_GID       = "GID:";
    std::string const RingBuffer::PR_KEY_SEQNO_MAX = "seqno_max:";
    std::string const RingBuffer::PR_KEY_SEQNO_MIN = "seqno_min:";
    std::string const RingBuffer::PR_KEY_OFFSET    = "offset:";
    std::string const RingBuffer::PR_KEY_SYNCED    = "synced:";

    void
    RingBuffer::write_preamble(bool const synced)
    {
        static int const VERSION(1);
        uint8_t* const preamble(reinterpret_cast<uint8_t*>(preamble_));

        std::ostringstream os;

        os << PR_KEY_VERSION << ' ' << VERSION << '\n';
        os << PR_KEY_GID << ' ' << gid_ << '\n';

        if (synced)
        {
            if (!seqno2ptr_.empty())
            {
                os << PR_KEY_SEQNO_MIN << ' '
                   << seqno2ptr_.begin()->first << '\n';

                os << PR_KEY_SEQNO_MAX << ' '
                   << seqno2ptr_.rbegin()->first << '\n';

                os << PR_KEY_OFFSET << ' ' << first_ - preamble << '\n';
            }
            else
            {
                os << PR_KEY_SEQNO_MIN << ' ' << SEQNO_ILL << '\n';
                os << PR_KEY_SEQNO_MAX << ' ' << SEQNO_ILL << '\n';
            }
        }

        os << PR_KEY_SYNCED << ' ' << synced << '\n';
        os << '\n';

        ::memset(preamble_, '\0', PREAMBLE_LEN);

        size_t copy_len(os.str().length());
        if (copy_len >= PREAMBLE_LEN) copy_len = PREAMBLE_LEN - 1;

        ::memcpy(preamble_, os.str().c_str(), copy_len);

        mmap_.sync(preamble_, copy_len);
    }

    void
    RingBuffer::open_preamble(bool const do_recover)
    {
        uint8_t* const preamble(reinterpret_cast<uint8_t*>(preamble_));
        int version(0);
        long long seqno_max(SEQNO_ILL);
        long long seqno_min(SEQNO_ILL);
        off_t offset(-1);
        bool  synced(false);

        {
            std::istringstream iss(preamble_);

            if (iss.fail())
                gu_throw_error(EINVAL) << "Failed to open preamble.";

            std::string line;
            while (getline(iss, line), iss.good())
            {
                std::istringstream istr(line);
                std::string key;

                istr >> key;

                if ('#' == key[0]) { /* comment line */ }
                else if (PR_KEY_VERSION   == key) istr >> version;
                else if (PR_KEY_GID       == key) istr >> gid_;
                else if (PR_KEY_SEQNO_MAX == key) istr >> seqno_max;
                else if (PR_KEY_SEQNO_MIN == key) istr >> seqno_min;
                else if (PR_KEY_OFFSET    == key) istr >> offset;
                else if (PR_KEY_SYNCED    == key) istr >> synced;
            }
        }

        if (version < 0 || version > 16)
        {
           log_warn << "Bogus version in GCache ring buffer preamble: "
                    << version << ". Assuming 0.";
           version = 0;
        }

        if (offset < -1 || (preamble + offset + sizeof(BufferHeader)) > end_)
        {
           log_warn << "Bogus offset in GCache ring buffer preamble: "
                    << offset << ". Assuming unknown.";
           offset = -1;
        }

        if (do_recover)
        {
            if (gid_ != gu::UUID())
            {
                log_info << "Recovering GCache ring buffer: version: " << version
                         << ", UUID: " << gid_ << ", offset: " << offset;

                try
                {
                    recover(offset - (start_ - preamble));
                }
                catch (gu::Exception& e)
                {
                    log_warn << "Failed to recover GCache ring buffer: "
                             << e.what();
                    reset();
                }
            }
            else
            {
                log_info << "Skipped GCache ring buffer recovery: could not "
                    "determine history UUID.";
            }
        }

        write_preamble(false);
    }

    void
    RingBuffer::close_preamble()
    {
        write_preamble(true);
    }

    int64_t
    RingBuffer::scan(off_t const offset)
    {
        int segment_scans(0);
        int64_t seqno_max(-1);
        uint8_t* ptr;
        BufferHeader* bh;
        size_t collision_count(0);
        int64_t erase_up_to(-1);
        uint8_t* segment_start(start_);
        uint8_t* segment_end(end_ - sizeof(BufferHeader));

        /* start at offset (first segment) if we know it and it is valid */
        if (offset >= 0)
        {
            if (start_ + offset + sizeof(BufferHeader) < segment_end)
                /* we know exaclty where the first segment starts */
                segment_start = start_ + offset;
            else
                /* first segment is completely missing, advance scan count */
                segment_scans = 1;
        }

        gu::Progress<ptrdiff_t> progress("GCache::RingBuffer initial scan",
                                         " bytes", end_ - start_, 1<<22 /*4Mb*/);

        while (segment_scans < 2)
        {
            segment_scans++;

            ptr = segment_start;
            bh = BH_cast(ptr);

#define GCACHE_SCAN_BUFFER_TEST                                 \
            (BH_test(bh) && bh->size > 0 &&                     \
             ptr + bh->size <= segment_end &&                   \
             BH_test(BH_cast(ptr + bh->size)))

            while (GCACHE_SCAN_BUFFER_TEST)
            {
                bh->flags |= BUFFER_RELEASED;
                bh->ctx    = this;

                int64_t const seqno_g(bh->seqno_g);

                if (gu_likely(seqno_g > 0))
                {
                    const std::pair<seqno2ptr_iter_t, bool>& res(
                        seqno_g > seqno_max ? seqno_max = seqno_g,
                        std::pair<seqno2ptr_iter_t, bool>(
                            seqno2ptr_.insert(seqno2ptr_.end(),
                                              seqno2ptr_pair_t(seqno_g, bh + 1)),
                            true) :
                        seqno2ptr_.insert(seqno2ptr_pair_t(seqno_g, bh + 1))
                        );

                    if (gu_unlikely (false == res.second))
                    {
                        collision_count++;

                        /* compare two buffers */
                        const void* const old_ptr(res.first->second);
                        BufferHeader* const old_bh
                            (old_ptr ? ptr2BH(old_ptr) : NULL);

                        bool const same_meta(NULL != old_bh &&
                            bh->seqno_g == old_bh->seqno_g  &&
                            bh->size    == old_bh->size     &&
                            bh->flags   == old_bh->flags);

                        const void* const new_ptr(static_cast<void*>(bh+1));

                        uint8_t cs_old[16] = { 0, };
                        uint8_t cs_new[16] = { 0, };
                        if (same_meta)
                        {
                            gu_fast_hash128(old_ptr,
                                            old_bh->size - sizeof(BufferHeader),
                                            cs_old);
                            gu_fast_hash128(new_ptr,
                                            bh->size - sizeof(BufferHeader),
                                            cs_new);
                        }

                        bool const same_data(same_meta &&
                                             !::memcmp(cs_old, cs_new,
                                                       sizeof(cs_old)));
                        std::ostringstream msg;

                        msg << "Attempt to reuse the same seqno: " << seqno_g
                            << ". New ptr = " << new_ptr << ", " << bh
                            << ", cs: " << gu::Hexdump(cs_new, sizeof(cs_new))
                            << ", previous ptr = " << res.first->second;

                        empty_buffer(bh); // this buffer is unusable
                        assert(BH_is_released(bh));

                        if (old_bh != NULL)
                        {
                            msg << ", " << old_bh << ", cs: "
                                << gu::Hexdump(cs_old,sizeof(cs_old));

                            if (!same_data) // no way to choose which is correct
                            {
                                empty_buffer(old_bh);
                                assert(BH_is_released(old_bh));
                                res.first->second = NULL;
                                /* mark entry as invalid, but don't remove from
                                 * the map to block this seqno */

                                if (erase_up_to < seqno_g) erase_up_to = seqno_g;
                            }
                        }

                        log_info << msg.str();

                        if (same_data) {
                            log_info << "Contents are the same, discarding "
                                     << new_ptr;
                        } else {
                            assert(NULL == res.first->second);
                            log_info << "Contents differ. Discarding both.";
                        }
                    }
                }

                progress.update(bh->size);
                ptr += bh->size;
                bh = BH_cast(ptr);
            }

            if (!BH_is_clear(bh))
            {
                if (start_ == segment_start && ptr != first_)
                {
                    log_warn << "Failed to scan the last segment to the end. "
                            "Last events may be missing. Last recovered event: "
                             << gid_ << ':' << seqno_max;
                }

                /* end of file, do best effort */
                if (end_ - sizeof(BufferHeader) == segment_end) BH_clear(bh);
            }

            if (offset > 0 && segment_start == start_ + offset)
            {
                /* started with the first segment, jump to the second one */
                assert(1 == segment_scans);
                first_ = segment_start;
                size_trail_ = end_ - ptr;
                segment_end = segment_start;
                segment_start = start_;
            }
            else if (offset < 0 && segment_start == start_)
            {
                /* started with the second segment, try to find the first one */
                assert(1 == segment_scans);
                next_ = ptr;

                while (!GCACHE_SCAN_BUFFER_TEST &&
                       ptr + sizeof(BufferHeader) < end_)
                {
                    progress.update(1);
                    ptr += 1;
                    bh = BH_cast(ptr);
                    /* it would have been nice to use alignment of 8 or so */
                }

                if (GCACHE_SCAN_BUFFER_TEST)
                {
                    segment_start = ptr;
                    first_ = segment_start;
                }
                else if (ptr + sizeof(BufferHeader) == end_)
                {
                    /* perhaps it was a single segment starting at start_ */
                    first_ = start_;
                    break;
                }
                else
                {
                    assert(0);
                }
            }
            else if (offset == 0 && segment_start == start_)
            {
                /* single segment case */
                assert(1 == segment_scans);
                first_ = segment_start;
                next_ = ptr;
                break;
            }
            else
            {
                assert(2 == segment_scans);
                assert(offset != 0);

                if (offset >= 0) next_ = ptr; /* end of the second segment */

                assert(first_ >= start_ && first_ < end_);
                assert(next_  >= start_ && next_  < end_);

                if (offset < 0 && segment_start > start_)
                {
                    /* first (end) segment was scanned last, estimate trail */
                    size_trail_ = end_ - ptr;
                }
                else if (offset > 0 && next_ > first_)
                {
                    size_trail_ = 0;
                }
            }
#undef GCACHE_SCAN_BUFFER_TEST
        } // while (segment_scans < 2)

        progress.finish();

        return erase_up_to;
    }

    void
    RingBuffer::recover(off_t const offset)
    {
        static const char* const diag_prefix = "Recovering GCache ring buffer: ";

        /* scan the buffer and populate seqno2ptr map */
        int64_t const lower(scan(offset));

        if (!seqno2ptr_.empty())
        {
            assert(next_ <= first_ || size_trail_ == 0);
            assert(next_ >  first_ || size_trail_ >  0);

            /* find the last gapless seqno sequence */
            seqno2ptr_t::reverse_iterator r(seqno2ptr_.rbegin());
            seqno_t const seqno_max(r->first);
            seqno_t       seqno_min(seqno2ptr_.begin()->first);

            if (lower > 0
                /* collisions detected */ ||
                seqno2ptr_t::size_type(seqno_max - seqno_min + 1) >
                seqno2ptr_.size()
                /* not all seqnos present */)
            {
                /* need to search for seqno gaps */
                assert(seqno_max >= lower);
                if (lower == seqno_max)
                {
                    seqno2ptr_.clear();
                    goto full_reset;
                }

                seqno_min = seqno_max;
                ++r;
                for (; r != seqno2ptr_.rend() && r->first > lower; ++r)
                {
                    assert(r->second != NULL);

                    if (r->first + 1 == seqno_min)
                        seqno_min = r->first;
                    else
                        break;
                }
            }
            else
            {
                r = seqno2ptr_.rend();
            }

            log_info << diag_prefix << "found gapless sequence " << seqno_min
                     << '-' << seqno_max;

            if (r != seqno2ptr_.rend())
            {
                log_info << diag_prefix << "discarding seqnos "
                         << seqno2ptr_.begin()->first << '-' << r->first;

                /* clear up seqno2ptr map */
                for (; r != seqno2ptr_.rend(); ++r)
                {
                    if (r->second) empty_buffer(ptr2BH(r->second));
                }
                seqno2ptr_.erase(seqno2ptr_.begin(), seqno2ptr_.find(seqno_min));
            }
            assert(seqno2ptr_.size() > 0);

            /* trim first_: start with the current first_ and scan forward to
             * the first non-empty buffer. */
            BufferHeader* bh(BH_cast(first_));
            assert(bh->size > sizeof(BufferHeader));
            while (bh->seqno_g == SEQNO_ILL)
            {
                assert(bh->size > sizeof(BufferHeader));

                bh = BH_next(bh);

                if (gu_unlikely(0 == bh->size)) bh = BH_cast(start_); // rollover
            }
            first_ = reinterpret_cast<uint8_t*>(bh);

            /* trim next_: start with the last seqno and scan forward up to the
             * current next_. Update to the end of the last non-empty buffer. */
            bh = ptr2BH(seqno2ptr_[seqno_max]);
            BufferHeader* last_bh(bh);
            while (bh != BH_cast(next_))
            {
                if (gu_likely(bh->size) > 0)
                {
                    assert(bh->seqno_d >= SEQNO_ILL);
                    assert(bh->size > sizeof(BufferHeader));

                    if (bh->seqno_g > 0) last_bh = bh;

                    bh = BH_next(bh);
                }
                else
                {
                    bh = BH_cast(start_); // rollover
                }
            }
            next_ = reinterpret_cast<uint8_t*>(BH_next(last_bh));

            /* at this point we must have at least one seqno'd buffer */
            assert(next_ != first_);

            /* as a result of trimming, trailing space may be gone */
            if (first_ < next_) size_trail_ = 0;
            else assert(size_trail_ >= sizeof(BufferHeader));

            estimate_space();

            /* now discard all the locked-in buffers (see seqno_reset()) */
            gu::Progress<size_t> progress(
                "GCache::RingBuffer unused buffers scan",
                " bytes", size_used_, 1<<22 /* 4Mb */);

            size_t total(0);
            size_t locked(0);
            bh = BH_cast(first_);
            while (bh != BH_cast(next_))
            {
                if (gu_likely(bh->size > 0))
                {
                    total++;
                    if (gu_unlikely(SEQNO_ILL == bh->seqno_g))
                    {
                        locked++;
                        discard(bh);
                    }

                    bh = BH_next(bh);
                }
                else
                {
                     bh = BH_cast(start_); // rollover
                }

                progress.update(bh->size);
            }

            progress.finish();

            log_info << "GCache DEBUG: RingBuffer::recover(): found "
                     << locked << '/' << total << " locked buffers";
            log_info << "GCache DEBUG: RingBuffer::recover(): used space: "
                     << size_used_ << '/' << size_cache_;

            assert_sizes();
        }
        else
        {
        full_reset:
            log_info << diag_prefix << "didn't recover any events.";
            reset();
        }
    }

} /* namespace gcache */
