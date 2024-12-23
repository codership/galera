/*
 * Copyright (C) 2010-2025 Codership Oy <info@codership.com>
 */

/*! @file page store class */

#ifndef _gcache_page_store_hpp_
#define _gcache_page_store_hpp_

#include "gcache_memops.hpp"
#include "gcache_page.hpp"
#include "gcache_seqno.hpp"

#include <string>
#include <deque>

namespace gcache
{
    class PageStore : public MemOps
    {
    public:

        PageStore (SeqnoMap&          seqno_map,
                   const std::string& dir_name,
                   size_t             keep_size,
                   size_t             page_size,
                   int                dbg,
                   bool               keep_page);

        ~PageStore ();

        static PageStore* page_store(const Page* p)
        {
            return static_cast<PageStore*>(p->parent());
        }

        void* malloc  (size_type size);

        void* realloc (void* ptr, size_type size);

        void  free    (BufferHeader* bh)
        {
            assert(BH_is_released(bh));
            free_page_ptr(static_cast<Page*>(BH_ctx(bh)), bh);
        }

        void  repossess(BufferHeader* bh)
        {
            assert(BH_is_released(bh));
            static_cast<Page*>(BH_ctx(bh))->repossess(bh);
        }

        void  discard (BufferHeader* bh)
        {
            assert(BH_is_released(bh));
            static_cast<Page*>(BH_ctx(bh))->discard(bh);
        }

        void  reset();

        void  seqno_assign(BufferHeader* bh, seqno_t s)
        {
            static_cast<Page*>(BH_ctx(bh))->seqno_assign(s);
        }

        void  seqno_lock(seqno_t s)
        {
            assert(s < seqno_locked_);
            seqno_locked_ = s;
        }

        void  seqno_unlock()
        {
            seqno_locked_ = SEQNO_MAX;
            cleanup();
        }

        void  set_page_size (size_t size) { page_size_ = size; }

        void  set_keep_size (size_t size) { keep_size_ = size; }

        void  set_debug(int dbg);

        /* for unit tests */
        size_t count()       const { return count_;        }
        size_t total_pages() const { return pages_.size(); }
        size_t total_size()  const { return total_size_;   }
        size_t keep_size()   const { return keep_size_;    }
        size_t keep_page()   const { return keep_page_;    }
        size_t page_size()   const { return page_size_;    }

#ifdef GCACHE_PAGE_STORE_UNIT_TEST
        void   wait_page_discard() const;
#endif /* GCACHE_PAGE_STORE_UNIT_TEST */

    private:

        static int  const DEBUG = 4; // debug flag

        SeqnoMap&         seqno_map_;
        std::string const base_name_; /* /.../.../gcache.page. */
        seqno_t           seqno_locked_;
        size_t            keep_size_; /* how much pages to keep after freeing*/
        size_t            page_size_; /* min size of the individual page */
        bool        const keep_page_; /* whether to keep the last page */
        size_t            count_;
        typedef std::deque<Page*> PageQueue;
        PageQueue         pages_;
        Page*             current_;
        size_t            total_size_;
        pthread_attr_t    delete_page_attr_;
        int               debug_;
        mutable pthread_t delete_thr_;

        void new_page    (size_type size);

        // returns true if a page could be deleted
        bool delete_page ();

        // cleans up extra pages.
        void cleanup     ();
#ifndef GCACHE_PAGE_STORE_UNIT_TEST
        void wait_page_discard() const;
#endif /* !GCACHE_PAGE_STORE_UNIT_TEST */

        void* malloc_new (size_type size);

        void
        free_page_ptr (Page* page, BufferHeader* bh)
        {
            page->free(bh);
            if (0 == page->used() && pages_.front() == page) cleanup();
        }

        PageStore(const gcache::PageStore&);
        PageStore& operator=(const gcache::PageStore&);
    };
}

#endif /* _gcache_page_store_hpp_ */
