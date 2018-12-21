/* Copyright (C) 2013-2016 Codership Oy <info@codership.com> */
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
#include <iostream>

namespace gu
{

class Allocator
{
public:

    class BaseName
    {
    public:
        virtual void print(std::ostream& os) const = 0;
        virtual ~BaseName() {}
    };

    // this questionable optimization reduces Allocator size by 8
    // probably not worth the loss of generality.
    typedef unsigned int   page_size_type; // max page size
    typedef page_size_type heap_size_type; // max heap store size

    explicit
    Allocator (const BaseName&     base_name      = BASE_NAME_DEFAULT,
               void*               reserved       = NULL,
               page_size_type      reserved_size  = 0,
               heap_size_type      max_heap       = (1U << 22),   /* 4M  */
               page_size_type      disk_page_size = (1U << 26));  /* 64M */

    ~Allocator ();

    /*! @param new_page - true if not adjucent to previous allocation */
    byte_t* alloc (page_size_type const size, bool& new_page);

    /* Total allocated size */
    size_t size () const { return size_; }

    /* Total count of pages */
    size_t count() const { return pages_->size(); }

#ifdef GU_ALLOCATOR_DEBUG
    /* appends own vector of Buf structures to the passed one,
     * should be called only after all allocations have been made.
     * returns sum of all appended buffers' sizes (same as size())  */
    size_t gather (std::vector<Buf>& out) const;
#endif /* GU_ALLOCATOR_DEBUG */

    /* After we allocated 3 heap pages, spilling vector into heap should not
     * be an issue. */
    static size_t const INITIAL_VECTOR_SIZE = 4;

private:

    class Page /* base class for memory and file pages */
    {
    public:

        Page (void* ptr, size_t size)
            : base_ptr_(static_cast<gu::byte_t*>(ptr)),
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

        byte_t*        base_ptr_;
        byte_t*        ptr_;
        page_size_type left_;

        Page& operator=(const Page&);
        Page (const Page&);
    };

    class HeapPage : public Page
    {
    public:

        HeapPage (page_size_type max_size);

        ~HeapPage () { free (base_ptr_); }
    };

    class FilePage : public Page
    {
    public:

        FilePage (const std::string& name, page_size_type size);

        ~FilePage () { fd_.unlink(); }

    private:

        FileDescriptor fd_;
        MMap           mmap_;
    };

    class PageStore
    {
    public:

        Page* new_page (page_size_type size) { return my_new_page(size); }

    protected:

        virtual ~PageStore() {}

    private:

        virtual Page* my_new_page (page_size_type size) = 0;
    };

    class HeapStore : public PageStore
    {
    public:

        HeapStore (heap_size_type max) : PageStore(), left_(max) {}

        ~HeapStore () {}

    private:

        heap_size_type left_;

        Page* my_new_page (page_size_type const size);
    };

    class FileStore : public PageStore
    {
    public:

        FileStore (const BaseName& base_name,
                   page_size_type  page_size)
            :
            PageStore(),
            base_name_(base_name),
            page_size_(page_size),
            n_        (0)
        {}

        ~FileStore() {}

        const BaseName& base_name() const { return base_name_; }
        int             size() const { return n_; }

    private:

        const BaseName&      base_name_;
        page_size_type const page_size_;
        int                  n_;

        Page* my_new_page (page_size_type const size);

        FileStore (const FileStore&);
        FileStore& operator= (const FileStore&);
    };

    Page       first_page_;
    Page*      current_page_;

    HeapStore  heap_store_;
    FileStore  file_store_;
    PageStore* current_store_;

    gu::Vector<Page*, INITIAL_VECTOR_SIZE> pages_;

#ifdef GU_ALLOCATOR_DEBUG
    gu::Vector<Buf,   INITIAL_VECTOR_SIZE> bufs_;
    void add_current_to_bufs();
#endif /* GU_ALLOCATOR_DEBUG */

    size_t     size_;

    Allocator(const gu::Allocator&);
    const Allocator& operator=(const gu::Allocator&);

    class BaseNameDefault : public BaseName
    {
    public:
        BaseNameDefault() {} // this is seemingly required by the standard
        void print(std::ostream& os) const { os << "alloc"; }
    };

    static BaseNameDefault const BASE_NAME_DEFAULT;

}; /* class Allocator */

inline
std::ostream& operator<< (std::ostream& os, const Allocator::BaseName& bn)
{
    bn.print(os); return os;
}

} /* namespace gu */

#endif /* _GU_ALLOC_HPP_ */
