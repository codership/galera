/*
 * Copyright (C) 2009-2025 Codership Oy <info@codership.com>
 */

#ifndef __GCACHE_H__
#define __GCACHE_H__

#include "gcache_seqno.hpp"
#include "gcache_mem_store.hpp"
#include "gcache_rb_store.hpp"
#include "gcache_page_store.hpp"
#include "gcache_types.hpp"

#include <gu_types.hpp>
#include <gu_lock.hpp> // for gu::Mutex and gu::Cond
#include <gu_config.hpp>
#include <gu_gtid.hpp>

#include <wsrep_api.h> // encryption declarations

#include <string>
#include <iostream>
#ifndef NDEBUG
#include <set>
#endif
#include <stdint.h>

namespace gcache
{
    class GCache : public SeqnoMap
    {
    public:

        static const std::string& PARAMS_DIR;

        static void register_params(gu::Config& cfg)
        {
            Params::register_params(cfg);
        }

        /*!
         * Creates a new gcache file in "gcache.name" conf parameter or
         * in data_dir. If file already exists, it gets overwritten.
         */
        GCache (ProgressCallback*  pcb,
                gu::Config&        cfg,
                const std::string& data_dir,
                wsrep_encrypt_cb_t encrypt_cb = NULL,
                void*              app_ctx    = NULL);

        virtual ~GCache();

        /*! prints object properties */
        void  print (std::ostream& os);

        /* Resets storage */
        void  reset();

        /* Sets encryption key */
        void set_enc_key(const wsrep_enc_key_t& key);

        /*!
         * Memory allocation methods
         *
         * malloc() and realloc() allocate space in the cache and return
         * pointers to it. That return value identifies the allocated resource
         * and should be used as an argument to free() to release it, similar
         * to standard libc malloc(), realloc() and free().
         * If cache is encrypted, a corresponding "shadow" plaintext buffer
         * pointer is passed in the ptx argument, otherwise contents of ptx is
         * identical to the return value.
         * In other words, the return value should be used to identify cache
         * resource, whereas the value passed in ptx should be used to read/
         * write to it.
         * Wherever the following methods below take void* as an argument,
         * it is meant to be a return value of malloc()/realloc(), NOT ptx.
         */
        typedef MemOps::ssize_type ssize_type;
        void* malloc  (ssize_type size, void*& ptx);
        void* realloc (void* ptr, ssize_type size, void*& ptx);
        void  free    (void* ptr);

        /*!
         * Retrieve plaintext buffer by pointer to ciphertext.
         * Repeated calls shall return the same pointer, i.e. there is only one
         * plaintext buffer per ciphertext.
         * The plaintext pointer shall be valid at least until drop_plaintext()
         * or free() is called. Subsequent call to get_plaintext() is not
         * guranteed to return the same pointer.
         */
        inline const void* get_ro_plaintext(const void* cphr)
        {
            return get_plaintext(cphr, false);
        }
        inline void* get_rw_plaintext(void* cphr)
        {
            return get_plaintext(cphr, true);
        }

        /*!
         * Allow to drop the plaintext buffer identified by ciphertext pointer
         * from cache
         */
        inline void  drop_plaintext(const void* cphr)
        {
            if (encrypt_cache)
            {
                gu::Lock lock(mtx);
                ps.drop_plaintext(cphr);
            }
        }

        /* Seqno related functions */

        /*!
         * Reinitialize seqno sequence (after SST or such)
         * Clears seqno->ptr map // and sets seqno_min to seqno.
         */
        void seqno_reset (const gu::GTID& gtid);

        /*!
         * Assign sequence number to buffer pointed to by ptr
         */
        void seqno_assign (const void* ptr,
                           seqno_t     seqno_g,
                           uint8_t     type,
                           bool        skip);

        /*!
         * Mark buffer to be skipped
         */
        void seqno_skip (const void* ptr,
                         seqno_t     seqno_g,
                         uint8_t     type);

        /*!
         * Release (free) buffers up to seqno
         */
        void seqno_release (seqno_t seqno);

        /*!
         * Discard (forget) seqnos up to and including seqno
         */
        void seqno_discard (const seqno_t& seqno);

        void set_low_limit (const seqno_t& seqno)
        {
            assert(mtx.owned() || in_dtor );
            seqno_low_ = std::max(seqno_low_, seqno);
        }

        /*!
         * Returns smallest seqno present in history
         */
        seqno_t seqno_min() const
        {
            gu::Lock lock(mtx);
            if (gu_likely(!seqno2ptr.empty()) &&
                seqno2ptr.index_end() > seqno_low_)
                return std::max(seqno2ptr.index_begin(), seqno_low_ + 1);
            else
                return SEQNO_ILL;
        }

        /*!
         * Move lock to a given seqno.
         * @throws gu::NotFound if seqno is not in the cache.
         */
        void  seqno_lock (seqno_t seqno_g);

        /*!
         * Get pointer to buffer identified by seqno.
         * Moves lock to the given seqno and clears released flag if any.
         * The buffer will need to be "freed" again.
         * @throws NotFound
         */
        const void* seqno_get_ptr (seqno_t seqno_g, ssize_t& size);

        class Buffer
        {
        public:

            Buffer() : seqno_g_(), ptr_(), size_(), skip_(), type_() { }

            Buffer (const Buffer& other)
                :
                seqno_g_(other.seqno_g_),
                ptr_    (other.ptr_),
                size_   (other.size_),
                skip_   (other.skip_),
                type_   (other.type_)
            { }

            Buffer& operator= (const Buffer& other)
            {
                seqno_g_ = other.seqno_g_;
                ptr_     = other.ptr_;
                size_    = other.size_;
                skip_    = other.skip_;
                type_    = other.type_;
                return *this;
            }

            seqno_t           seqno_g() const { return seqno_g_; }
            const gu::byte_t* ptr()     const { return ptr_;     }
            ssize_type        size()    const { return size_;    }
            bool              skip()    const { return skip_;    }
            uint8_t           type()    const { return type_;    }

        protected:

            void set_ptr   (const void* p)
            {
                ptr_ = reinterpret_cast<const gu::byte_t*>(p);
            }

            void set_other (seqno_t g, ssize_type s, bool skp, uint8_t t)
            {
                assert(s > 0);
                seqno_g_ = g; size_ = s; skip_ = skp, type_ = t;
            }

        private:

            seqno_t           seqno_g_;
            const gu::byte_t* ptr_;
            ssize_type        size_;
            bool              skip_;
            uint8_t           type_;

            friend class GCache;
        };

        /*!
         * Fills a vector with Buffer objects starting with seqno start
         * until either vector length or seqno map is exhausted.
         * Moves seqno lock to start.
         *
         * @retval number of buffers filled (<= v.size())
         */
        size_t seqno_get_buffers (std::vector<Buffer>& v, seqno_t start);

        /*!
         * Releases any seqno locks present.
         */
        void seqno_unlock ();

        /*! @throws NotFound */
        void param_set (const std::string& key, const std::string& val);

        /* prints out buffer metadata */
        std::string meta(const void* ptr);

        static size_t const PREAMBLE_LEN;

#ifdef GCACHE_UNIT_TEST
        const PageStore& page_store()  const { return ps; }
        const seqno2ptr_t& seqno_map() const { return seqno2ptr; }
#endif /* GCACHE_UNIT_TEST */
    private:

        typedef MemOps::size_type size_type;

        void free_common (BufferHeader*, const void*);

        gu::Config&     config;

        class Params
        {
        public:

            static void register_params(gu::Config&);

            Params(gu::Config&, const std::string&);

            const std::string& rb_name()  const { return rb_name_;  }
            const std::string& dir_name() const { return dir_name_; }

            size_t mem_size()            const { return mem_size_;        }
            size_t rb_size()             const { return rb_size_;         }
            size_t page_size()           const { return page_size_;       }
            size_t keep_pages_size()     const { return keep_pages_size_; }
            size_t keep_plaintext_size() const { return keep_plaintext_size_;}
            int    debug()               const { return debug_;           }
            bool   recover()             const { return recover_;         }

            void mem_size        (size_t s) { mem_size_        = s; }
            void page_size       (size_t s) { page_size_       = s; }
            void keep_pages_size (size_t s) { keep_pages_size_ = s; }
            void keep_plaintext_size (size_t s) { keep_plaintext_size_ = s; }
#ifndef NDEBUG
            void debug           (int    d) { debug_           = d; }
#endif

        private:

            std::string const rb_name_;
            std::string const dir_name_;
            size_t            mem_size_;
            size_t      const rb_size_;
            size_t            page_size_;
            size_t            keep_pages_size_;
            size_t            keep_plaintext_size_;
            int               debug_;
            bool        const recover_;
        }
            params;

        gu::Mutex       mtx;

        seqno2ptr_t     seqno2ptr;
        gu::UUID        gid;

        MemStore        mem;
        RingBuffer      rb;
        PageStore       ps;

        long long       mallocs;
        long long       reallocs;
        long long       frees;

        seqno_t         seqno_max;
        seqno_t         seqno_released;

        seqno_t         seqno_low_;
        seqno_t         seqno_locked;
        int             seqno_locked_count;

        bool const      encrypt_cache;

#ifndef NDEBUG
        std::set<const void*> buf_tracker;
        bool in_dtor;
#endif

        template <typename T> /* T assumes void* or const void* */
        T get_plaintext(T const cphr, bool const writable)
        {
            if (encrypt_cache)
            {
                gu::Lock lock(mtx);
                return ps.get_plaintext(cphr, writable);
            }
            else
                return (cphr);
        }

        /* change == true will mark plaintext as changed */
        BufferHeader* get_BH(const void* ptr, bool change = false)
        {
            return encrypt_cache ? ps.get_BH(ptr, change) : ptr2BH(ptr);
        }

        void discard_buffer (BufferHeader* bh, const void* ptr);

        /* discard buffers while condition.check() is true */
        template <typename T>
        bool discard (T& condition);

        /* returns true when successfully discards all seqnos up to s */
        bool discard_seqno (seqno_t s);

        /* returns true when successfully discards at least size of buffers */
        bool discard_size (size_t size);

        /* discards all seqnos greater than s */
        void discard_tail (seqno_t s);

        // disable copying
        GCache (const GCache&);
        GCache& operator = (const GCache&);
    };
}

#endif /* __GCACHE_H__ */
