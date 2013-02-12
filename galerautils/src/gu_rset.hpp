/* Copyright (C) 2013 Codership Oy <info@codership.com> */
/*!
 * @file common RecordSet interface
 *
 * Record set is a collection of serialized records of the same type.
 *
 * It stores them in an iovec-like collection of buffers before sending
 * and restores from a single buffer when receiving.
 *
 * $Id$
 */

#ifndef _GU_RSET_HPP_
#define _GU_RSET_HPP_

#include "gu_alloc.hpp"
#include "gu_digest.hpp"

#ifdef GU_RSET_CHECK_SIZE
#  include "gu_throw.hpp"
#endif

#include <string>

namespace gu {

class RecordSet
{
public:

    enum Version
    {
        EMPTY = 0,
        VER1
    };

    static Version const MAX_VER = VER1;

    enum CheckType
    {
        CHECK_NONE   = 0,
        CHECK_MMH32,
        CHECK_MMH64,
        CHECK_MMH128
    };

    /*! return total size of a RecordSet */
    size_t size() const  { return size_; }

    /*! return number of records in the record set */
    int    count() const { return count_; }

protected:

    Version const   version_;
    CheckType const check_type_;

    ssize_t size_;
    ssize_t count_;

    /* ctor for RecordSetOut */
    RecordSet (Version const version, CheckType const ct);

    /* ctor for RecordSetIn */
    RecordSet (const byte_t* buf, ssize_t size);

    virtual ~RecordSet() {}
};

/*! class to store records in buffer(s) to send out */
class RecordSetOutBase : public RecordSet
{
public:

    /*! return number of disjoint pages in the record set */
    ssize_t page_count() const { return bufs_.size(); }

    /*! return vector of RecordSet fragments in adjucent order */
    ssize_t gather (std::vector<Buf>& out);

protected:

    RecordSetOutBase (const std::string& base_name,     /* basename for on-disk
                                                         * allocator */
                      CheckType          ct,
                      Version            version  = MAX_VER
#ifdef GU_RSET_CHECK_SIZE
                      ,ssize_t            max_size = 0x7fffffff
#endif
        );

    /*! return total size of a RecordSet */
#if 0 // this has code duplication
    template <class R>
    ssize_t append_base (const R& record)
    {
        ssize_t const size(record.serial_size());

#ifdef GU_RSET_CHECK_SIZE
        if (gu_unlikely(size > max_size_ - size_)) gu_throw_error(EMSGSIZE);
#endif

        bool new_page;

        byte_t* const ptr(alloc_.alloc (size, new_page));

        new_page = (new_page || !prev_stored_);

        ssize_t const ssize (record.serialize_to (ptr, size));

        assert (ssize == size);

        prev_stored_ = true;
        count++;

        return post_append (new_page, ptr, ssize);
    }

    ssize_t append_base (const void* const src, ssize_t const size,
                         bool const store = true, bool const new_record = true)
    {
        assert (src);
        assert (size);

#ifdef GU_RSET_CHECK_SIZE
        if (gu_unlikely(size > max_size_ - size_)) gu_throw_error(EMSGSIZE);
#endif

        bool new_page(!store);
        const byte_t* ptr(reinterpret_cast<const byte_t*>(src));

        if (store)
        {
            ptr = alloc_.alloc (size, new_page);
            assert (0 != ptr); // alloc should throw

            new_page = (new_page || !prev_stored_);

            ::memcpy (const_cast<byte_t*>(ptr), src, size);
        }

        prev_stored_ = store;
        count_ += new_record;

        return post_append (new_page, ptr, size);
    }

#else // OLD - the following has no code duplication

    /* this is to emulate partial specialization of function template through
     * overloading by parameter */
    template <bool store> struct HasPtr{};

    /* variant for classes that don't provide ptr() method and need to be
     * explicitly serialized to internal storage */
    template <class R>
    void process (const R&       record,
                  const byte_t*& ptr,
                  bool&          new_page,
                  size_t const   size,
                  bool,
                  HasPtr<false>)
    {
        byte_t* const dst(alloc_.alloc (size, new_page));

        new_page = (new_page || !prev_stored_);
        ptr = dst;

#ifdef NDEBUG
        record.serialize_to (dst, size);
#else
        size_t const ssize (record.serialize_to (dst, size));
        assert (ssize == size);
#endif
    }

    /* variant for classes that have ptr() method and can be either serialized
     * or referenced */
    template <class R>
    void process (const R&       record,
                  const byte_t*& ptr,
                  bool&          new_page,
                  size_t const   size,
                  bool const     store,
                  HasPtr<true>)
    {
        if (store)
        {
            process (record, ptr, new_page, size, true, HasPtr<false>());
        }
        else
        {
            ptr = record.ptr();
            new_page = true;
        }
    }

    template <class R, bool has_ptr>
    size_t append_base (const R& record,
                        bool const store = true,
                        bool const new_record = true)
    {
        ssize_t const size (record.serial_size());

#ifdef GU_RSET_CHECK_SIZE
        if (gu_unlikely(size > max_size_ - size_)) gu_throw_error(EMSGSIZE);
#endif

        bool new_page;
        const byte_t* ptr;

        process (record, ptr, new_page, size, store, HasPtr<has_ptr>());

        prev_stored_ = store;
        count_ += new_record;

        post_append (new_page, ptr, size);

        return size;
    }

#endif /* OLD */

private:

#ifdef GU_RSET_CHECK_SIZE
    ssize_t const    max_size_;
#endif

    Allocator        alloc_;
    Hash             check_;
    std::vector<Buf> bufs_;
    bool             prev_stored_;

    void
    post_alloc (bool const new_page, const byte_t* const ptr,
                ssize_t const size);

    void
    post_append (bool const new_page, const byte_t* const ptr,
                 ssize_t const size);

    int header_size     () const;
    int header_size_max () const;

    /* Writes the header to the end of provided buffer, returns header
     * offset from ptr */
    ssize_t write_header (byte_t* ptr, ssize_t size);
};


/*! This is a small wrapper template for RecordSetOutBase to avoid templating
 *  the whole thing instead of just the two append methods. */
template <class R>
class RecordSetOut : public RecordSetOutBase
{
public:

    RecordSetOut (const std::string& base_name,
                  CheckType          ct,
                  Version            version  = MAX_VER
#ifdef GU_RSET_CHECK_SIZE
                  ,ssize_t           max_size = 0x7fffffff
#endif
        )
        : RecordSetOutBase (base_name, ct, version
#ifdef GU_RSET_CHECK_SIZE
                            ,max_size
#endif
            ) {}

    size_t append (const R& r)
    {
        return append_base<R, false> (r);
//        return append_base<R> (r); old append_base() method
    }

    ssize_t append (const void* const src, ssize_t const size,
                    bool const store = true, bool const new_record = true)
    {
        assert (src);
        assert (size);

        BufWrap bw (src, size);
        return append_base<BufWrap, true> (bw, store, new_record);
//        return append_base (src, size, store); - old append_base() method
    }

private:

    /*! a wrapper class to represent ptr and size as a serializable object:
     *  simply defines serial_size(), ptr() and serialize_to() methods */
    class BufWrap
    {
        const byte_t* const ptr_;
        size_t const        size_;

    public:

        BufWrap (const void* const ptr, size_t const size)
            : ptr_(reinterpret_cast<const byte_t*>(ptr)), size_(size)
        {}

        size_t serial_size() const { return size_; }
        const byte_t* ptr()  const { return ptr_; }

        size_t serialize_to (byte_t* const dst, size_t) const
        {
            ::memcpy (dst, ptr_, size_);
            return size_;
        }
    };

    RecordSetOut (const RecordSetOut&);
    RecordSetOut& operator = (const RecordSetOut&);
};


/*! class to recover records from a buffer */
class RecordSetInBase : public RecordSet
{
public:

    RecordSetInBase (const byte_t* buf,/* pointer to the beginning of buffer */
                     size_t        size,             /* total size of buffer */
                     bool          check_first = true);      /* checksum now */

    void rewind() const { next_ = begin_; }

    bool checksum() const;

protected:

    template <class R>
    void next_base (Buf& n) const
    {
        if (gu_likely (next_ < size_))
        {
            size_t const tmp_size(R::serial_size(n.ptr, size_ - next_));

            /* sanity check */
            if (gu_likely (next_ + tmp_size <= size_t(size_)))
            {
                n.ptr  = next_ + next_;
                n.size = tmp_size;
                next_ += tmp_size;
                return;
            }

            throw_error (E_FAULT);
        }

        assert (next_ == size_);

        throw_error (E_PERM);
    }

    template <class R>
    R next_base () const
    {
        if (gu_likely (next_ < size_))
        {
            R const      rec(head_ + next_, size_ - next_);
            size_t const tmp_size(rec.serial_size());

            /* sanity check */
            if (gu_likely (next_ + tmp_size <= size_t(size_)))
            {
                next_ += tmp_size;
                return rec;
            }

            throw_error (E_FAULT);
        }

        assert (next_ == size_);

        throw_error (E_PERM);
    }

private:

    const byte_t* const head_;        /* pointer to header        */
    ssize_t             begin_;       /* offset to first record   */
    ssize_t mutable     next_;        /* offset to next record    */
    /* size_ is offset past all records */

    /* takes total size of the supplied buffer */
    void parse_header_v1 (size_t size);

    enum Error
    {
        E_PERM,
        E_FAULT
    };

    GU_NORETURN void throw_error (Error code) const;

    /* shallow copies here - we're not allocating anything */
    RecordSetInBase (const RecordSetInBase& r)
    :
    RecordSet   (r),
    head_       (r.head_),
    begin_      (r.begin_),
    next_       (r.next_)
    {}

    RecordSetInBase& operator= (const RecordSetInBase r);
#if 0
    {
        std::swap(head_,        r.head_);
        std::swap(begin,        r.begin_);
        std::swap(next_,        r.next_);
    }
#endif
}; /* class RecordSetInBase */


/*! This is a small wrapper template for RecordSetInBase to avoid templating
 *  the whole thing instead of just the two next methods. */
template <class R>
class RecordSetIn : public RecordSetInBase
{
public:

    RecordSetIn (const byte_t* buf,/* pointer to the beginning of buffer */
                 size_t        size,             /* total size of buffer */
                 bool          check_first = true)       /* checksum now */
        : RecordSetInBase (buf, size, check_first)
    {}

    void next (Buf& n) const { next_base<R> (n); }

    R next () const { return next_base<R> (); }

}; /* class RecordSetIn */

} /* namespace gu */

#endif /* _GU_RSET_HPP_ */
