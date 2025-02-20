/*
 * Copyright (C) 2010-2025 Codership Oy <info@codership.com>
 */

/*! @file page store implementation */

#include "gcache_page_store.hpp"
#include "gcache_bh.hpp"
#include "gcache_limits.hpp"

#include <gu_logger.hpp>
#include <gu_throw.hpp>

#include <cstdio>
#include <cstring>
#include <pthread.h>

#include <iomanip>

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
make_page_name (const std::string& base_name, size_t count)
{
    std::ostringstream os;
    os << base_name << std::setfill ('0') << std::setw (6) << count;
    return os.str();
}

static void
remove_file (const std::string& file_name)
{
    if (file_name.length() > 0)
    {
        if (::remove(file_name.c_str()))
        {
            int err = errno;

            log_error << "Failed to remove page file '" << file_name
                      << "': " << err << " (" << strerror(err) << ")";
        }
        else
        {
            log_info << "Deleted page " << file_name;
        }
    }
    else
    {
        log_error << "Empty file name in " << __FUNCTION__;
    }
}

struct delete_thread_arg
{
    gcache::SeqnoMap& seqno_map_;
    gcache::Page&     page_;
    pthread_t         previous_thread_;
    bool              debug_;

    delete_thread_arg(gcache::SeqnoMap& m, gcache::Page& p, pthread_t t, bool d)
        :
        seqno_map_(m),
        page_     (p),
        previous_thread_(t),
        debug_    (d)
    {}
    ~delete_thread_arg() { delete &page_; }
};

static void*
discard_page(void* __restrict__ a)
{
    delete_thread_arg* arg(static_cast<delete_thread_arg*>(a));

    auto& page(arg->page_);

#ifndef NDEBUG
    if (arg->debug_) { log_info << "PageStore::discard_page() prev. thread: "
                                << arg->previous_thread_ << ", page: "
                                << page; }
#endif

    if (arg->previous_thread_ != pthread_t(-1))
        pthread_join(arg->previous_thread_, NULL);

    if (page.seqno_max() > 0)
        arg->seqno_map_.seqno_discard(page.seqno_max());

    std::string const file_name(page.name());

    delete arg;

    remove_file(file_name);

    pthread_exit(NULL);
}

/* This method does minimum work while holding global lock and then
 * delegates seqno2ptr map cleanup to a dedicated thread. If there is a
 * previously launched thread it will be joined by the new one. */
bool
gcache::PageStore::delete_page ()
{
    Page* const page = pages_.front();

#ifndef NDEBUG
    if (debug_) { log_info << "PageStore::delete_page() " << *page; }
#endif

    if (page->used() > 0 || page->seqno_max() >= seqno_locked_)
        return false;
    pages_.pop_front();
    total_size_ -= page->size();
    if (current_ == page) current_ = 0;

    /* While we are still holding global lock close the page and up the
     * low available limit to the max seqno contained in a page */
    seqno_map_.set_low_limit(page->seqno_max());
    page->close();

    /* if there is currently another thread running it will be joined in
     * this new thread */
    pthread_t const saved(delete_thr_);
    int err = pthread_create(&delete_thr_, &delete_page_attr_, discard_page,
                             new delete_thread_arg(seqno_map_, *page,
                                                   delete_thr_, debug_));
    if (0 != err)
    {
        delete_thr_ = saved;
        gu_throw_system_error(err) << "Failed to create page deletion thread";
    }

    return true;
}

/* Deleting pages only from the beginning kinda means that some free pages
 * can be locked in the middle for a while. Leaving it like that for
 * simplicity for now. */
void
gcache::PageStore::cleanup ()
{
    while (page_cleanup_needed() && delete_page()) {}
}

void
gcache::PageStore::wait_page_discard() const
{
    if (delete_thr_ != pthread_t(-1))
    {
        pthread_join(delete_thr_, NULL);
        delete_thr_ = pthread_t(-1);
    }
}

void
gcache::PageStore::reset ()
{
    while (pages_.size() > 0 && delete_page()) {};
}

void
gcache::PageStore::set_enc_key (const Page::EncKey& new_key)
{
    /* on key change create new page (saves current key there) */
    if (debug_)
    {
        log_info << "GCache: encryption key rotated, size: " << new_key.size();
    }
    new_page(0, new_key);
    enc_key_ = new_key;
}

inline void
gcache::PageStore::new_page (size_type const size, const Page::EncKey& new_key)
{
    size_type const key_buf_size(BH_size(enc_key_.size()));
    size_type const meta_size(Page::meta_size(key_buf_size));
    size_type const min_size(meta_size + Page::aligned_size(size));

    Page* const page(new Page(this,
                              make_page_name(base_name_, count_),
                              new_key,
                              nonce_,
                              page_size_ > min_size ? page_size_ : min_size,
                              debug_));
    pages_.push_back (page);
    total_size_ += page->size();
    current_ = page;
    count_++;
    nonce_ += page->size(); /* advance nonce for the next page */

    /* allocate, write and release key buffer */

    void* const kp(current_->malloc(key_buf_size));// buffer in page (ciphertext)
    assert(kp);

    size_type const key_alloc_size(Page::aligned_size(key_buf_size));
    assert(key_alloc_size >= sizeof(BufferHeader) + enc_key_.size());

    /* choose whether to operate on a tmp plaintext buffer or directly on page */
    BufferHeader* const bh
        (BH_cast(encrypt_cb_ ? ::operator new(key_alloc_size) : kp));

    BH_clear(bh);
    bh->size    = key_buf_size;
    bh->seqno_g = SEQNO_NONE;
    bh->ctx     = reinterpret_cast<BH_ctx_t>(current_);
    bh->flags   = 0;
    bh->store   = BUFFER_IN_PAGE;
    BH_release(bh);

    if (enc_key_.size() != 0) ::memcpy(bh + 1, enc_key_.data(), enc_key_.size());

    if (encrypt_cb_)
    {
        current_->xcrypt(encrypt_cb_, app_ctx_, bh, kp, key_alloc_size,
                         WSREP_ENC);
    }
    else
    {
        /* nothing to do, data written directly to page */
    }

    current_->free(bh); /* we won't need the buffer until recovery */

    if (encrypt_cb_) ::operator delete(bh);
}

gcache::PageStore::PageStore (SeqnoMap&                seqno_map,
                              const std::string&       dir_name,
//                              const std::string&       prefix,
                              wsrep_encrypt_cb_t const encrypt_cb,
                              void*              const app_ctx,
                              size_t             const keep_size,
                              size_t             const page_size,
                              size_t             const keep_plaintext_size,
                              int                const dbg,
                              bool               const keep_page)
    :
    seqno_map_ (seqno_map),
    base_name_ (make_base_name(dir_name)),
    encrypt_cb_(encrypt_cb),
    app_ctx_   (app_ctx),
    enc_key_   (),
    nonce_     (),
    seqno_locked_(SEQNO_MAX),
    keep_size_ (keep_size),
    page_size_ (page_size),
    keep_plaintext_size_ (keep_plaintext_size),
    count_     (0),
    pages_     (),
    current_   (0),
    total_size_(0),
    enc2plain_ (),
    plaintext_size_(0),
    delete_page_attr_(),
    delete_thr_(pthread_t(-1)),
    debug_     (dbg & DEBUG),
    keep_page_ (keep_page)
{
    int err = pthread_attr_init (&delete_page_attr_);

    if (0 != err)
    {
        gu_throw_system_error(err) << "Failed to initialize page file "
            "deletion thread attributes";
    }
}

void
gcache::PageStore::Plain::print(std::ostream& os) const
{
    os << "Page: "      << page_
       << ", ptx: "     << static_cast<void*>(ptx_)
       << ", BH: "      << &bh_
       << ", alloc'd: " << alloc_size_
       << ", refs: "    << ref_count_
       << ", changed: " << (changed_ ? 'Y' : 'N')
       << ", freed: "   << (freed_ ? 'Y' : 'N')
        ;
}

gcache::PageStore::~PageStore ()
{
    if (enc2plain_.size() > 0)
    {
        int unflushed(0);
        int unfreed(0);
        for (PlainMap::iterator i(enc2plain_.begin()); i != enc2plain_.end();
             ++i)
        {
            unflushed += i->second.changed_;
            unfreed   += i->second.ptx_ != NULL;
        }

        if (unflushed > 0)
        {
            log_error << "Unflushed plaintext buffers: " << unflushed << '/'
                      << enc2plain_.size();
            if (debug_)
            {
                for (PlainMap::iterator i(enc2plain_.begin());
                     i != enc2plain_.end(); ++i)
                {
                    if (i->second.changed_) { log_error << i->second; }
                }
            }
        }

        if (unfreed > 0)
        {
            log_error << "Unfreed plaintext buffers: " << unfreed << '/'
                      << enc2plain_.size();
            if (debug_)
            {
                for (PlainMap::iterator i(enc2plain_.begin());
                     i != enc2plain_.end(); ++i)
                {
                    if (i->second.ptx_ != NULL) { log_error << i->second; }
                }
            }
        }

        assert(!(unflushed || unfreed));
    }

    try
    {
        while (pages_.size() && delete_page()) {};

        if (delete_thr_ != pthread_t(-1)) pthread_join (delete_thr_, NULL);
    }
    catch (gu::Exception& e)
    {
        log_error << e.what() << " in ~PageStore()"; // abort() ?
    }

    if (page_cleanup_needed())
    {
        log_info << "Could not delete " << pages_.size()
                 << " page files: some buffers are still \"mmapped\".";
        if (debug_)
            for (PageQueue::iterator i(pages_.begin()); i != pages_.end(); ++i)
            {
                log_info << *(*i);
            }
    }
    else if (debug_ && pages_.size() > 0 )
    {
        log_info << "Pages to stay: ";
        for (PageQueue::iterator i(pages_.begin()); i != pages_.end(); ++i)
        {
            log_info << *(*i);
        }
    }

    for (PageQueue::iterator i(pages_.begin()); i != pages_.end(); ++i)
    {
        delete *i;
    }
    pages_.clear();

    pthread_attr_destroy (&delete_page_attr_);
}

inline void*
gcache::PageStore::malloc_new (size_type const size)
{
    Limits::assert_size(size);

    void* ret(NULL);

    try
    {
        new_page(size, enc_key_);
        ret = current_->malloc (size);
        cleanup();
    }
    catch (gu::Exception& e)
    {
        log_error << "Cannot create new cache page: " << e.what();
    }
    assert(ret);
    return ret;
}

void*
gcache::PageStore::malloc (size_type const size, void*& ptx)
{
    Limits::assert_size(size);

    void* ptr(NULL);
    if (gu_likely(NULL != current_)) ptr = current_->malloc(size);
    if (gu_unlikely(NULL == ptr)) ptr = malloc_new(size);

    BufferHeader* bh(NULL);
    void* ret(NULL);

    if (gu_likely(NULL != ptr))
    {
        size_type alloc_size(0);
        if (encrypt_cb_) /* allocate corresponding plaintext buffer */
        {
            alloc_size = Page::aligned_size(size);
            bh = BH_cast(::operator new(alloc_size));
        }
        else             /* use mmapped buffer directly */
        {
            bh = BH_cast(ptr);
        }

        bh->size    = size;
        bh->seqno_g = SEQNO_NONE;
        bh->ctx     = reinterpret_cast<BH_ctx_t>(current_);
        bh->flags   = 0;
        bh->store   = BUFFER_IN_PAGE;

        ptx = bh + 1;           /* this points to plaintext buf */
        ret = BH_cast(ptr) + 1; /* points to mmapped payload */

        if (encrypt_cb_)
        {
            assert(alloc_size > 0);
            Plain plain = {
                current_,    // page_
                bh,          // ptx_
                *bh,         // bh_
                alloc_size,  // alloc_size_
                1,           // ref_count_
                true,        // changed_ (malloc() intention is writing)
                false        // freed_
            };

            if (gu_unlikely(!enc2plain_.insert(PlainMapEntry(ret,plain)).second))
            {
                delete bh;
                gu_throw_fatal << "Failed to insert plaintext ctx. Map size: "
                               << enc2plain_.size();
            }

            plaintext_size_ += alloc_size;
        }
    }
    else ptx = NULL;

    return ret;
}

void*
gcache::PageStore::realloc (void* ptr, size_type const size)
{
    Limits::assert_size(size);

    if (encrypt_cb_)
    {
       assert(0); // should not be called when encryption is on
       return NULL;
    }
    /*!
     * @note FFR: One of the reasons in-place realloc is not supported when
     * encryption is enabled is the need to realloc plaintext buffer as well
     * which adds too much complexity for a functionality which is not even
     * being used ATM.
     */

    assert(ptr != NULL);

    BufferHeader* const bh(ptr2BH(ptr));
    assert(SEQNO_NONE == bh->seqno_g);
    assert(BUFFER_IN_PAGE == bh->store);

    size_type const old_size(Page::aligned_size(bh->size));
    size_type const new_size(Page::aligned_size(size));
    Page*     const page(reinterpret_cast<Page*>(bh->ctx));

    /* we can do in-place realloc (whether it is shrinking or growing)
     * only if this is the last allocated buffer in the page */
    if (old_size == new_size ||
        page->realloc(bh, old_size, new_size))
    {
        bh->size = size;
        return ptr;
    }

    return NULL; // fallback to malloc()/memcpy()/free()
}

gcache::PageStore::PlainMap::iterator
gcache::PageStore::find_plaintext(const void* const ptr)
{
    assert(encrypt_cb_); // must be called only if encryption callback is set

    PlainMap::iterator i(enc2plain_.find(ptr));
    if (enc2plain_.end() == i)
    {
        assert(0); // this sohuld not happen unless ptr was discarded
        gu_throw_fatal << "Internal program error: plaintext context not found.";
    }
    return i;
}

void*
gcache::PageStore::get_plaintext(const void* ptr, bool const writable)
{
    assert(encrypt_cb_); // must be called only if encryption callback is set

    PlainMap::iterator const i(find_plaintext(ptr));
    assert(i->first == ptr);

    Plain& p(i->second);
    assert(p.page_);
    assert(!writable || !p.freed_); // should not change freed buffer

    if (NULL == p.ptx_)
    {
        /* plaintext was flushed to page, reread it back */
        assert(false == p.changed_);
        p.ptx_ = BH_cast(::operator new(p.alloc_size_));
        plaintext_size_ += p.alloc_size_;
        p.page_->xcrypt(encrypt_cb_, app_ctx_, ptr2BH(ptr), p.ptx_,p.alloc_size_,
                        WSREP_DEC);

        // make sure buffer headers agree
        assert(p.ptx_->seqno_g == p.bh_.seqno_g);
        assert(p.ptx_->ctx     == p.bh_.ctx);
        assert(p.ptx_->size    == p.bh_.size);
        assert(p.ptx_->store   == p.bh_.store);
        assert(p.ptx_->type    == p.bh_.type);

        // mask released flag since it can differ after repossession
        assert((p.ptx_->flags|BUFFER_RELEASED) == (p.bh_.flags|BUFFER_RELEASED));
    }

    p.changed_ = p.changed_ || writable;
    p.ref_count_++;

    return p.ptx_ + 1;
}

void
gcache::PageStore::drop_plaintext(PlainMap::iterator const i,
                                  const void*        const ptr,
                                  bool               const free)
{
    assert(i->first == ptr);

    Plain& p(i->second);
    assert(p.page_);

    if (p.ref_count_ > 0)
    {
        assert(p.ptx_);
        p.ref_count_--;
    }
    else
    {
        /* allow freeing of unreferenced buffers to avoid unnecessary lookups
         * and potential decryption overhead */
        assert(free);
    }

    assert(false == p.freed_ || false == free); /* can free only once */
    p.freed_ = p.freed_ || free;

    /* Do anything only if there's too much plaintext or it was already freed,
     * otherwise free() should take care of it. */
    if (p.ref_count_ == 0 &&
        (plaintext_size_ > keep_plaintext_size_ || p.freed_))
    {
        if (p.changed_)
        {
            assert(p.ptx_);

            /* update buffer header in ptx_ */
            *p.ptx_ = p.bh_;

            /* flush to page before freeing */
            p.page_->xcrypt(encrypt_cb_, app_ctx_, p.ptx_, ptr2BH(ptr),
                            p.alloc_size_, WSREP_ENC);
            p.changed_ = false;
        }

        delete p.ptx_;
        p.ptx_ = NULL;
        plaintext_size_ -= p.alloc_size_;
    }
}

void
gcache::PageStore::repossess(BufferHeader* bh, const void* ptr)
{
    assert(BH_is_released(bh)); // will be changed by the caller

    Page* page;

    if (encrypt_cb_)
    {
        Plain& p(bh2Plain(bh));
        assert(p.freed_);

        p.freed_ = false;
        /* don't increment reference counter or decrypt ciphertext - this method
           is not to acquire resource, it is to reverse the effects of free() */

        page = p.page_;
    }
    else
    {
        page = reinterpret_cast<Page*>(bh->ctx);
    }

    page->repossess(bh, ptr);
}

void
gcache::PageStore::set_debug(int const dbg)
{
    debug_ = dbg & DEBUG;

    for (PageQueue::iterator i(pages_.begin()); i != pages_.end(); ++i)
    {
        (*i)->set_debug(debug_);
    }
}
