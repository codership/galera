/* Copyright (C) 2013 Codership Oy <info@codership.com> */
/*!
 * @file Continuous buffer allocator for RecordSet
 *
 * $Id$
 */

#ifndef _GU_ALLOC_HPP_
#define _GU_ALLOC_HPP_

#include "gu_string.hpp"
#include "gu_fdesc.hpp"
#include "gu_mmap.hpp"
#include "gu_buf.hpp"
#include "gu_vector.hpp"

#include "gu_macros.h" // gu_likely()

#include <cstdlib>     // realloc(), free()
#include <string>

namespace gu
{

class Allocator
{
public:

    Allocator (byte_t*             reserved,
               size_t              reserved_size,
               const StringBase<>& base_name,
               size_t              max_ram        = (1U << 22),   /* 4M  */
               size_t              disk_page_size = (1U << 26));  /* 64M */

    ~Allocator ();

    /*! @param new_page - true if not adjucent to previous allocation */
    byte_t* alloc (size_t const size, bool& new_page);

    /* Total allocated size */
    size_t size () const { return size_; }

    /* Total count of pages */
    size_t count() const { return pages_->size(); }

    /* appends own vector of Buf structures to the passed one,
     * should be called only after all allocations have been made.
     * returns sum of all appended buffers' sizes (same as size())  */
    size_t gather (std::vector<Buf>& out) const;

    /* After we allocated 3 heap pages, spilling vector into heap should not
     * be an issue. */
    static size_t const INITIAL_VECTOR_SIZE = 4;

private:

    class Page /* base class for memory and file pages */
    {
    public:

        Page (byte_t* ptr, size_t size)
            : base_ptr_(ptr),
              ptr_     (base_ptr_),
              left_    (size)
        {}

        virtual ~Page() {};

        byte_t* alloc (size_t size)
        {
            byte_t* ret = NULL;

            if (gu_likely(size <= left_))
            {
                ret   =  ptr_;
                ptr_  += size;
                left_ -= size;
            }

            return ret;
        }

        const byte_t* base() const { return base_ptr_; }
        ssize_t       size() const { return ptr_ - base_ptr_; }

    protected:

        byte_t* base_ptr_;
        byte_t* ptr_;
        size_t  left_;

        Page& operator=(const Page&);
        Page (const Page&);
    };

    class HeapPage : public Page
    {
    public:

        HeapPage (size_t max_size);

        ~HeapPage () { free (base_ptr_); }
    };

    class FilePage : public Page
    {
    public:

        FilePage (const std::string& name, size_t size);

        ~FilePage () { fd_.unlink(); }

    private:

        FileDescriptor fd_;
        MMap           mmap_;
    };

    class PageStore
    {
    public:

        Page* new_page (size_t size) { return my_new_page(size); }

    protected:

        virtual ~PageStore() {}

    private:

        virtual Page* my_new_page (size_t size) = 0;
    };

    class HeapStore : public PageStore
    {
    public:

        HeapStore (size_t max) : PageStore(), left_(max) {}

        ~HeapStore () {}

    private:

        /* to avoid too frequent allocation, make it 64K */
        static size_t const PAGE_SIZE = 1U << 16;

        size_t left_;

        Page* my_new_page (size_t const size);
    };

    class FileStore : public PageStore
    {
    public:

        FileStore (const StringBase<>& base_name,
                   size_t              page_size)
            : PageStore(),
              base_name_(base_name),
              page_size_(page_size),
              n_        (0)
        {}

        ~FileStore() {}

    private:

        const String<256> base_name_;
        size_t const      page_size_;
        int               n_;

        Page* my_new_page (size_t const size);
    };

    byte_t     buf_[ 1U << 12 ]; /* 4K buffer optimistically preallocated
                                  * together with the object in hopes to avoid
                                  * additional allocations. */
    Page       first_page_;
    Page*      current_page_;

    HeapStore  heap_store_;
    FileStore  file_store_;
    PageStore* current_store_;

    gu::Vector<Page*, INITIAL_VECTOR_SIZE> pages_;
    gu::Vector<Buf,   INITIAL_VECTOR_SIZE> bufs_;

    size_t     size_;

    void add_current_to_bufs();

    Allocator(const gu::Allocator&);
    const Allocator& operator=(const gu::Allocator&);
};

}

#endif /* _GU_BUF_HPP_ */
