/*
 * Copyright (C) 2010-2018 Codership Oy <info@codership.com>
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

        PageStore (const std::string& dir_name,
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

        void  free    (BufferHeader* bh) { assert(0); }

        void  repossess(BufferHeader* bh) { assert(0); }

        void  discard (BufferHeader* bh)
        {
            assert(BH_is_released(bh));
            assert(SEQNO_ILL == bh->seqno_g);
            free_page_ptr(static_cast<Page*>(BH_ctx(bh)), bh);
        }

        void  reset();

        void  set_page_size (size_t size) { page_size_ = size; }

        void  set_keep_size (size_t size) { keep_size_ = size; }

        void  set_debug(int dbg);

        /* for unit tests */
        size_t count()       const { return count_;        }
        size_t total_pages() const { return pages_.size(); }
        size_t total_size()  const { return total_size_;   }

    private:

        static int  const DEBUG = 4; // debug flag

        std::string const base_name_; /* /.../.../gcache.page. */
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
#ifndef GCACHE_DETACH_THREAD
        pthread_t         delete_thr_;
#endif /* GCACHE_DETACH_THREAD */

        void new_page    (size_type size);

        // returns true if a page could be deleted
        bool delete_page ();

        // cleans up extra pages.
        void cleanup     ();

        void* malloc_new (size_type size);

        void
        free_page_ptr (Page* page, BufferHeader* bh)
        {
            page->free(bh);
            if (0 == page->used()) cleanup();
        }

        PageStore(const gcache::PageStore&);
        PageStore& operator=(const gcache::PageStore&);
    };
}

#endif /* _gcache_page_store_hpp_ */
