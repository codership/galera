//
// Copyright (C) 2011-2013 Codership Oy <info@codership.com>
//

#ifndef GALERA_KEY_HPP
#define GALERA_KEY_HPP

#include "wsrep_api.h"

#include "gu_hash.h"
#include "gu_serialize.hpp"
#include "gu_unordered.hpp"
#include "gu_throw.hpp"
#include "gu_logger.hpp"
#include "gu_vlq.hpp"

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


    class KeyPartOS
    {
    public:
        KeyPartOS(const gu::byte_t* buf, size_t buf_size)
            :
            buf_(buf),
            buf_size_(buf_size)
        { }
        const gu::byte_t* buf() const { return buf_; }
        size_t size() const { return buf_size_; }
        size_t key_len() const
        {
#ifndef GALERA_KEY_VLQ
            return buf_[0];
#else
            size_t ret;
            (void)gu::uleb128_decode(buf_, buf_size_, 0, ret);
            return ret;
#endif
        }
#ifndef GALERA_KEY_VLQ
        const gu::byte_t* key() const { return buf_ + 1; }
#else
        const gu::byte_t* key() const
        {
            size_t not_used;
            return buf_ + gu::uleb128_decode(buf_, buf_size_, 0, not_used);
        }
#endif
        bool operator==(const KeyPartOS& other) const
        {
            return (other.buf_size_ == buf_size_ &&
                    memcmp(other.buf_, buf_, buf_size_) == 0);
        }
    private:
        const gu::byte_t* buf_;
        size_t            buf_size_;
    };


    inline std::ostream& operator<<(std::ostream& os, const KeyPartOS& kp)
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


    class KeyOS
    {
    public:
        enum
        {
            F_SHARED = 0x1
        };

        KeyOS(int version) : version_(version), flags_(), keys_() { }

        KeyOS(int version, const wsrep_buf_t* keys, size_t keys_len,
            uint8_t flags)
            :
            version_(version),
            flags_  (flags),
            keys_   ()
        {
            if (keys_len > 255)
            {
                gu_throw_error(EINVAL)
                    << "maximum number of key parts exceeded: " << keys_len;
            }

            switch (version)
            {
            case 1:
            case 2:
                for (size_t i(0); i < keys_len; ++i)
                {
                    size_t const offset(keys_.size());
                    size_t key_len(keys[i].len);
                    const gu::byte_t* base(reinterpret_cast<const gu::byte_t*>(
                                               keys[i].ptr));
#ifndef GALERA_KEY_VLQ
                    if (gu_unlikely(key_len > 0xff)) key_len = 0xff;
                    keys_.reserve(offset + 1 + key_len);
                    keys_.insert(keys_.end(), key_len);
                    keys_.insert(keys_.end(), base, base + key_len);
#else
                    size_t len_size(gu::uleb128_size(key_len));
                    keys_.resize(offset + len_size);
                    (void)gu::uleb128_encode(
                        key_len, &keys_[0], keys_.size(), offset);
                    keys_.insert(keys_.end(), base, base + keys[i].key_len);
#endif
                }
                break;
            default:
                gu_throw_fatal << "unsupported key version: " << version_;
            }
        }

        template <class Ci>
        KeyOS(int version, Ci begin, Ci end, uint8_t flags)
            : version_(version), flags_(flags), keys_()
        {

            for (Ci i(begin); i != end; ++i)
            {
                keys_.insert(
                    keys_.end(), i->buf(), i->buf() + i->size());
            }
        }

        int version() const { return version_; }

        template <class C>
        C key_parts() const
        {
            C ret;
            size_t i(0);
            size_t const keys_size(keys_.size());

            while (i < keys_size)
            {
#ifndef GALERA_KEY_VLQ
                size_t key_len(keys_[i] + 1);
#else
                size_t key_len;
                size_t offset(
                    gu::uleb128_decode(&keys_[0], keys_size, i, key_len));
                key_len += offset - i;
#endif
                if (gu_unlikely((i + key_len) > keys_size))
                {
                    gu_throw_fatal
                        << "Keys buffer overflow by " << i + key_len - keys_size
                        << " bytes: " << i + key_len << '/' << keys_size;
                }

                KeyPartOS kp(&keys_[i], key_len);
                ret.push_back(kp);
                i += key_len;
            }
            assert(i == keys_size);

            return ret;
        }

        uint8_t flags() const { return flags_; }

        bool operator==(const KeyOS& other) const
        {
            return (keys_ == other.keys_);
        }

        bool equal_all(const KeyOS& other) const
        {
            return (version_ == other.version_ &&
                    flags_   == other.flags_   &&
                    keys_    == other.keys_);
        }

        size_t size() const
        {
            return keys_.size() + sizeof(*this);
        }

        size_t hash() const
        {
            return gu_table_hash(&keys_[0], keys_.size());
        }

        size_t hash_with_flags() const
        {
            return hash() ^ gu_table_hash(&flags_, sizeof(flags_));
        }

        size_t serialize(gu::byte_t*, size_t, size_t) const;
        size_t unserialize(const gu::byte_t*, size_t, size_t);
        size_t serial_size() const;

    private:
        friend std::ostream& operator<<(std::ostream& os, const KeyOS& key);
        int        version_;
        uint8_t    flags_;
        gu::Buffer keys_;
    };

    inline std::ostream& operator<<(std::ostream& os, const KeyOS& key)
    {
        std::ostream::fmtflags flags(os.flags());
        switch (key.version_)
        {
        case 2:
            os << std::hex << static_cast<int>(key.flags()) << " ";
            // Fall through
        case 1:
        {
            std::deque<KeyPartOS> dq(key.key_parts<std::deque<KeyPartOS> >());
            std::copy(dq.begin(), dq.end(),
                      std::ostream_iterator<KeyPartOS>(os, " "));
            break;
        }
        default:
            gu_throw_fatal << "unsupported key version: " << key.version_;
        }
        os.flags(flags);
        return os;
    }


    inline size_t
    KeyOS::serialize(gu::byte_t* buf, size_t buflen, size_t offset) const
    {
        switch (version_)
        {
#ifndef GALERA_KEY_VLQ
        case 1:
            return gu::serialize2(keys_, buf, buflen, offset);
        case 2:
            offset = gu::serialize1(flags_, buf, buflen, offset);
            return gu::serialize2(keys_, buf, buflen, offset);
#else
        case 1:
        {
            size_t keys_size(keys_.size());
            offset = gu::uleb128_encode(keys_size, buf, buflen, offset);
            assert (offset + key_size <= buflen);
            std::copy(&keys_[0], &keys_[0] + keys_size, buf + offset);
            return (offset + keys_size);
        }
#endif
        default:
            log_fatal << "Internal error: unsupported key version: "
                      << version_;
            abort();
            return 0;
        }
    }

    inline size_t
    KeyOS::unserialize(const gu::byte_t* buf, size_t buflen, size_t offset)
    {
        switch (version_)
        {
#ifndef GALERA_KEY_VLQ
        case 1:
            return gu::unserialize2(buf, buflen, offset, keys_);
        case 2:
            offset = gu::unserialize1(buf, buflen, offset, flags_);
            return gu::unserialize2(buf, buflen, offset, keys_);
#else
        case 1:
        {
            size_t len;
            offset = gu::uleb128_decode(buf, buflen, offset, len);
            keys_.resize(len);
            std::copy(buf + offset, buf + offset + len, keys_.begin());
            return (offset + len);
        }
#endif
        default:
            gu_throw_error(EPROTONOSUPPORT) << "unsupported key version: "
                                            << version_;
        }
    }

    inline size_t
    KeyOS::serial_size() const
    {
        switch (version_)
        {
#ifndef GALERA_KEY_VLQ
        case 1:
            return gu::serial_size2(keys_);
        case 2:
            return (gu::serial_size(flags_) + gu::serial_size2(keys_));
#else
        case 1:
        {
            size_t size(gu::uleb128_size(keys_.size()));
            return (size + keys_.size());
        }
#endif
        default:
            log_fatal << "Internal error: unsupported key version: "
                      << version_;
            abort();
            return 0;
        }
    }

}


#endif // GALERA_KEY_HPP
