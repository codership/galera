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
    typedef std::vector<void*> MemPoolVector;

    /* Since we specialize this template iwth thread_safe=true parameter below,
     * this makes it implicit thread_safe=false specialization. */
    template <size_t                   buf_size,
              MemPoolVector::size_type reserve,
              bool                     thread_safe>
    class MemPool
    {
    public:

        explicit
        MemPool(const char* name = "")
            : pool_     (),
              name_     (name),
              hits_     (0),
              misses_   (0),
              allocd_   (0)
        {
            GU_COMPILE_ASSERT(buf_size  >  0, non_positive_buf_size);
            pool_.reserve(reserve);
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

            os << "MemPool("       << name_
               << "): hit ratio: " << hr
               << ", misses: "     << misses_
               << ", in use: "     << allocd_ - pool_.size()
               << ", in pool: "    << pool_.size();
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

            bool const ret(reserve + allocd_/2 > pool_.size());

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
            return (operator new(buf_size));
        }

        void free(void* const buf)
        {
            assert(buf);
            operator delete(buf);
        }

        friend class MemPool<buf_size, reserve, true>;

    private:

        MemPoolVector pool_;
        const char* const name_;
        size_t hits_;
        size_t misses_;
        size_t allocd_;

        MemPool (const MemPool&);
        MemPool operator= (const MemPool&);

    }; /* class MemPool: thread-unsafe */


    /* Thread-safe MemPool specialization */
    template <size_t                   buf_size,
              MemPoolVector::size_type reserve>
    class MemPool<buf_size, reserve, true>
    {
    public:

        explicit
        MemPool(const char* name = "") : base_(name), mtx_ () {}

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

        void print(std::ostream& os) const
        {
            Lock lock(mtx_);
            base_.print(os);
        }

    private:

        typedef MemPool<buf_size, reserve, false> MemPoolBase;

        MemPoolBase base_;
        Mutex       mtx_;

    }; /* class MemPool: thread-safe */

    template <size_t                   s,
              MemPoolVector::size_type r,
              bool                     b>
    std::ostream& operator << (std::ostream& os,
                               const MemPool<s, r, b>& mp)
    {
        mp.print(os); return os;
    }

} /* namespace gu */


#endif /* _GU_MEM_POOL_HPP_ */
