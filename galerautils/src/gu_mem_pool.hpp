/* Copyright (C) 2013 Codership Oy <info@codership.com> */
/**
 * @file Self-adjusting pool of same size memory buffers.
 *
 * How it works: pool is never allowed to keep more than half of total
 * allocated buffers (plus min_count), so at least half of buffers must be
 * in use. As more than half goes out of use they will be deallocated rather
 * than placed back in the pool.
 *
 * $Id$
 */

#ifndef _GU_MEM_POOL_HPP_
#define _GU_MEM_POOL_HPP_

#include "gu_lock.hpp"
#include "gu_macros.hpp"

#include <assert.h>

#include <vector>
#include <ostream>
//#include <new> // std::bad_alloc

namespace gu
{
    /* There is template specialization for thread_safe = true, so it makes
     * this implicit false specialization. */
    template <bool thread_safe>
    class MemPool
    {
    public:

        MemPool(int buf_size, int min_count, const char* name = "")
            : pool_     (),
              hits_     (0),
              misses_   (0),
              allocd_   (0),
              name_     (name),
              buf_size_ (buf_size),
              min_count_(min_count)
        {
            assert(buf_size_  >  0);
            assert(min_count_ >= 0);
            pool_.reserve(min_count_);
        }

        ~MemPool()
        {
            /* all buffers must be returned to pool before destruction */
            assert(pool_.size() == allocd_);

            for (size_t i(0); i < pool_.size(); ++i)
            {
                assert(pool_[i]);
                free(pool_[i]);
            }
        }

        void* acquire()
        {
            void* ret(from_pool());

            if (!ret) ret = alloc();

            return ret;
        }

        void recycle(void* buf)
        {
            if (!to_pool(buf)) free(buf);
        }

        void print(std::ostream& os) const
        {
            double hr(hits_);

            if (hr > 0)
            {
                assert(misses_ > 0);
                hr /= hits_ + misses_;
            }

            os << "MemPool(" << name_ << "): hit ratio: " << hr
               << ", allocated: " << allocd_ << ", pool count: "
               << pool_.size();
        }

    protected:

        /* from_pool() and to_pool() will need to be called under mutex
         * in thread-safe version, so all object data are modified there.
         * alloc() and free() then can be called outside critical section. */
        void* from_pool()
        {
            void* ret(NULL);

            if (pool_.size() > 0)
            {
                ret = pool_.back();
                assert(ret);
                pool_.pop_back();
                ++hits_;
            }
            else
            {
                ++allocd_;
                ++misses_;
            }

            return ret;
        }

        // returns false if buffer can't be returned to pool
        bool to_pool(void* buf)
        {
            assert(buf);

            bool const ret(allocd_/2 + min_count_ > pool_.size());

            if (ret)
            {
                pool_.push_back(buf);
            }
            else
            {
                assert(allocd_ > 0);
                --allocd_;
            }

            return ret;
        }

        void* alloc()
        {
            return (operator new(buf_size_));
        }

        void free(void* const buf)
        {
            assert(buf);
            operator delete(buf);
        }

        friend class MemPool<true>;

    private:

        std::vector<void*> pool_;
        size_t hits_;
        size_t misses_;
        size_t allocd_;
        const char* const name_;
        int const buf_size_;
        int const min_count_;

        MemPool (const MemPool&);
        MemPool operator= (const MemPool&);

    }; /* class MemPool<false>: thread-unsafe */

    /* thread-unsa */
    template <>
    class MemPool<true>
    {
    public:

        MemPool(int buf_size, int min_count, const char* name = "")
            : base_(buf_size, min_count, name),
              mtx_ ()
        {}

        ~MemPool() {}

        void* acquire()
        {
            void* ret;

            {
                Lock lock(mtx_);
                ret = base_.from_pool();
            }

            if (!ret) ret = base_.alloc();

            return ret;
        }

        void recycle(void* buf)
        {
            bool pooled;

            {
                Lock lock(mtx_);
                pooled = base_.to_pool(buf);
            }

            if (!pooled) base_.free(buf);
        }

        void print(std::ostream& os)
        {
            Lock lock(mtx_);
            base_.print(os);
        }

    private:

        MemPool<false> base_;
        Mutex          mtx_;

    }; /* class MemPool<true>: thread-safe */

} /* namespace gu */

template <bool ts>
std::ostream& operator << (std::ostream& os, const gu::MemPool<ts>& mp)
{
    mp.print(os); return os;
}

#endif /* _GU_MEM_POOL_HPP_ */
