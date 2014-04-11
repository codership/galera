/*
 * Copyright (C) 2010-2014 Codership Oy <info@codership.com>
 */

/*! @file page store implementation */

#include "gcache_page_store.hpp"
#include "gcache_bh.hpp"

#include <cstdio>
#include <cstring>
#include <pthread.h>

static const std::string base_name ("gcache.page.");

static std::string
make_base_name (const std::string& dir_name)
{
    if (dir_name.empty())
    {
        return base_name;
    }
    else
    {
        if (dir_name[dir_name.length() - 1] == '/')
        {
            return (dir_name + base_name);
        }
        else
        {
            return (dir_name + '/' + base_name);
        }
    }
}

static std::string
make_page_name (const std::string& base_name, ssize_t count)
{
    std::ostringstream os;
    os << base_name << std::setfill ('0') << std::setw (6) << count;
    return os.str();
}

static void*
remove_file (void* __restrict__ arg)
{
    char* const file_name (static_cast<char*>(arg));

    if (NULL != file_name)
    {
        if (remove (file_name))
        {
            int err = errno;

            log_error << "Failed to remove page file '" << file_name << "': "
                      << gu::to_string(err) << " (" << strerror(err) << ")";
        }
        else
        {
            log_info << "Deleted page " << file_name;
        }

        free (file_name);
    }
    else
    {
        log_error << "Null file name in " << __FUNCTION__;
    }

    pthread_exit(NULL);
}

bool
gcache::PageStore::delete_page ()
{
    Page* const page = pages_.front();

    if (page->used() > 0) return false;

    pages_.pop_front();

    char* const file_name(strdup(page->name().c_str()));

    total_size_ -= page->size();

    if (current_ == page) current_ = 0;

    delete page;

#ifdef GCACHE_DETACH_THREAD
    pthread_t delete_thr_;
#else
    if (delete_thr_ != pthread_t(-1)) pthread_join (delete_thr_, NULL);
#endif /* GCACHE_DETACH_THERAD */

    int err = pthread_create (&delete_thr_, &delete_page_attr_, remove_file,
                              file_name);
    if (0 != err)
    {
        delete_thr_ = pthread_t(-1);
        gu_throw_error(err) << "Failed to create page file deletion thread";
    }

    return true;
}

/* Deleting pages only from the beginning kinda means that some free pages
 * can be locked in the middle for a while. Leaving it like that for simplicity
 * for now. */
void
gcache::PageStore::cleanup ()
{
    while (total_size_   > keep_size_ &&
           pages_.size() > keep_page_ &&
           delete_page())
    {}
}

void
gcache::PageStore::reset ()
{
    while (pages_.size() > 0 && delete_page()) {};
}

inline void
gcache::PageStore::new_page (ssize_t size)
{
    Page* const page(new Page(this, make_page_name (base_name_, count_), size));

    pages_.push_back (page);
    total_size_ += size;
    current_ = page;
    count_++;
}

gcache::PageStore::PageStore (const std::string& dir_name,
                              ssize_t            keep_size,
                              ssize_t            page_size,
                              bool               keep_page)
    :
    base_name_ (make_base_name(dir_name)),
    keep_size_ (keep_size),
    page_size_ (page_size),
    keep_page_ (keep_page),
    count_     (0),
    pages_     (),
    current_   (0),
    total_size_(0),
    delete_page_attr_()
#ifndef GCACHE_DETACH_THREAD
    , delete_thr_(pthread_t(-1))
#endif /* GCACHE_DETACH_THREAD */
{
    int err = pthread_attr_init (&delete_page_attr_);

    if (0 != err)
    {
        gu_throw_error(err) << "Failed to initialize page file deletion "
                            << "thread attributes";
    }

#ifdef GCACHE_DETACH_THREAD
    err = pthread_attr_setdetachstate (&delete_page_attr_,
                                       PTHREAD_CREATE_DETACHED);
    if (0 != err)
    {
        pthread_attr_destroy (&delete_page_attr_);
        gu_throw_error(err) << "Failed to set DETACHED attribute to "
                            << "page file deletion thread";
    }
#endif /* GCACHE_DETACH_THREAD */
}

gcache::PageStore::~PageStore ()
{
    try
    {
        while (pages_.size() && delete_page()) {};
#ifndef GCACHE_DETACH_THREAD
        if (delete_thr_ != pthread_t(-1)) pthread_join (delete_thr_, NULL);
#endif /* GCACHE_DETACH_THREAD */
    }
    catch (gu::Exception& e)
    {
        log_error << e.what() << " in ~PageStore()"; // abort() ?
    }

    if (pages_.size() > 0)
    {
        log_error << "Could not delete " << pages_.size()
                  << " page files: some buffers are still \"mmapped\".";
    }

    pthread_attr_destroy (&delete_page_attr_);
}

inline void*
gcache::PageStore::malloc_new (ssize_t size)
{
    void* ret = 0;

    try
    {
        new_page (page_size_ > size ? page_size_ : size);
        ret = current_->malloc (size);
        cleanup();
    }
    catch (gu::Exception& e)
    {
        log_error << "Cannot create new cache page (out of disk space?): "
                  << e.what();
        // abort();
    }

    return ret;
}

void*
gcache::PageStore::malloc (ssize_t size)
{
    if (gu_likely (0 != current_))
    {
        register void* ret = current_->malloc (size);

        if (gu_likely(0 != ret)) return ret;

        current_->drop_fs_cache();
    }

    return malloc_new (size);
}

void*
gcache::PageStore::realloc (void* ptr, ssize_t size)
{
    assert(ptr != 0);

    BufferHeader* const bh(ptr2BH(ptr));
    Page* const page(static_cast<Page*>(bh->ctx));

    void* ret(page->realloc(ptr, size));

    if (0 != ret) return ret;

    ret = malloc_new (size);

    if (gu_likely(0 != ret))
    {
        ssize_t const ptr_size(bh->size - sizeof(BufferHeader));

        memcpy (ret, ptr, size > ptr_size ? ptr_size : size);
        free_page_ptr (page, bh);
    }

    return ret;
}
