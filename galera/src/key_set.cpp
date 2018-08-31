//
// Copyright (C) 2013-2018 Codership Oy <info@codership.com>
//

#include "key_set.hpp"

#include "gu_logger.hpp"
#include "gu_hexdump.hpp"

#include <limits>
#include <algorithm> // std::transform

namespace galera
{

void
KeySet::throw_version(int ver)
{
    gu_throw_error(EINVAL) << "Unsupported KeySet version: " << ver;
}

static const char* ver_str[KeySet::MAX_VERSION + 1] =
{
    "EMPTY", "FLAT8", "FLAT8A", "FLAT16", "FLAT16A"
};

KeySet::Version
KeySet::version (const std::string& ver)
{
    std::string tmp(ver);
    std::transform(tmp.begin(), tmp.end(), tmp.begin(), ::toupper);

    for (int i(EMPTY); i <= MAX_VERSION; ++i)
    {
        if (tmp == ver_str[i]) return version(i);
    }

    gu_throw_error(EINVAL) << "Unsupported KeySet version: " << ver; throw;
}

static const char* type_str[4] = { "SH", "RE", "UP", "EX" };

const char*
KeySet::type(wsrep_key_type_t t)
{
    assert(size_t(t) < sizeof(type_str) / sizeof(type_str[0]));
    return type_str[t];
}

size_t
KeySet::KeyPart::store_annotation (const wsrep_buf_t* const parts,
                                   int                const part_num,
                                   gu::byte_t*              buf,
                                   int                const size,
                                   int                const alignment)
{
    assert(size >= 0);

    /* max len representable in one byte */
    static size_t const max_part_len(std::numeric_limits<gu::byte_t>::max());

    /* max multiple of alignment_ len representable in ann_size_t */
    ann_size_t const max_ann_len(std::numeric_limits<ann_size_t>::max() /
                                 alignment * alignment);

    ann_size_t ann_size;
    int        tmp_size(sizeof(ann_size));

    for (int i(0); i <= part_num; ++i)
    {
        tmp_size += 1 + std::min(parts[i].len, max_part_len);
    }

    assert(tmp_size > 0);

    /* Make sure that final annotation size is
     * 1) is a multiple of alignment
     * 2) is representable with ann_size_t
     * 3) doesn't exceed dst buffer size
     */
    ann_size = std::min<size_t>(GU_ALIGN(tmp_size, alignment), max_ann_len);
    ann_size = std::min<size_t>(ann_size, size / alignment * alignment);
    assert (ann_size <= size);
    assert ((ann_size % alignment) == 0);

    ann_size_t const pad_size(tmp_size < ann_size ? ann_size - tmp_size : 0);

    if (gu_likely(ann_size > 0))
    {
        ann_size_t const tmp(gu::htog(ann_size));
        ann_size_t       off(sizeof(tmp));

        ::memcpy(buf, &tmp, off);

        for (int i(0); i <= part_num && off < ann_size; ++i)
        {
            size_t const left(ann_size - off - 1);
            gu::byte_t const part_len
                (std::min(std::min(parts[i].len, left), max_part_len));

            buf[off] = part_len; ++off;

            const gu::byte_t* const from(
                static_cast<const gu::byte_t*>(parts[i].ptr));

            std::copy(from, from + part_len, buf + off);

            off += part_len;
        }

        if (pad_size > 0)
        {
            ::memset(buf + off, 0, pad_size);
            off += pad_size;
        }

        assert (off == ann_size);
    }
//    log_info << "stored annotation of size: " << ann_size;

    return ann_size;
}

void
KeySet::KeyPart::print_annotation(std::ostream& os, const gu::byte_t* buf)
{
    ann_size_t const ann_size(gu::gtoh<ann_size_t>(
                                  *reinterpret_cast<const ann_size_t*>(buf)));

    size_t const begin(sizeof(ann_size_t));
    size_t off(begin);

    while (off < ann_size)
    {
        if (off != begin) os << '/';

        gu::byte_t const part_len(buf[off]); ++off;

        bool const last(ann_size == off + part_len);

        /* this is an attempt to guess whether we should interpret key part as
         * a string or numerical value */
        bool const alpha(!last || part_len > 8);

        os << gu::Hexdump (buf + off, part_len, alpha);

        off += part_len;
    }
}

void
KeySet::KeyPart::throw_buffer_too_short (size_t expected, size_t got)
{
    gu_throw_error (EINVAL) << "Buffer too short: expected "
                            << expected << ", got " << got;
}

void
KeySet::KeyPart::throw_bad_type_version (wsrep_key_type_t t, int v)
{
    gu_throw_error(EINVAL) << "Internal program error: wsrep key type: " << t
                           << ", writeset version: " << v;
}

void
KeySet::KeyPart::throw_bad_prefix (gu::byte_t p)
{
    gu_throw_error(EPROTO) << "Unsupported key prefix: " << int(p);
}

void
KeySet::KeyPart::throw_match_empty_key (Version my, Version other)
{
    gu_throw_error(EINVAL) << "Attempt to match against an empty key ("
                           << my << ',' << other << ')';
}

void
KeySet::KeyPart::print (std::ostream& os) const
{
    Version const ver(version());

    size_t const size(ver != EMPTY ? base_size(ver, data_, 1) : 0);

    os << '(' << prefix() << ',' << ver_str[ver] << ')'
       << gu::Hexdump(data_, size);

    if (annotated(ver))
    {
        os << "=";
        print_annotation (os, data_ + size);
    }
}

/* returns true if left type is stronger than right */
static inline bool
key_prefix_is_stronger_than(int const left,
                            int const right)
{
    return left > right; // for now key prefix is numerically ordered
}

KeySetOut::KeyPart::KeyPart (KeyParts&      added,
                             KeySetOut&     store,
                             const KeyPart* parent,
                             const KeyData& kd,
                             int const      part_num,
                             int const      ws_ver,
                             int const      alignment)
    :
    hash_ (parent->hash_),
    part_ (0),
    value_(static_cast<const gu::byte_t*>(kd.parts[part_num].ptr)),
    size_ (kd.parts[part_num].len),
    ver_  (parent->ver_),
    own_  (false)
{
    assert (ver_);
    uint32_t const s(gu::htog(size_));
    hash_.append (&s, sizeof(s));
    hash_.append (value_, size_);

    KeySet::KeyPart::TmpStore ts;
    KeySet::KeyPart::HashData hd;

    hash_.gather<sizeof(hd.buf)>(hd.buf);

    /* only leaf part of the key can be not WSREP_KEY_SHARED */
    bool const leaf (part_num + 1 == kd.parts_num);
    wsrep_key_type_t const type (leaf ? kd.type : WSREP_KEY_SHARED);
    int const prefix (KeySet::KeyPart::prefix(type, ws_ver));

    assert (kd.parts_num > part_num);

    KeySet::KeyPart kp(ts, hd, kd.parts, ver_, prefix, part_num, alignment);

#if 0 /* find() way */
    /* the reason to use find() first, instead of going straight to insert()
     * is that we need to insert the part that was persistently stored in the
     * key set. At the same time we can't yet store the key part in the key set
     * before we can be sure that it is not a duplicate. Sort of 2PC. */
    KeyParts::iterator found(added.find(kp));

    if (added.end() != found)
    {
        if (key_prefix_is_stronger_than(prefix, found->prefix()))
        {       /* need to ditch weaker and add stronger version of the key */
            added.erase(found);
            found = added.end();
        }
        else if (leaf || key_prefix_is_stronger_than(found->prefix(), prefix))
        {
#ifndef NDEBUG
            if (leaf)
                log_debug << "KeyPart ctor: full duplicate of " << *found;
            else
                log_debug << "Duplicate of stronger: " << *found;
#endif
            throw DUPLICATE();
        }
    }

    if (added.end() == found)               /* no such key yet, store and add */
    {
        kp.store (store);
        std::pair<KeyParts::iterator, bool> res(added.insert(kp));
        assert (res.second);
        found = res.first;
    }

    part_ = &(*found);
#else /* insert() way */
    std::pair<KeyParts::iterator, bool> const inserted(added.insert(kp));

    if (inserted.second)
    {
        /* The key part was successfully inserted, store it in the key set
           buffer */
        inserted.first->store (store);
    }
    else
    {
        /* A matching key part instance is already present in the set,
           check constraints */
        if (key_prefix_is_stronger_than(prefix, inserted.first->prefix()))
        {
            /* The key part instance present in the set has weaker constraint,
               store this instance as well and update inserted to point there.
               (we can't update already stored data - it was checksummed, so we
               have to store a duplicate with a stronger constraint) */
            kp.store (store);
            inserted.first->update_ptr(kp.ptr());
            /* It is a hack, but it should be safe to modify key part already
               inserted into unordered set, as long as modification does not
               change hash and equality test results. And we get it to point to
               a duplicate here.*/
        }
        else if (leaf || key_prefix_is_stronger_than(inserted.first->prefix(),
                                                     prefix))
        {
            /* we don't throw DUPLICATE for branch parts, just ignore them.
               DUPLICATE is thrown only when the whole key is a duplicate. */
#ifndef NDEBUG
            if (leaf)
                log_debug << "KeyPart ctor: full duplicate of "
                          << *inserted.first;
            else
                log_debug << "Duplicate of exclusive: " << *inserted.first;
#endif
            throw DUPLICATE();
        }
    }

    part_ = &(*inserted.first);
#endif /* insert() way */
}

void
KeySetOut::KeyPart::print (std::ostream& os) const
{
    if (part_)
        os << *part_;
    else
        os << "0x0";

    os << '(' << gu::Hexdump(value_, size_, true) << ')';
}

#define CHECK_PREVIOUS_KEY 1

size_t
KeySetOut::append (const KeyData& kd)
{
    int i(0);

//    log_info << "Appending key data:" << kd;

#ifdef CHECK_PREVIOUS_KEY
    /* find common ancestor with the previous key */
    for (;
         i < kd.parts_num &&
             size_t(i + 1) < prev_.size() &&
             prev_[i + 1].match(kd.parts[i].ptr, kd.parts[i].len);
         ++i)
    {
#if 0
        log_info << "prev[" << (i+1) << "]\n"
                 << prev_[i+1]
                 << "\nmatches\n"
                 << gu::Hexdump(kd.parts[i].ptr, kd.parts[i].len, true);
#endif /* 0 */
    }
//    log_info << "matched " << i << " parts";

    int const kd_leaf_prefix(KeySet::KeyPart::prefix(kd.type, ws_ver_));

    /* if we have a fully matched key OR common ancestor is stronger, return */
    if (i > 0)
    {
        assert (size_t(i) < prev_.size());

        int const exclusive_prefix
            (KeySet::KeyPart::prefix(WSREP_KEY_EXCLUSIVE, ws_ver_));

        if (key_prefix_is_stronger_than(prev_[i].prefix(), kd_leaf_prefix) ||
            prev_[i].prefix() == exclusive_prefix)
        {
//            log_info << "Returning after matching a stronger key:\n"<<prev_[i];
            assert (prev_.size() == (i + 1U)); // only leaf can be exclusive.
            return 0;
        }

        if (kd.parts_num == i) /* leaf */
        {
            assert(!key_prefix_is_stronger_than(prev_[i].prefix(),
                                                kd_leaf_prefix));

            if (prev_[i].prefix() == kd_leaf_prefix)
            {
//                log_info << "Returning after matching all " << i << " parts";
                return 0;
            }
            else /* need to add a stronger copy of the leaf */
                --i;
        }
    }

    int const anc(i);
    const KeyPart* parent(&prev_[anc]);

//    log_info << "Common ancestor: " << anc << ' ' << *parent;
#else
    KeyPart tmp(prev_[0]);
    const KeyPart* const parent(&tmp);
#endif /* CHECK_PREVIOUS_KEY */

    /* create parts that didn't match previous key and add to the set
     * of previously added keys. */
    size_t const old_size (size());
    int j(0);
    for (; i < kd.parts_num; ++i, ++j)
    {
        try
        {
            KeyPart kp(added_, *this, parent, kd, i, ws_ver_, alignment());

#ifdef CHECK_PREVIOUS_KEY
            if (size_t(j) < new_.size())
            {
                new_[j] = kp;
            }
            else
            {
                new_().push_back (kp);
            }
            parent = &new_[j];
#else
            if (kd.copy) kp.acquire();
            if (i + 1 != kd.parts_num)
                tmp = kp; // <- updating parent for next iteration
#endif /* CHECK_PREVIOUS_KEY */

//            log_info << "pushed " << kp;
        }
        catch (KeyPart::DUPLICATE& e)
        {
            assert (i + 1 == kd.parts_num);
            /* There is a very small probability that child part throws DUPLICATE
             * even after parent was added as a new key. It does not matter:
             * a duplicate will be a duplicate in certification as well. */
#ifndef NDEBUG
//            log_debug << "Returning after catching a DUPLICATE. Part: " << i;
#endif /* NDEBUG */
            goto out;
        }
    }

    assert (i == kd.parts_num);
    assert (anc + j == kd.parts_num);

#ifdef CHECK_PREVIOUS_KEY
    /* copy new parts to prev_ */
    prev_().resize(1 + kd.parts_num);
    std::copy(new_().begin(), new_().begin() + j, prev_().begin() + anc + 1);

    /* acquire key part value if it is volatile */
    if (kd.copy)
        for (int k(anc + 1); size_t(k) < prev_.size(); ++k)
        {
            prev_[k].acquire();
        }
#endif /* CHECK_PREVIOUS_KEY */

out:
    return size() - old_size;
}

#if 0
const KeyIn&
galera::KeySetIn::get_key() const
{
    size_t offset(0);
    while (offset < keys_.size())
    {
        KeyOS key(version_);
        if ((offset = unserialize(&keys_[0], keys_.size(), offset, key)) == 0)
        {
            gu_throw_fatal << "failed to unserialize key";
        }
        s.push_back(key);
    }
    assert(offset == keys_.size());
}
#endif

} /* namespace galera */
