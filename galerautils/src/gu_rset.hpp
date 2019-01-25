/* Copyright (C) 2013-2019 Codership Oy <info@codership.com> */
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

#include "gu_vector.hpp"
#include "gu_alloc.hpp"
#include "gu_digest.hpp"

#include "gu_limits.h" // GU_MIN_ALIGNMENT

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
        VER1,
        VER2
    };

    static Version const MAX_VERSION    = VER2;
    static int     const VER2_ALIGNMENT = GU_MIN_ALIGNMENT;

    enum CheckType
    {
        CHECK_NONE   = 0,
        CHECK_MMH32,
        CHECK_MMH64,
        CHECK_MMH128
    };

    static int check_size(CheckType ct);

    /*! return net, payload size of a RecordSet */
    size_t size() const  { return size_; }

    /*! return total, padded size of a RecordSet */
    size_t serial_size() const  { return GU_ALIGN(size_, alignment_); }

    /*! return number of records in the record set */
    int    count() const { return count_; }

    Version   version()    const { return Version(version_); }
    CheckType check_type() const { return CheckType(check_type_); }

    /*! return alignment of the records */
    int       alignment()  const { return alignment_; };

    typedef gu::Vector<gu::Buf, 16> GatherVector;

protected:

    ssize_t   size_;
    int       count_;

private:

    byte_t    version_;
    byte_t    check_type_;
    byte_t    alignment_;

protected:
    /* ctor for RecordSetOut */
    RecordSet (Version const version, CheckType const ct);

    /* ctor for RecordSetIn */
    RecordSet ()
        : size_      (0),
          count_     (0),
          version_   (EMPTY),
          check_type_(CHECK_NONE),
          alignment_ (Version(0))
    {}

    void init (const byte_t* buf, ssize_t size);

    ~RecordSet() {}
};

/*! specialization of Vector::serialize() method */
template<> inline RecordSet::GatherVector::size_type
RecordSet::GatherVector::serialize(void*     const buf,
                                   size_type const buf_size,
                                   size_type const offset /* = 0 */)
{
    byte_t*       to (static_cast<byte_t*>(buf) + offset);
    byte_t* const end(static_cast<byte_t*>(buf) + buf_size);
    for (size_type i(0); i < size(); ++i)
    {
        const gu::Buf& f((*this)[i]);
        if (to + f.size > end)
        {
            gu_throw_fatal << "attempt to write beyond buffer boundary";
        }
        const gu::byte_t* from(static_cast<const gu::byte_t*>(f.ptr));
        to = std::copy(from, from + f.size, to);
    }
    return to - static_cast<byte_t*>(buf);
}


#if defined(__GNUG__)
# if (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || (__GNUC__ > 4)
#  pragma GCC diagnostic push
# endif // (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || (__GNUC__ > 4)
# pragma GCC diagnostic ignored "-Weffc++"
#endif

/*! class to store records in buffer(s) to send out */
class RecordSetOutBase : public RecordSet
{
public:

    typedef Allocator::BaseName BaseName;

    /*! return number of disjoint pages in the record set */
    ssize_t page_count() const { return bufs_->size() + padding_page_needed(); }

    /*! return vector of RecordSet fragments in adjusent order */
    ssize_t gather (GatherVector& out);

protected:

    RecordSetOutBase() : RecordSet() {}

    RecordSetOutBase (byte_t*           reserved,
                      size_t            reserved_size,
                      const BaseName&   base_name,     /* basename for on-disk
                                                        * allocator */
                      CheckType         ct,
                      Version           version  = MAX_VERSION
#ifdef GU_RSET_CHECK_SIZE
                      ,ssize_t          max_size = 0x7fffffff
#endif
        );

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
        byte_t* const dst(alloc(size, new_page));

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
    std::pair<const byte_t*, size_t>
    append_base (const R& record,
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
        // make sure there is at least one record
        count_ += new_record || (0 == count_);

        post_append (new_page, ptr, size);

        size_ += size;

        return std::pair<const byte_t*, size_t>(ptr, size);
    }

private:

#ifdef GU_RSET_CHECK_SIZE
    ssize_t const max_size_;
#endif
    Allocator     alloc_;
    Hash          check_;
    Vector<Buf, Allocator::INITIAL_VECTOR_SIZE> bufs_;
    bool          prev_stored_;

    inline bool padding_page_needed() const
    {
        return (size_ % alignment());
    }

    inline byte_t*
    alloc(size_t const size, bool& new_page)
    {
        byte_t* const ret(alloc_.alloc (size, new_page));
        new_page = (new_page || !prev_stored_);
        return ret;
    }

    inline void
    post_alloc (bool const          new_page,
                const byte_t* const ptr,
                ssize_t const       size)
    {
        if (new_page)
        {
            Buf b = { ptr, size };
            bufs_->push_back (b);
        }
        else
        {
            bufs_->back().size += size;
        }
    }

    inline void
    post_append (bool const          new_page,
                 const byte_t* const ptr,
                 ssize_t const       size)
    {
        check_.append (ptr, size);
        post_alloc (new_page, ptr, size);
    }


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

    typedef RecordSetOutBase::BaseName BaseName;

    RecordSetOut() : RecordSetOutBase() {}

    RecordSetOut (byte_t*             reserved,
                  size_t              reserved_size,
                  const BaseName&     base_name,
                  CheckType           ct,
                  Version             version  = MAX_VERSION
#ifdef GU_RSET_CHECK_SIZE
                  ,ssize_t            max_size = 0x7fffffff
#endif
        )
        : RecordSetOutBase (reserved, reserved_size, base_name, ct, version
#ifdef GU_RSET_CHECK_SIZE
                            ,max_size
#endif
            )
    {}

    std::pair<const byte_t*, size_t>
    append (const R& r)
    {
        return append_base<R, false> (r);
//        return append_base<R> (r); old append_base() method
    }

    std::pair<const byte_t*, size_t>
    append (const void* const src, ssize_t const size,
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
        const byte_t* ptr()  const { return ptr_;  }

        size_t serialize_to (byte_t* const dst, size_t) const
        {
            ::memcpy (dst, ptr_, size_);
            return size_;
        }
    };

    RecordSetOut (const RecordSetOut&);
    RecordSetOut& operator = (const RecordSetOut&);

}; /* class RecordSetOut */


/*! class to recover records from a buffer */
class RecordSetInBase : public RecordSet
{
public:

    RecordSetInBase (const byte_t* buf,/* pointer to the beginning of buffer */
                     size_t        size,             /* total size of buffer */
                     bool          check_now = true);        /* checksum now */

    /* this is a "delayed constructor", for the object created empty */
    void init (const byte_t* buf,      /* pointer to the beginning of buffer */
               size_t        size,                   /* total size of buffer */
               bool          check_now = true);              /* checksum now */


    void rewind() const { next_ = begin_; }

    void checksum() const; // throws if checksum fails

    uint64_t get_checksum() const;

    gu::Buf buf() const
    {
        gu::Buf ret = { head_, ssize_t(serial_size()) }; return ret;
    }

protected:

    template <class R>
    void next_base (Buf& n) const
    {
        if (gu_likely (next_ < size_))
        {
            size_t const next_size(R::serial_size(head_ + next_, size_ -next_));

            /* sanity check */
            if (gu_likely (next_ + next_size <= size_t(size_)))
            {
                n.ptr  = head_ + next_;
                n.size = next_size;
                next_ += next_size;
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

    const byte_t*   head_;        /* pointer to header        */
    ssize_t mutable next_;        /* offset to next record    */
    short           begin_;       /* offset to first record   */
    /* size_ from parent class is offset past all records */

    /* takes total size of the supplied buffer */
    void parse_header_v1_2 (size_t size);

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
    next_       (r.next_),
    begin_      (r.begin_)
    {}

    RecordSetInBase& operator= (const RecordSetInBase r);
#if 0
    {
        std::swap(head_, r.head_);
        std::swap(next_, r.next_);
        std::swap(begin, r.begin_);
    }
#endif
}; /* class RecordSetInBase */


/*! This is a small wrapper template for RecordSetInBase to avoid templating
 *  the whole thing instead of just the two next methods. */
template <class R>
class RecordSetIn : public RecordSetInBase
{
public:

    RecordSetIn (const void* buf,/* pointer to the beginning of buffer */
                 size_t      size,             /* total size of buffer */
                 bool        check_first = true)       /* checksum now */
        :
        RecordSetInBase (reinterpret_cast<const byte_t*>(buf),
                         size, check_first)
    {}

    RecordSetIn () : RecordSetInBase (NULL, 0, false) {}

    void next (Buf& n) const { next_base<R> (n); }

    R next () const { return next_base<R> (); }
}; /* class RecordSetIn */

#if defined(__GNUG__)
# if (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || (__GNUC__ > 4)
#  pragma GCC diagnostic pop
# endif // (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || (__GNUC__ > 4)
#endif

} /* namespace gu */

#endif /* _GU_RSET_HPP_ */
