/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
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
                   ssize_t            disk_size,
                   ssize_t            page_size);

        ~PageStore ();

        void* malloc  (ssize_t size) throw (gu::Exception);

        void  free    (void*   ptr)  throw();

        void* realloc (void*   ptr, ssize_t size) throw (gu::Exception);

        void  reset() throw (gu::Exception);

        ssize_t count() const throw() { return count_; } // for unit tests

    private:

        std::string const base_name_; /* /.../.../gcache.page. */
        ssize_t     const disk_size_; /* free pages be deleted when exceeded */
        ssize_t     const page_size_; /* min size of the individual page */
        ssize_t           count_;
        std::deque<Page*> pages_;
        Page*             current_;
        ssize_t           total_size_;

        void new_page    (ssize_t size) throw (gu::Exception);

        // returns true if a page could be deleted
        bool delete_page () throw (gu::Exception);

        // cleans up extra pages.
        void cleanup     () throw (gu::Exception);

        void* malloc_new (ssize_t size) throw ();

        void
        free_page_ptr (Page* page, void* ptr)
        {
            page->free(ptr);
            if (0 == page->used()) cleanup();
        }

        PageStore(const gcache::PageStore&);
        PageStore& operator=(const gcache::PageStore&);
    };
}

#endif /* _gcache_page_store_hpp_ */
