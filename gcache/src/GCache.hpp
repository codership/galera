/*
 * Copyright (C) 2009-2014 Codership Oy <info@codership.com>
 */

#ifndef __GCACHE_H__
#define __GCACHE_H__

#include "gcache_mem_store.hpp"
#include "gcache_rb_store.hpp"
#include "gcache_page_store.hpp"

#include "gu_types.hpp"

#include <string>
#include <iostream>
#include <map>
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
        void* malloc  (ssize_t size);
        void  free    (void* ptr);
        void* realloc (void* ptr, ssize_t size);

        /* Seqno related functions */

        /*!
         * Reinitialize seqno sequence (after SST or such)
         * Clears seqno->ptr map // and sets seqno_min to seqno.
         */
        void  seqno_reset (/*int64_t seqno*/);

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

        class Buffer
        {
        public:

            Buffer() : seqno_g_(), ptr_(), size_(), seqno_d_() { }

            Buffer (const Buffer& other)
                :
                seqno_g_(other.seqno_g_),
                ptr_    (other.ptr_),
                size_   (other.size_),
                seqno_d_(other.seqno_d_)
            { }

            Buffer& operator= (const Buffer& other)
            {
                seqno_g_ = other.seqno_g_;
                ptr_     = other.ptr_;
                size_    = other.size_;
                seqno_d_ = other.seqno_d_;
                return *this;
            }

            int64_t           seqno_g() const { return seqno_g_; }
            const gu::byte_t* ptr()     const { return ptr_;     }
            ssize_t           size()    const { return size_;    }
            int64_t           seqno_d() const { return seqno_d_; }

        protected:

            void set_ptr   (const void* p)
            {
                ptr_ = reinterpret_cast<const gu::byte_t*>(p);
            }

            void set_other (ssize_t s, int64_t g, int64_t d)
            { size_ = s; seqno_g_ = g; seqno_d_ = d; }

        private:

            int64_t           seqno_g_;
            const gu::byte_t* ptr_;
            ssize_t           size_;
            int64_t           seqno_d_;

            friend class GCache;
        };

        /*!
         * Fills a vector with Buffer objects starting with seqno start
         * until either vector length or seqno map is exhausted.
         * Moves seqno lock to start.
         *
         * @retval number of buffers filled (<= v.size())
         */
        ssize_t seqno_get_buffers (std::vector<Buffer>& v,
                                   int64_t start);

        /*!
         * Releases any seqno locks present.
         */
        void seqno_unlock ();

        /*! @throws NotFound */
        void param_set (const std::string& key, const std::string& val);

        static size_t const PREAMBLE_LEN;

    private:

        void free_common (BufferHeader*);

        gu::Config&     config;

        class Params
        {
        public:

            static void register_params(gu::Config&);

            Params(gu::Config&, const std::string&);

            const std::string& rb_name()  const { return rb_name_;  }
            const std::string& dir_name() const { return dir_name_; }

            ssize_t mem_size()            const { return mem_size_;        }
            ssize_t rb_size()             const { return rb_size_;         }
            ssize_t page_size()           const { return page_size_;       }
            ssize_t keep_pages_size()     const { return keep_pages_size_; }

            void mem_size        (ssize_t s) { mem_size_        = s; }
            void page_size       (ssize_t s) { page_size_       = s; }
            void keep_pages_size (ssize_t s) { keep_pages_size_ = s; }

        private:

            std::string const rb_name_;
            std::string const dir_name_;
            ssize_t           mem_size_;
            ssize_t     const rb_size_;
            ssize_t           page_size_;
            ssize_t           keep_pages_size_;
        }
            params;

        gu::Mutex       mtx;
        gu::Cond        cond;

        typedef std::map<int64_t, const void*>  seqno2ptr_t;
        typedef seqno2ptr_t::iterator           seqno2ptr_iter_t;
        typedef std::pair<int64_t, const void*> seqno2ptr_pair_t;
        seqno2ptr_t     seqno2ptr;

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

        void constructor_common();

        /* returns true when successfully discards all seqnos up to s */
        bool discard_seqno (int64_t s);

        // disable copying
        GCache (const GCache&);
        GCache& operator = (const GCache&);
    };
}

#endif /* __GCACHE_H__ */
