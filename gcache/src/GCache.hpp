/*
 * Copyright (C) 2009-2010 Codership Oy <info@codership.com>
 */

#ifndef __GCACHE_H__
#define __GCACHE_H__

#include "gcache_mem_store.hpp"
#include "gcache_rb_store.hpp"
#include "gcache_page_store.hpp"

#include <galerautils.hpp>

#include <string>
#include <iostream>
#include <map>
#ifndef NDEBUG
#include <set>
#endif
#include <stdint.h>

namespace gcache
{

    class GCache : public MemOps
    {

    public:

        /*!
         * Creates a new gcache file in "gcache.name" conf parameter or
         * in data_dir. If file already exists, it gets overwritten.
         */
        GCache (gu::Config& cfg, const std::string& data_dir);

        virtual ~GCache();

        /*! prints object properties */
        void print (std::ostream& os);

        /* Resets storage */
        void reset();

        /* Memory allocation functions */
        void* malloc  (ssize_t size) throw (gu::Exception);
        void  free    (const void* ptr) throw ();
        void* realloc (void* ptr, ssize_t size) throw (gu::Exception);

        /* Seqno related functions */

        /*!
         * Reinitialize seqno sequence (after SST or such)
         * Clears cache from buffers with seqnos and sets seqno_min to seqno.
         */
        void    seqno_init (int64_t seqno);

        /*!
         * Assign sequence number to buffer pointed to by ptr
         */
        void    seqno_assign (const void* ptr,
                              int64_t     seqno_g,
                              int64_t     seqno_d,
                              bool        release);

        /*!
         * Get the smallest seqno present in the cache.
         * Locks seqno from removal.
         */
        int64_t seqno_get_min ();

        /*!          DEPRECATED
         * Get pointer to buffer identified by seqno.
         * Moves lock to the given seqno.
         */
        const void* seqno_get_ptr (int64_t  seqno_g,
                                   int64_t& seqno_d,
                                   ssize_t& size)
            throw (gu::NotFound);

        class Buffer
        {
        public:
            Buffer() : ptr_(), size_(), seqno_g_(), seqno_d_() { }
            Buffer (const Buffer& other)
                :
                ptr_(other.ptr_),
                size_(other.size_),
                seqno_g_(other.seqno_g_),
                seqno_d_(other.seqno_d_)
            { }
            const void* ptr()     const { return ptr_;     }
            ssize_t     size()    const { return size_;    }
            int64_t     seqno_g() const { return seqno_g_; }
            int64_t     seqno_d() const { return seqno_d_; }

        protected:

            void set_ptr   (const void* p) { ptr_ = p; }

            void set_other (ssize_t s, int64_t g, int64_t d)
            { size_ = s; seqno_g_ = g; seqno_d_ = d; }

        private:


            Buffer& operator= (const Buffer&);

            const void* ptr_;
            ssize_t     size_;
            int64_t     seqno_g_;
            int64_t     seqno_d_;

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
        void seqno_release ();

        void
        param_set (const std::string& key, const std::string& val)
            throw (gu::Exception, gu::NotFound);

        static size_t const PREAMBLE_LEN;

    private:

        void discard (BufferHeader*) throw() {}

        void free_common (BufferHeader*bh) throw()
        {
            void* const ptr(bh + 1);

#ifndef NDEBUG
            std::set<const void*>::iterator it = buf_tracker.find(ptr);
            if (it == buf_tracker.end())
            {
                log_fatal << "Have not allocated this ptr: " << ptr;
                abort();
            }
            buf_tracker.erase(it);
#endif
            frees++;

            switch (bh->store)
            {
            case BUFFER_IN_MEM:  mem.free (ptr); break;
            case BUFFER_IN_RB:   rb.free  (ptr); break;
            case BUFFER_IN_PAGE:
                if (gu_likely(bh->seqno_g > 0))
                {
                    discard_seqno (bh->seqno_g);
                }
                ps.free (ptr); break;
            }
        }

        gu::Config&     config;

        class Params
        {
        public:
            Params(gu::Config&, const std::string&) throw (gu::Exception);
            std::string const rb_name;
            std::string const dir_name;
            ssize_t           mem_size;
            ssize_t     const rb_size;
            ssize_t           page_size;
            ssize_t           keep_pages_size;
        }
            params;

        gu::Mutex       mtx;
        gu::Cond        cond;

        typedef std::map<int64_t, const void*> seqno2ptr_t;
        seqno2ptr_t     seqno2ptr;

        typedef seqno2ptr_t::iterator           seqno2ptr_iter_t;
        typedef std::pair<int64_t, const void*> seqno2ptr_pair_t;

        MemStore        mem;
        RingBuffer      rb;
        PageStore       ps;

        long long       mallocs;
        long long       reallocs;
        long long       frees;

        int64_t         seqno_locked;
        int64_t         seqno_min;
        int64_t         seqno_max;

#ifndef NDEBUG
        std::set<const void*> buf_tracker;
#endif

        void constructor_common();
        void discard_seqno (int64_t);

        // disable copying
        GCache (const GCache&);
        GCache& operator = (const GCache&);
    };
}

#endif /* __GCACHE_H__ */
