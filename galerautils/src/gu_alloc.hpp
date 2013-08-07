/* Copyright (C) 2013 Codership Oy <info@codership.com> */
/*!
 * @file Continuous buffer allocator for RecordSet
 *
 * $Id$
 */

#ifndef _GU_ALLOC_HPP_
#define _GU_ALLOC_HPP_

#include "gu_fdesc.hpp"
#include "gu_mmap.hpp"
#include "gu_buf.hpp"

#include "gu_macros.h" // gu_likely()

#include <cstdlib> // realloc(), free()
#include <string>
#include <list>
#include <vector>

namespace gu
{

class Allocator
{
public:

    Allocator (const std::string& base_name,
               size_t             max_ram        = (1U << 22),   /* 4M  */
               size_t             disk_page_size = (1U << 26));  /* 64M */

    ~Allocator ();

    /*! @param new_page - true if not adjucent to previous allocation */
    byte_t* alloc (size_t const size, bool& new_page);

    /* Total allocated size */
    size_t size () const { return size_; }

    /* Total count of pages */
    size_t count() const { return pages_.size(); }

    /* appends own vector of Buf structures to the passed one,
     * should be called only after all allocations have been made.
     * returns sum of all appended buffers' sizes (same as size())  */
    size_t gather (std::vector<Buf>& out) const;

private:

    class Page /* base class for memory and disk pages */
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
            byte_t* ret = 0;

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

    class MemPage : public Page
    {
    public:

        MemPage (size_t max_size);

        ~MemPage () { free (base_ptr_); }
    };

    class DiskPage : public Page
    {
    public:

        DiskPage (const std::string& name, size_t size);

        ~DiskPage () { fd_.unlink(); }

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

    class MemStore : public PageStore
    {
    public:

        MemStore (size_t max) : PageStore(), left_(max) {}

        virtual ~MemStore () {}

    private:

        size_t left_;

        /* to avoid too frequent allocation, make it 128K */
        static size_t const PAGE_SIZE;

        Page* my_new_page (size_t const size);
    };

    class DiskStore : public PageStore
    {
    public:

        DiskStore (const std::string& base_name,
                   size_t             page_size);

        virtual ~DiskStore() {}

    private:

        std::string const base_name_;
        size_t const      page_size_;
        int               n_;

        Page* my_new_page (size_t const size);

#if NOT_USED
        static std::string make_base_name (const std::string& dir_name,
                                           const std::string& base_name)
        {
            std::ostringstream fname;

            if (!disk_dir_.empty())
            {
                fname << disk_dir_
                      << (disk_dir_[disk_dir_.length() - 1] == '/' ? "" : "/");
            }

            fname << base_name_ << '.';

            return fname.str();
        }
#endif /* NOT_USED */
    };

    MemStore   mem_store_;
    DiskStore  disk_store_;
    PageStore* current_store_;
    Page*      current_page_;

    std::vector<Page*> pages_;
    std::vector<Buf>   bufs_;
    size_t             size_;

    void add_current_to_bufs();

    Allocator(const gu::Allocator&);
    const Allocator& operator=(const gu::Allocator&);
};

}

#endif /* _GU_BUF_HPP_ */
