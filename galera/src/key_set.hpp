//
// Copyright (C) 2013-2018 Codership Oy <info@codership.com>
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

/* forward declarations for KeySet::KeyPart */
class KeySetOut;

class KeySet
{
public:

    enum Version
    {
        EMPTY = 0,
        FLAT8,    /*  8-byte hash (flat) */
        FLAT8A,   /*  8-byte hash (flat), annotated */
        FLAT16,   /* 16-byte hash (flat) */
        FLAT16A,  /* 16-byte hash (flat), annotated */
//      TREE8,    /*  8-byte hash + full serialized key */
        MAX_VERSION = FLAT16A
    };

    static Version version (unsigned int ver)
    {
        if (gu_likely (ver <= MAX_VERSION)) return static_cast<Version>(ver);

        throw_version(ver);
    }

    static Version version (const std::string& ver);

    static const char* type(wsrep_key_type_t const t);

    class Key
    {
    public:
        enum Prefix // this stays for backward compatibility
        {
            P_SHARED = 0,
            P_EXCLUSIVE
        };

        static int const TYPE_MAX = WSREP_KEY_EXCLUSIVE;
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

        union TmpStore { gu::byte_t buf[TMP_STORE_SIZE]; gu_word_t align; };
        union HashData { gu::byte_t buf[MAX_HASH_SIZE];  gu_word_t align; };

        /* This ctor creates a serialized representation of a key in tmp store
         * from a key hash and optional annotation. */
        KeyPart (TmpStore&       tmp,
                 const HashData& hash,
                 const wsrep_buf_t* parts, /* for annotation */
                 Version const   ver,
                 int const       prefix,
                 int const       part_num,
                 int const       alignment
            )
            : data_(tmp.buf)
        {
            assert(ver > EMPTY && ver <= MAX_VERSION);

            /* 16 if ver in { FLAT16, FLAT16A }, 8 otherwise */
            int const key_size
                (8 << (static_cast<unsigned int>(ver - FLAT16) <= 1));

            assert((key_size % alignment) == 0);
            assert((uintptr_t(tmp.buf)  % GU_WORD_BYTES) == 0);
            assert((uintptr_t(hash.buf) % GU_WORD_BYTES) == 0);

            ::memcpy (tmp.buf, hash.buf, key_size);

            /*  use lower bits for header:  */

            /* clear header bits */
            gu::byte_t b = tmp.buf[0] & (~HEADER_MASK);

            /* set prefix  */
            assert(prefix <= PREFIX_MASK);
            b |= (prefix & PREFIX_MASK);

            /* set version */
            b |= (ver & VERSION_MASK) << PREFIX_BITS;

            tmp.buf[0] = b;

            if (annotated(ver))
            {
                store_annotation(parts, part_num,
                                 tmp.buf + key_size,
                                 sizeof(tmp.buf) - key_size,
                                 alignment);
            }
        }

        /* This ctor uses pointer to a permanently stored serialized key part */
        KeyPart (const gu::byte_t* const buf, size_t const size)
            : data_(buf)
        {
            if (gu_likely(size >= 8 && serial_size() <= size)) return;

            throw_buffer_too_short (serial_size(), size);
        }

        explicit KeyPart (const gu::byte_t* ptr = NULL) : data_(ptr) {}

        /* converts wsrep key type to KeyPart "prefix" depending on writeset
         * version */
        static int prefix(wsrep_key_type_t const ws_type, int const ws_ver)
        {
            if (ws_ver >= 0 && ws_ver <= 5)
            {
                switch (ws_type)
                {
                case WSREP_KEY_SHARED:
                    return 0;
                case WSREP_KEY_REFERENCE:
                    return ws_ver < 4 ? KeySet::Key::P_EXCLUSIVE : 1;
                case WSREP_KEY_UPDATE:
                    return ws_ver < 4 ? KeySet::Key::P_EXCLUSIVE :
                    (ws_ver < 5 ? 1 : 2);
                case WSREP_KEY_EXCLUSIVE:
                    return ws_ver < 4 ? KeySet::Key::P_EXCLUSIVE :
                    (ws_ver < 5 ? 2 : 3);
                }
            }
            assert(0);
            throw_bad_type_version(ws_type, ws_ver);
        }

        /* The return value is subject to interpretation based on the
         * writeset version which is done in wsrep_type(int) method */
        int prefix() const
        {
            return (data_[0] & PREFIX_MASK);
        }

        wsrep_key_type_t wsrep_type(int const ws_ver) const
        {
            assert(ws_ver >= 0 && ws_ver <= 5);

            wsrep_key_type_t ret;

            switch (prefix())
            {
            case 0:
                ret = WSREP_KEY_SHARED;
                break;
            case 1:
                ret = ws_ver < 4 ? WSREP_KEY_EXCLUSIVE : WSREP_KEY_REFERENCE;
                break;
            case 2:
                assert(ws_ver >= 4);
                ret = ws_ver < 5 ? WSREP_KEY_EXCLUSIVE : WSREP_KEY_UPDATE;
                break;
            case 3:
                assert(ws_ver >= 5);
                ret = WSREP_KEY_EXCLUSIVE;
                break;
            default:
                throw_bad_prefix(prefix());
            }

            assert(prefix() == prefix(ret, ws_ver));
            return ret;
        }

        static Version version(const gu::byte_t* const buf)
        {
            return Version(
                buf ? (buf[0] >> PREFIX_BITS) & VERSION_MASK : EMPTY);
        }

        Version version() const { return KeyPart::version(data_); }

        KeyPart (const KeyPart& k) : data_(k.data_) {}

        KeyPart& operator= (const KeyPart& k) { data_ = k.data_; return *this; }

        /* for hash table */
        bool matches (const KeyPart& kp) const
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

            switch (std::min(version(), kp.version()))
            {
            case EMPTY:
                assert(0);
                throw_match_empty_key(version(), kp.version());
            case FLAT16:
            case FLAT16A:
#if GU_WORDSIZE == 64
                ret = (lhs[1] == rhs[1]);
#else
                ret = (lhs[2] == rhs[2] && lhs[3] == rhs[3]);
#endif /* WORDSIZE */
                /* fall through */
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
            }

            return ret;
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
        print (std::ostream& os) const;

        void
        swap (KeyPart& other)
        {
            using std::swap;
            swap(data_, other.data_);
        }

        const gu::byte_t* ptr() const { return data_; }

    protected:

        friend class KeySetOut;

        /* update data pointer */
        void update_ptr(const gu::byte_t* ptr) const { data_ = ptr; }

        /* update storage of KeyPart already inserted in unordered set */
        void store(gu::RecordSetOut<KeyPart>& rs) const
        {
            data_ = rs.append(data_, serial_size(), true, true).first;
//            log_info << "Stored key of size: " << serial_size();
        }

    private:

        static unsigned int const PREFIX_BITS  = 2;
        static gu::byte_t   const PREFIX_MASK  = (1 << PREFIX_BITS)  - 1;
        static unsigned int const VERSION_BITS = 3;
        static gu::byte_t   const VERSION_MASK = (1 << VERSION_BITS) - 1;
        static unsigned int const HEADER_BITS  = PREFIX_BITS + VERSION_BITS;
        static gu::byte_t   const HEADER_MASK  = (1 << HEADER_BITS)  - 1;

        mutable /* to be able to store const object */
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
                          gu::byte_t* buf, int size, int alignment);

        static void
        print_annotation (std::ostream& os, const gu::byte_t* buf);

        static void
        throw_buffer_too_short (size_t expected, size_t got) GU_NORETURN;
        static void
        throw_bad_type_version (wsrep_key_type_t t, int v)   GU_NORETURN;
        static void
        throw_bad_prefix       (gu::byte_t p)                GU_NORETURN;
        static void
        throw_match_empty_key  (Version my, Version other)   GU_NORETURN;
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
            return (l.matches(r));
        }
    }; /* functor KeyPartEqual */

    static void throw_version(int) GU_NORETURN;
}; /* class KeySet */

inline void
swap (KeySet::KeyPart& a, KeySet::KeyPart& b) { a.swap(b); }

inline std::ostream&
operator << (std::ostream& os, const KeySet::KeyPart& kp)
{
    kp.print (os);
    return os;
}


#if defined(__GNUG__)
# if (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || (__GNUC__ > 4)
#  pragma GCC diagnostic push
# endif // (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || (__GNUC__ > 4)
# pragma GCC diagnostic ignored "-Weffc++"
#endif

class KeySetOut : public gu::RecordSetOut<KeySet::KeyPart>
{
public:
    typedef gu::UnorderedSet <
        KeySet::KeyPart, KeySet::KeyPartHash, KeySet::KeyPartEqual
    >
    /* This #if decides whether we use straight gu::UnorderedSet for appended
     * key parts (0), or go for an optimized version (1). Don't remove it. */
#if 0
    KeyParts;
#else
    KeyPartSet;

    /* This is a naive mock up of an "unordered set" that first tries to use
     * preallocated set of buckets and falls back to a "real" heap-based
     * unordered set from STL/TR1 when preallocated one is exhausted.
     * The goal is to make sure that at least 3 keys can be inserted without
     * the need for dynamic allocation.
     * In practice, with 64 "buckets" and search depth of 3, the average
     * number of inserted keys before there is a need to go for heap is 25.
     * 128 buckets will give you 45 and 256 - around 80. */
    class KeyParts
    {
    public:
        KeyParts() : first_(), second_(NULL), first_size_(0)
        { ::memset(first_, 0, sizeof(first_)); }

        ~KeyParts() { delete second_; }

        /* This iterator class is declared for compatibility with
         * unordered_set. We may actually use a more simple interface here. */
        class iterator
        {
        public:
            iterator(const KeySet::KeyPart* kp) : kp_(kp) {}
            /* This is sort-of a dirty hack to ensure that first_ array
             * of KeyParts class can be treated like a POD array.
             * It uses the fact that the only non-static member of
             * KeySet::KeyPart is gu::byte_t* and so does direct casts between
             * pointers. I wish someone could make it cleaner. */
            iterator(const gu::byte_t** kp)
                : kp_(reinterpret_cast<const KeySet::KeyPart*>(kp)) {}
            const KeySet::KeyPart* operator -> () const { return kp_; }
            const KeySet::KeyPart& operator *  () const { return *kp_; }
            bool operator == (const iterator& i) const
            {
                return (kp_ == i.kp_);
            }
            bool operator != (const iterator& i) const
            {
                return (kp_ != i.kp_);
            }
        private:
            const KeySet::KeyPart* kp_;
        };

        const iterator end()
        {
            return iterator(static_cast<const KeySet::KeyPart*>(NULL));
        }

        const iterator find(const KeySet::KeyPart& kp)
        {
            unsigned int idx(kp.hash());

            for (unsigned int i(0); i < FIRST_DEPTH; ++i, ++idx)
            {
                idx &= FIRST_MASK;

                if (0 !=first_[idx] && KeySet::KeyPart(first_[idx]).matches(kp))
                {
                    return iterator(&first_[idx]);
                }
            }

            if (second_ && second_->size() > 0)
            {
                KeyPartSet::iterator i2(second_->find(kp));
                if (i2 != second_->end()) return iterator(&(*i2));
            }

            return end();
        }

        std::pair<iterator, bool> insert(const KeySet::KeyPart& kp)
        {
            unsigned int idx(kp.hash());

            for (unsigned int i(0); i < FIRST_DEPTH; ++i, ++idx)
            {
                idx &= FIRST_MASK;

                if (0 == first_[idx])
                {
                    first_[idx] = kp.ptr();
                    ++first_size_;
                    return
                        std::pair<iterator, bool>(iterator(&first_[idx]), true);
                }

                if (KeySet::KeyPart(first_[idx]).matches(kp))
                {
                    return
                        std::pair<iterator, bool>(iterator(&first_[idx]),false);
                }
            }

            if (!second_)
            {
                second_ = new KeyPartSet();
//                log_info << "Requesting heap at load factor "
//                         << first_size_ << '/' << FIRST_SIZE << " = "
//                         << (double(first_size_)/FIRST_SIZE);
            }

            std::pair<KeyPartSet::iterator, bool> res = second_->insert(kp);
            return std::pair<iterator, bool>(iterator(&(*res.first)),
                                             res.second);
        }

        iterator erase(iterator it)
        {
            unsigned int idx(it->hash());

            for (unsigned int i(0); i < FIRST_DEPTH; ++i, ++idx)
            {
                idx &= FIRST_MASK;

                if (first_[idx] && KeySet::KeyPart(first_[idx]).matches(*it))
                {
                    first_[idx] = 0;
                    --first_size_;
                    return iterator(&first_[(idx + 1) & FIRST_MASK]);
                }
            }

            if (second_ && second_->size() > 0)
            {
                KeyPartSet::iterator it2(second_->erase(second_->find(*it)));

                if (it2 != second_->end()) return iterator(&(*it2));
            }

            return end();
        }

        size_t size() const { return (first_size_ + second_->size()); }

    private:

        static unsigned int const FIRST_MASK  = 0x3f; // 63
        static unsigned int const FIRST_SIZE  = FIRST_MASK + 1;
        static unsigned int const FIRST_DEPTH = 3;

        const gu::byte_t* first_[FIRST_SIZE];
        KeyPartSet*       second_;
        unsigned int      first_size_;
    };
#endif /* 1 */

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
                 int const      part_num,
                 int const      ws_ver,
                 int const      alignment);

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

        int
        prefix() const { return (part_ ? part_->prefix() : 0); }

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
//                log_debug << "released: " << gu::Hexdump(value_, size_, true);
                delete[] value_; value_ = 0;
            }
            own_ = false;
        }

        ~KeyPart() { release(); }

        void
        print (std::ostream& os) const;

        typedef gu::RecordSet::GatherVector GatherVector;

    private:

        gu::Hash          hash_;
        const KeySet::KeyPart* part_;
        mutable
        const gu::byte_t* value_;
        unsigned int      size_;
        KeySet::Version   ver_;
        mutable
        bool              own_;

    }; /* class KeySetOut::KeyPart */


    KeySetOut () // empty ctor for slave TrxHandle
        :
        gu::RecordSetOut<KeySet::KeyPart>(),
        added_(),
        prev_ (),
        new_  (),
        version_()
    {}

    KeySetOut (gu::byte_t*             reserved,
               size_t                  reserved_size,
               const BaseName&         base_name,
               KeySet::Version const   version,
               gu::RecordSet::Version const rsv,
               int const               ws_ver)
        :
        gu::RecordSetOut<KeySet::KeyPart> (
            reserved,
            reserved_size,
            base_name,
            check_type(version),
            rsv
            ),
        added_(),
        prev_ (),
        new_  (),
        version_(version),
        ws_ver_(ws_ver)
    {
        assert (version_ != KeySet::EMPTY);
        assert ((uintptr_t(reserved) % GU_WORD_BYTES) == 0);
        assert (ws_ver <= 5);
        KeyPart zero(version_);
        prev_().push_back(zero);
    }

    ~KeySetOut () {}

    size_t
    append (const KeyData& kd);

    KeySet::Version
    version () { return count() ? version_ : KeySet::EMPTY; }

private:

    // depending on version we may pack data differently
    KeyParts              added_;
    gu::Vector<KeyPart,5> prev_;
    gu::Vector<KeyPart,5> new_;
    KeySet::Version       version_;
    int                   ws_ver_;

    static gu::RecordSet::CheckType
    check_type (KeySet::Version ver)
    {
        switch (ver)
        {
        case KeySet::EMPTY: break; /* Can't create EMPTY KeySetOut */
        default: return gu::RecordSet::CHECK_MMH128;
        }

        KeySet::throw_version(ver);
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
        gu::RecordSetIn<KeySet::KeyPart>(buf, size, false),
        version_(ver)
    {}

    KeySetIn () : gu::RecordSetIn<KeySet::KeyPart>(), version_(KeySet::EMPTY) {}

    void init (KeySet::Version ver, const gu::byte_t* buf, size_t size)
    {
        gu::RecordSetIn<KeySet::KeyPart>::init(buf, size, false);
        version_ = ver;
    }

    KeySet::KeyPart const
    next () const { return gu::RecordSetIn<KeySet::KeyPart>::next(); }

private:

    KeySet::Version version_;

}; /* class KeySetIn */

#if defined(__GNUG__)
# if (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || (__GNUC__ > 4)
#  pragma GCC diagnostic pop
# endif // (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || (__GNUC__ > 4)
#endif

} /* namespace galera */

#endif // GALERA_KEY_SET_HPP
