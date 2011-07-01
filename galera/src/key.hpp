//
// Copyright (C) 2011 Codership Oy <info@codership.com>
//

#ifndef GALERA_KEY_HPP
#define GALERA_KEY_HPP

#include "serialization.hpp"
#include "wsrep_api.h"

#include "gu_unordered.hpp"
#include "gu_throw.hpp"

#include <deque>
#include <iomanip>
#include <iterator>

#include <cstring>
#include <stdint.h>

namespace galera
{

    // helper to cast from any kind of pointer to void
    template <typename C>
    static inline void* void_cast(const C* c)
    {
        return const_cast<void*>(reinterpret_cast<const void*>(c));
    }


    class KeyPart
    {
    public:
        KeyPart(const gu::byte_t* key) : key_(key) { }
        size_t size()           const { return (1 + key_[0]); }
        size_t key_len()        const { return key_[0]      ; }
        const gu::byte_t* key() const { return &key_[1]     ; }
        bool operator==(const KeyPart& other) const
        {
            return (key_[0] == other.key_[0] &&
                    memcmp(key_ + 1, other.key_ + 1, key_[0]) == 0);
        }
    private:
        const gu::byte_t* key_;
    };

    inline std::ostream& operator<<(std::ostream& os, const KeyPart& kp)
    {
        const std::ostream::fmtflags prev_flags(os.flags(std::ostream::hex));
        const char                   prev_fill(os.fill('0'));

        for (const gu::byte_t* i(kp.key()); i != kp.key() + kp.key_len();
             ++i)
        {
            os << std::setw(2) << static_cast<int>(*i);
        }
        os.flags(prev_flags);
        os.fill(prev_fill);

        return os;
    }


    class Key
    {
    public:
        enum
        {
            F_PARTIAL_MATCH_IS_FULL = 1 << 0
        };

        Key() : keys_() { }

        Key(const wsrep_key_t* keys, size_t keys_len)
            :
            keys_ ()
        {
            if (keys_len > 255)
            {
                gu_throw_error(EINVAL)
                    << "maximum number of key parts exceeded: " << keys_len;
            }

            for (size_t i(0); i < keys_len; ++i)
            {
                if (keys[i].key_len > 256)
                {
                    gu_throw_error(EINVAL)
                        << "key part length " << keys[i].key_len
                        << " greater than max 256";
                }
                gu::byte_t len(keys[i].key_len);
                const gu::byte_t* base(reinterpret_cast<const gu::byte_t*>(
                                           keys[i].key));
                keys_.push_back(len);
                keys_.insert(keys_.end(), base, base + len);
            }
        }

        template <class Ci>
        Key(Ci begin, Ci end) : keys_()
        {
            for (Ci i(begin); i != end; ++i)
            {
                keys_.push_back(i->key_len());
                keys_.insert(keys_.end(), i->key(), i->key() + i->key_len());
            }
        }

        template <class C>
        C key_parts() const
        {
            C ret;
            size_t i;
            for (i = 0; i < keys_.size(); )
            {
                KeyPart kp(&keys_[i]);
                ret.push_back(kp);
                i += kp.size();
            }
            if (i != keys_.size()) gu_throw_fatal;
            return ret;
        }

        bool operator==(const Key& other) const
        {
            return (keys_ == other.keys_);
        }

    private:
        friend class KeyHash;
        friend size_t serialize(const Key&, gu::byte_t*, size_t, size_t);
        friend size_t unserialize(const gu::byte_t*, size_t, size_t, Key&);
        friend size_t serial_size(const Key&);
        gu::Buffer keys_;
    };

    inline std::ostream& operator<<(std::ostream& os, const Key& key)
    {
        std::deque<KeyPart> dq(key.key_parts<std::deque<KeyPart> >());
        std::copy(dq.begin(), dq.end(), std::ostream_iterator<KeyPart>(os, " "));
        return os;
    }


    class KeyHash
    {
    public:
        size_t operator()(const Key& k) const
        {
            size_t prime(5381);
            for (gu::Buffer::const_iterator i(k.keys_.begin());
                 i != k.keys_.end(); ++i)
            {
                prime = ((prime << 5) + prime) + *i;
            }
            return prime;
        }
    };

    inline size_t serialize(const Key& key, gu::byte_t* buf, size_t buflen,
                            size_t offset)
    {
        return serialize<uint16_t>(key.keys_, buf, buflen, offset);
    }

    inline size_t unserialize(const gu::byte_t* buf, size_t buflen,
                              size_t offset, Key& key)
    {
        return unserialize<uint16_t>(buf, buflen, offset, key.keys_);
    }

    inline size_t serial_size(const Key& key)
    {
        return serial_size<uint16_t>(key.keys_);
    }

}


#endif // GALERA_KEY_HPP
