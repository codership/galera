/*
 * Copyright (C) 2010-2014 Codership Oy <info@codership.com>
 */

/*! @file page store class */

#ifndef _gcache_page_store_hpp_
#define _gcache_page_store_hpp_

#include "gcache_memops.hpp"
#include "gcache_page.hpp"

#include <string>
#include <deque>

namespace gcache
{
    class PageStore : public MemOps
    {
    public:

        PageStore (const std::string& dir_name,
                   ssize_t            keep_size,
                   ssize_t            page_size,
                   bool               keep_page);

        ~PageStore ();

        static PageStore* page_store(const Page* p)
        {
            return static_cast<PageStore*>(p->parent());
        }

        void* malloc  (ssize_t size);

        void  free    (BufferHeader* bh) { assert(0); }

        void* realloc (void* ptr, ssize_t size);

        void  discard (BufferHeader* bh)
        {
            assert(BH_is_released(bh));
            assert(SEQNO_ILL == bh->seqno_g);
            free_page_ptr(static_cast<Page*>(bh->ctx), bh);
        }

        void  reset();

        ssize_t count() const { return count_; } // for unit tests

        void  set_page_size (ssize_t size) { page_size_ = size; }

        void  set_keep_size (ssize_t size) { keep_size_ = size; }

    private:

        std::string const base_name_; /* /.../.../gcache.page. */
        ssize_t           keep_size_; /* how much pages to keep after freeing*/
        ssize_t           page_size_; /* min size of the individual page */
        bool        const keep_page_; /* whether to keep the last page */
        ssize_t           count_;
        std::deque<Page*> pages_;
        Page*             current_;
        ssize_t           total_size_;
        pthread_attr_t    delete_page_attr_;
#ifndef GCACHE_DETACH_THREAD
        pthread_t         delete_thr_;
#endif /* GCACHE_DETACH_THREAD */

        void new_page    (ssize_t size);

        // returns true if a page could be deleted
        bool delete_page ();

        // cleans up extra pages.
        void cleanup     ();

        void* malloc_new (ssize_t size) ;

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
