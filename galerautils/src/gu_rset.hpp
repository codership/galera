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
#include "gu_throw.hpp"
#include "gu_logger.hpp"

#include <string>

namespace gu {

class RecordSet
{
public:

    enum Version
    {
        VER0 = 0
    };

    static Version const MAX_VER = VER0;

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
    Hash    check_;

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
                      Version            version  = MAX_VER,
                      ssize_t            max_size = 0x7fffffff);

    /*! return total size of a RecordSet */
#if 0 // this has code duplication
    template <class R>
    ssize_t append_base (const R& record)
    {
        ssize_t const size(record.serial_size());

        if (gu_unlikely(size > max_size_ - size_)) gu_throw_error(EMSGSIZE);

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

        if (gu_unlikely(size > max_size_ - size_)) gu_throw_error(EMSGSIZE);

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

        if (gu_unlikely(size > max_size_ - size_)) gu_throw_error(EMSGSIZE);

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

    ssize_t const    max_size_;
    Allocator        alloc_;
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


template <class R>
class RecordSetOut : public RecordSetOutBase
{
public:

    RecordSetOut (const std::string& base_name,
                  CheckType          ct,
                  Version            version  = MAX_VER,
                  ssize_t            max_size = 0x7fffffff)
        : RecordSetOutBase (base_name, ct, version, max_size) {}

    size_t append (const R& r)
    {
        return append_base<R, false> (r);
//        return append_base<R> (r);
    }

    ssize_t append (const void* const src, ssize_t const size,
                    bool const store = true, bool const new_record = true)
    {
        assert (src);
        assert (size);

        BufWrap bw (src, size);
        return append_base<BufWrap, true> (bw, store, new_record);
//        return append_base (src, size, store);
    }

private:

    /* a wrapper class to represent ptr and size as a serializable object:
     * simply defines serial_size(), ptr() and serialize_to() methods */
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
class RecordSetIn : public RecordSet
{
public:

    RecordSetIn (const byte_t* buf,   /* pointer to the beginning of buffer */
                 size_t        size,  /* total size of buffer */
                 bool          check_first = true); /* checksum now */

    void rewind() const { next_ = begin_; }

    template <class R>
    void next(Buf& n) const
    {
        n.ptr  = next_;
        n.size = R::serial_size(n.ptr, size_ - next_);
        next_ += n.size;
    }

    template <class R>
    R next() const
    {
        R rec(head_ + next_, size_ - next_);
        next_ += rec.serial_size();
        return rec;
    }

private:

    const byte_t* const head_;        /* pointer to header        */
    bool const          check_first_; /* checksum on construction */
    ssize_t             begin_;       /* offset to first record   */
    ssize_t mutable     next_;        /* offset to next record    */
    /* size_ is offset past all records */

    /* takes total size of the supplied buffer */
    void parse_header_v0 (size_t size);

    /* shallow copies here - we're not allocating anything */
    RecordSetIn (const RecordSetIn& r)
    :
    RecordSet   (r),
    head_       (r.head_),
    check_first_(r.check_first_),
    begin_      (r.begin_),
    next_       (r.next_)
    {}

    RecordSetIn& operator= (const RecordSetIn r);
#if 0
    {
        std::swap(head_,        r.head_);
        std::swap(check_first_, r.check_first_);
        std::swap(begin,        r.begin_);
        std::swap(next_,        r.next_);
    }
#endif

};

} /* namespace gu */

#endif /* _GU_RSET_HPP_ */
