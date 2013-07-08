//
// Copyright (C) 2013 Codership Oy <info@codership.com>
//


#ifndef GALERA_KEY_SET_HPP
#define GALERA_KEY_SET_HPP

#include "gu_rset.hpp"
#include "gu_unordered.hpp"
#include "gu_logger.hpp"
#include "gu_hexdump.hpp"
#include "key_data.hpp"


namespace galera
{

class KeySet
{
public:

    enum Version
    {
        EMPTY = 0,
        FLAT16,    /* 16-byte hash (flat) */
        FLAT16A,   /* 16-byte hash (flat), annotated */
        FLAT8,     /*  8-byte hash (flat) */
        FLAT8A     /*  8-byte hash (flat), annotated */
//      TREE8      /*  8-byte hash + full serialized key */
    };

    static Version const MAX_VERSION = FLAT8A;

    static Version version (unsigned int ver)
    {
        if (gu_likely (ver <= MAX_VERSION)) return static_cast<Version>(ver);

        gu_throw_error (EINVAL) << "Unrecognized KeySet version: " << ver;
    }

    class Key
    {
    public:
        enum Prefix
        {
            P_SHARED = 0,
            P_EXCLUSIVE,
            P_LAST = P_EXCLUSIVE
        };
    }; /* class Key */

    /* This class describes what commonly would be referred to as a "key".
     * It is called KeyPart because it does not fully represent a multi-part
     * key, but only nth part out of N total.
     * To fully represent a 3-part key p1:p2:p3 one would need 3 such objects:
     * for parts p1, p1:p2, p1:p2:p3 */
    class KeyPart
    {
    public:

        static size_t const TMP_STORE_SIZE = 4096;
        static size_t const MAX_HASH_SIZE  = 16;

        struct TmpStore { gu::byte_t buf[TMP_STORE_SIZE]; };
        struct HashData { gu::byte_t buf[MAX_HASH_SIZE];  };

        /* This ctor creates a serialized representation of a key in tmp store
         * from a key hash and optional annotation. */
        KeyPart (TmpStore&       tmp,
                 const HashData& hash,
                 Version const   ver,
                 bool const      exclusive,
                 const wsrep_buf_t* parts, /* for annotation */
                 int const       part_num
            )
            : data_(tmp.buf)
        {
            assert(ver >= FLAT16 && ver <= FLAT8A);

            const uint64_t* from(reinterpret_cast<const uint64_t*>(hash.buf));
            uint64_t*       to  (reinterpret_cast<uint64_t*>(tmp.buf));

            /* copy first 8 bytes of hash */
            to[0] = from[0];

            size_t ann_off(8);

            if (static_cast<unsigned int>(ver - FLAT16) <= 1)
            {
                /* copy next 8 bytes of hash */
                to[1] = from[1];
                ann_off += 8;
            }

            /*  use lower bits for header:  */

            /* clear header bits */
            gu::byte_t b = tmp.buf[0] & (~HEADER_MASK);

            /* set prefix  */
            if (exclusive) { b |= (Key::P_EXCLUSIVE & PREFIX_MASK); }

            /* set version */
            b |= (ver & VERSION_MASK) << PREFIX_BITS;

            tmp.buf[0] = b;

            if (annotated(ver))
            {
                store_annotation (parts, part_num,
                                  tmp.buf + ann_off, sizeof(tmp.buf) - ann_off);
            }
        }

        /* This ctor uses pointer to a permanently stored serialized key part */
        KeyPart (const gu::byte_t* const buf, size_t const size)
            : data_(buf)
        {
            if (gu_likely(size >= 8 && serial_size() <= size)) return;

            gu_throw_error (EINVAL) << "Buffer too short: expected "
                                    << serial_size() << ", got " << size;
        }

        KeyPart () : data_(0) {}

        Key::Prefix prefix() const
        {
            gu::byte_t const p(data_[0] & PREFIX_MASK);

            if (gu_likely(p <= Key::P_LAST))
                return static_cast<Key::Prefix>(p);

            gu_throw_error(EPROTO) << "Unsupported key prefix: " << p;
        }

        bool shared()       const { return prefix() == Key::P_SHARED; }

        bool exclusive()    const { return prefix() == Key::P_EXCLUSIVE; }

        static Version version(const gu::byte_t* const buf)
        {
            return Version(
                buf ? (buf[0] >> PREFIX_BITS) & VERSION_MASK : EMPTY);
        }

        Version version() const { return KeyPart::version(data_); }

        KeyPart (const KeyPart& k) : data_(k.data_) {}

        KeyPart& operator= (const KeyPart& k) { data_ = k.data_; return *this; }

        /* for hash table */
        bool match (const KeyPart& kp) const
        {
            assert (NULL != this->data_);
            assert (NULL != kp.data_);

            bool ret(true); // collision by default

#if GU_WORDSIZE == 64
            const uint64_t* lhs(reinterpret_cast<const uint64_t*>(data_));
            const uint64_t* rhs(reinterpret_cast<const uint64_t*>(kp.data_));
#else
            const uint32_t* lhs(reinterpret_cast<const uint32_t*>(data_));
            const uint32_t* rhs(reinterpret_cast<const uint32_t*>(kp.data_));
#endif /* WORDSIZE */

            switch (std::max(version(), kp.version()))
            {
            case FLAT16:
            case FLAT16A:
#if GU_WORDSIZE == 64
                ret = (lhs[1] == rhs[1]);
#else
                ret = (lhs[2] == rhs[2] && lhs[3] == rhs[3]);
#endif /* WORDSIZE */
            case FLAT8:
            case FLAT8A:
                /* shift is to clear up the header */
#if GU_WORDSIZE == 64
                ret = ret && ((gtoh64(lhs[0]) >> HEADER_BITS) ==
                              (gtoh64(rhs[0]) >> HEADER_BITS));
#else
                ret = ret && (lhs[1] == rhs[1] &&
                              (gtoh32(lhs[0]) >> HEADER_BITS) ==
                              (gtoh32(rhs[0]) >> HEADER_BITS));
#endif /* WORDSIZE */
                return ret;
            case EMPTY:;
            }

            assert(0);
            gu_throw_error(EINVAL) << "Attempt to match against an empty key.";
        }

        size_t
        hash () const
        {
            /* Now this leaves uppermost bits always 0.
             * How bad is it in practice? Is it reasonable to assume that only
             * lower bits are used in unordered set? */
            size_t ret(gu::gtoh(reinterpret_cast<const size_t*>(data_)[0]) >>
                       HEADER_BITS);
            return ret; // (ret ^ (ret << HEADER_BITS)) to cover 0 bits
        }

        static size_t
        serial_size (const gu::byte_t* const buf, size_t const size)
        {
            Version const ver(version(buf));

            return serial_size (ver, buf, size);
        }

        size_t
        serial_size () const { return KeyPart::serial_size(data_, -1U); }

        void
        store (gu::RecordSetOut<KeyPart>& rs)
        {
            data_ = rs.append(data_, serial_size(), true, true).first;
//            log_info << "Stored key of size: " << serial_size();
        }

        void
        print (std::ostream& os) const;

        void
        swap (KeyPart& other)
        {
            using std::swap;
            swap(data_, other.data_);
        }

    private:

        static unsigned int const PREFIX_BITS  = 2;
        static gu::byte_t   const PREFIX_MASK  = (1 << PREFIX_BITS)   -1;
        static unsigned int const VERSION_BITS = 3;
        static gu::byte_t   const VERSION_MASK = (1 << VERSION_BITS) - 1;
        static unsigned int const HEADER_BITS  = PREFIX_BITS + VERSION_BITS;
        static gu::byte_t   const HEADER_MASK  = (1 << HEADER_BITS)  - 1;

        const gu::byte_t* data_; // it never owns the buffer

        static size_t
        base_size (Version const ver,
                   const gu::byte_t* const buf, size_t const size)
        {
            switch (ver)
            {
            case FLAT16:
            case FLAT16A:
                return 16;
            case FLAT8:
            case FLAT8A:
                return 8;
            case EMPTY: assert(0);
            }

            abort();
        }

        static bool
        annotated (Version const ver)
        {
            return (ver == FLAT16A || ver == FLAT8A);
        }

        typedef uint16_t ann_size_t;

        static size_t
        serial_size (Version const ver,
                     const gu::byte_t* const buf, size_t const size = -1U)
        {
            size_t ret(base_size(ver, buf, size));

            assert (ret <= size);

            if (annotated(ver))
            {
                assert (ret + 2 <= size);
                ret +=gu::gtoh(*reinterpret_cast<const ann_size_t*>(buf + ret));
                assert (ret <= size);
            }

            return ret;
        }

        static size_t
        store_annotation (const wsrep_buf_t* parts, int part_num,
                          gu::byte_t* buf, size_t size);

        static void
        print_annotation (std::ostream& os, const gu::byte_t* buf);

    }; /* class KeyPart */

    class KeyPartHash
    {
    public: size_t operator() (const KeyPart& k) const { return k.hash(); }
    };

    class KeyPartEqual
    {
    public:
        bool operator() (const KeyPart& l, const KeyPart& r) const
        {
            return (l == r);
        }
    }; /* functor KeyPartEqual */

}; /* class KeySet */

void swap (KeySet::KeyPart& a, KeySet::KeyPart& b) { a.swap(b); }

inline std::ostream&
operator << (std::ostream& os, const KeySet::KeyPart& kp)
{
    kp.print (os);
    return os;
}


#if defined(__GNUG__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#endif

class KeySetOut : public gu::RecordSetOut<KeySet::KeyPart>
{
public:

    typedef gu::UnorderedSet <
    KeySet::KeyPart, KeySet::KeyPartHash, KeySet::KeyPartEqual
    >
    KeyParts;

    class KeyPart
    {
    public:

        KeyPart (KeySet::Version const ver = KeySet::FLAT16)
            :
            hash_ (),
            part_ (0),
            value_(0),
            size_ (0),
            ver_  (ver),
            own_  (false)
        {
            assert (ver_);
        }

        /* to throw in KeyPart() ctor in case it is a duplicate */
        class DUPLICATE {};

        KeyPart (KeyParts&      added,
                 KeySetOut&     store,
                 const KeyPart* parent,
                 const KeyData& kd,
                 int const      part_num);

        KeyPart (const KeyPart& k)
        :
        hash_ (k.hash_),
        part_ (k.part_),
        value_(k.value_),
        size_ (k.size_),
        ver_  (k.ver_),
        own_  (k.own_)
        {
            assert (ver_);
            k.own_ = false;
        }

        friend void
        swap (KeyPart& l, KeyPart& r)
        {
            using std::swap;

            swap (l.hash_,  r.hash_ );
            swap (l.part_,  r.part_ );
            swap (l.value_, r.value_);
            swap (l.size_,  r.size_ );
            swap (l.ver_,   r.ver_  );
            swap (l.own_,   r.own_  );
        }

        KeyPart&
        operator= (KeyPart k) { swap(*this, k); return *this; }

        bool
        match (const void* const v, size_t const s) const
        {
            return (size_ == s && !(::memcmp (value_, v, size_)));
        }

        bool
        exclusive () const { return (part_ && part_->exclusive()); }

        bool
        shared () const { return !exclusive(); }

        void
        acquire()
        {
            gu::byte_t* tmp = new gu::byte_t[size_];
            std::copy(value_, value_ + size_, tmp);
            value_ = tmp; own_ = true;
        }

        void
        release()
        {
            if (own_)
            {
                log_info << "released: " << gu::Hexdump(value_, size_, true);
                delete[] value_; value_ = 0;
            }
            own_ = false;
        }

        ~KeyPart() { release(); }

        void
        print (std::ostream& os) const;

    private:

        gu::Hash          hash_;
        const KeySet::KeyPart* part_;
        mutable
        const gu::byte_t* value_;
        size_t            size_;
        KeySet::Version   ver_;
        mutable
        bool              own_;

    }; /* class KeySetOut::KeyPart */


    KeySetOut (const std::string&    base_name,
               KeySet::Version const version)
        :
        RecordSetOut (
            base_name,
            check_type      (version),
            ks_to_rs_version(version)
            ),
        version_(version),
        added_(),
        prev_ (),
        new_  ()
    {
        assert (version_);
        KeyPart zero(version_);
        prev_.push_back(zero);
    }

    ~KeySetOut () {}

    size_t
    append (const KeyData& kd);

    KeySet::Version
    version () { return count() ? version_ : KeySet::EMPTY; }

private:

    // depending on version we may pack data differently
    KeySet::Version      version_;
    KeyParts             added_;
    std::vector<KeyPart> prev_;
    std::vector<KeyPart> new_;


    static gu::RecordSet::CheckType
    check_type (KeySet::Version ver)
    {
        switch (ver)
        {
        case KeySet::EMPTY: break; /* Can't create EMPTY KeySetOut */
        default: return gu::RecordSet::CHECK_MMH128;
        }
        throw;
    }

    static gu::RecordSet::Version
    ks_to_rs_version (KeySet::Version ver)
    {
        switch (ver)
        {
        case KeySet::EMPTY: break; /* Can't create EMPTY KeySetOut */
        default: return gu::RecordSet::VER1;
        }
        throw;
    }

}; /* class KeySetOut */


inline std::ostream&
operator << (std::ostream& os, const KeySetOut::KeyPart& kp)
{
    kp.print (os);
    return os;
}


class KeySetIn : public gu::RecordSetIn<KeySet::KeyPart>
{
public:

    KeySetIn (KeySet::Version ver, const gu::byte_t* buf, size_t size)
        :
        RecordSetIn(buf, size, false),
        version_(ver)
    {}

    KeySetIn () : RecordSetIn(), version_(KeySet::EMPTY) {}

    void init (KeySet::Version ver, const gu::byte_t* buf, size_t size)
    {
        RecordSetIn::init(buf, size, false);
        version_ = ver;
    }

    KeySet::KeyPart const
    next () const { return RecordSetIn::next(); }

private:

    KeySet::Version version_;

}; /* class KeySetIn */

#if defined(__GNUG__)
#pragma GCC diagnostic pop
#endif

} /* namespace galera */

#endif // GALERA_KEY_SET_HPP
