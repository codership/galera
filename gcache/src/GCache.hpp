/*
 * Copyright (C) 2009-2016 Codership Oy <info@codership.com>
 */

#ifndef __GCACHE_H__
#define __GCACHE_H__

#include "gcache_mem_store.hpp"
#include "gcache_rb_store.hpp"
#include "gcache_page_store.hpp"
#include "gcache_types.hpp"

#include <gu_types.hpp>
#include <gu_lock.hpp> // for gu::Mutex and gu::Cond
#include <gu_config.hpp>

#include <string>
#include <iostream>
#ifndef NDEBUG
#include <set>
#endif
#include <stdint.h>

namespace gcache
{
    class GCache
    {
    public:

        static void register_params(gu::Config& cfg)
        {
            Params::register_params(cfg);
        }

        /*!
         * Creates a new gcache file in "gcache.name" conf parameter or
         * in data_dir. If file already exists, it gets overwritten.
         */
        GCache (gu::Config& cfg, const std::string& data_dir);

        virtual ~GCache();

        /*! prints object properties */
        void  print (std::ostream& os);

        /* Resets storage */
        void  reset();

        /* Memory allocation functions */
        typedef MemOps::ssize_type ssize_type;
        void* malloc  (ssize_type size);
        void  free    (void* ptr);
        void* realloc (void* ptr, ssize_type size);

        /* Seqno related functions */

        /*!
         * Reinitialize seqno sequence (after SST or such)
         * Clears seqno->ptr map // and sets seqno_min to seqno.
         */
        void  seqno_reset (const gu::UUID& gid, seqno_t seqno);

        /*!
         * Assign sequence number to buffer pointed to by ptr
         */
        void  seqno_assign (const void* ptr,
                            int64_t     seqno_g,
                            int64_t     seqno_d);

        /*!
         * Release (free) buffers up to seqno
         */
        void seqno_release (int64_t seqno);

        /*!
         * Returns smallest seqno present in history
         */
        int64_t seqno_min() const
        {
            gu::Lock lock(mtx);
            if (gu_likely(!seqno2ptr.empty()))
                return seqno2ptr.begin()->first;
            else
                return -1;
        }

        /*!
         * Move lock to a given seqno.
         * @throws gu::NotFound if seqno is not in the cache.
         */
        void  seqno_lock (int64_t const seqno_g);

        /*!          DEPRECATED
         * Get pointer to buffer identified by seqno.
         * Moves lock to the given seqno.
         * @throws NotFound
         */
        const void* seqno_get_ptr (int64_t  seqno_g,
                                   int64_t& seqno_d,
                                   ssize_t& size);

        /*!
         * Returns allocated gcache memory pool size (in bytes).
         */
        size_t allocated_pool_size ();


        /*!
         * Implements the cleanup policy test.
         */
        bool cleanup_required()
        {
            return (params.keep_pages_size() && ps.total_size() > params.keep_pages_size()) ||
                   (params.keep_pages_count() && ps.total_pages() > params.keep_pages_count());
        }


        class Buffer
        {
        public:

            Buffer() : seqno_g_(), seqno_d_(), ptr_(), size_() { }

            Buffer (const Buffer& other)
                :
                seqno_g_(other.seqno_g_),
                seqno_d_(other.seqno_d_),
                ptr_    (other.ptr_),
                size_   (other.size_)
            { }

            Buffer& operator= (const Buffer& other)
            {
                seqno_g_ = other.seqno_g_;
                seqno_d_ = other.seqno_d_;
                ptr_     = other.ptr_;
                size_    = other.size_;
                return *this;
            }

            int64_t           seqno_g() const { return seqno_g_; }
            int64_t           seqno_d() const { return seqno_d_; }
            const gu::byte_t* ptr()     const { return ptr_;     }
            ssize_type        size()    const { return size_;    }

        protected:

            void set_ptr   (const void* p)
            {
                ptr_ = reinterpret_cast<const gu::byte_t*>(p);
            }

            void set_other (int64_t g, int64_t d, ssize_type s)
            {
                assert(s > 0);
                seqno_g_ = g; seqno_d_ = d; size_ = s;
            }

        private:

            int64_t           seqno_g_;
            int64_t           seqno_d_;
            const gu::byte_t* ptr_;
            ssize_type        size_; /* same type as passed to malloc() */

            friend class GCache;
        };

        /*!
         * Fills a vector with Buffer objects starting with seqno start
         * until either vector length or seqno map is exhausted.
         * Moves seqno lock to start.
         *
         * @retval number of buffers filled (<= v.size())
         */
        size_t seqno_get_buffers (std::vector<Buffer>& v, int64_t start);

        /*!
         * Releases any seqno locks present.
         */
        void seqno_unlock ();

        /*! @throws NotFound */
        void param_set (const std::string& key, const std::string& val);

        static size_t const PREAMBLE_LEN;

    private:

        typedef MemOps::size_type size_type;

        void free_common (BufferHeader*);

        gu::Config&     config;

        class Params
        {
        public:

            static void register_params(gu::Config&);

            Params(gu::Config&, const std::string&);

            const std::string& rb_name()  const { return rb_name_;  }
            const std::string& dir_name() const { return dir_name_; }

            size_t mem_size()            const { return mem_size_;         }
            size_t rb_size()             const { return rb_size_;          }
            size_t page_size()           const { return page_size_;        }
            size_t keep_pages_size()     const { return keep_pages_size_;  }
            size_t keep_pages_count()    const { return keep_pages_count_; }
            bool   recover()             const { return recover_;         }

            void mem_size         (size_t s) { mem_size_         = s; }
            void page_size        (size_t s) { page_size_        = s; }
            void keep_pages_size  (size_t s) { keep_pages_size_  = s; }
            void keep_pages_count (size_t c) { keep_pages_count_ = c; }

        private:

            std::string const rb_name_;
            std::string const dir_name_;
            size_t            mem_size_;
            size_t      const rb_size_;
            size_t            page_size_;
            size_t            keep_pages_size_;
            size_t            keep_pages_count_;
            bool        const recover_;
        }
            params;

        gu::Mutex       mtx;
        gu::Cond        cond;

        seqno2ptr_t     seqno2ptr;
        gu::UUID        gid;

        MemStore        mem;
        RingBuffer      rb;
        PageStore       ps;

        long long       mallocs;
        long long       reallocs;
        long long       frees;

        int64_t         seqno_locked;
        int64_t         seqno_max;
        int64_t         seqno_released;

#ifndef NDEBUG
        std::set<const void*> buf_tracker;
#endif

        /* returns true when successfully discards all seqnos up to s */
        bool discard_seqno (int64_t s);

        // disable copying
        GCache (const GCache&);
        GCache& operator = (const GCache&);
    };
}

#endif /* __GCACHE_H__ */
