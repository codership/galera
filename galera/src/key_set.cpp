//
// Copyright (C) 2013 Codership Oy <info@codership.com>
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

size_t
KeySet::KeyPart::store_annotation (const wsrep_buf_t* const parts,
                                   int const part_num,
                                   gu::byte_t* buf, int const size)
{
    assert(size >= 0);

    ann_size_t ann_size;
    int        tmp_size(sizeof(ann_size));

    for (int i(0); i <= part_num; ++i)
    {
        tmp_size += 1 + std::min(parts[i].len, 255);
    }

    tmp_size = std::min(tmp_size, size);
    ann_size = std::min<size_t>(tmp_size,
                                std::numeric_limits<ann_size_t>::max());

    assert (ann_size <= size);

    *reinterpret_cast<ann_size_t*>(buf) = gu::htog(ann_size);

    size_t off(sizeof(ann_size_t));

    for (int i(0); i <= part_num && off < ann_size; ++i)
    {
        int left(ann_size - off - 1);
        gu::byte_t const part_len(std::min(std::min(parts[i].len, left), 255));

        buf[off] = part_len; ++off;

        const gu::byte_t* const from(
            reinterpret_cast<const gu::byte_t*>(parts[i].ptr));

        std::copy(from, from + part_len, buf + off);

        off += part_len;
    }

    assert (off == ann_size);

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
KeySet::KeyPart::throw_bad_prefix (gu::byte_t p)
{
    gu_throw_error(EPROTO) << "Unsupported key prefix: " << p;
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

    os << '(' << int(exclusive()) << ',' << ver_str[ver] << ')'
       << gu::Hexdump(data_, size);

    if (annotated(ver))
    {
        os << "=";
        print_annotation (os, data_ + size);
    }
}

KeySetOut::KeyPart::KeyPart (KeyParts&      added,
                             KeySetOut&     store,
                             const KeyPart* parent,
                             const KeyData& kd,
                             int const      part_num)
    :
    hash_ (parent->hash_),
    part_ (0),
    value_(reinterpret_cast<const gu::byte_t*>(kd.parts[part_num].ptr)),
    size_ (kd.parts[part_num].len),
    ver_  (parent->ver_),
    own_  (false)
{
    assert (ver_);
//            uint32_t const s(gu::htog(size_));
    hash_.append (uint32_t(gu::htog(size_)));
    hash_.append (value_, size_);

    KeySet::KeyPart::TmpStore ts;
    KeySet::KeyPart::HashData hd;

    hash_.gather<sizeof(hd.buf)>(hd.buf);

    /* only leaf part of the key can be exclusive */
    bool const leaf (part_num + 1 == kd.parts_num);
    bool const exclusive (!kd.shared() && leaf);

    assert (kd.parts_num > part_num);

    KeySet::KeyPart kp(ts, hd, ver_, exclusive, kd.parts, part_num);

    KeyParts::iterator found(added.find(kp));

    if (added.end() != found)
    {
        if (exclusive && found->shared())
        {       /* need to ditch shared and add exclusive version of the key */
            assert (found->shared());
            added.erase(found);
            found = added.end();
        }
        else if (leaf || found->exclusive())
        {
#ifndef NDEBUG
            if (leaf)
                log_debug << "KeyPart ctor: full duplicate of " << *found;
            else
                log_debug << "Duplicate of exclusive: " << *found;
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

    /* if we have a fully matched key OR common ancestor is exclusive, return */
    if (i > 0)
    {
        assert (size_t(i) < prev_.size());

        if (prev_[i].exclusive())
        {
            assert (prev_.size() == (i + 1U));
//           log_info << "Returning after matching exclusive key:\n"<< prev_[i];
            return 0;
        }

        if (kd.parts_num == i) /* leaf */
        {
            assert (prev_[i].shared());
            if (kd.shared())
            {
//                log_info << "Returning after matching all " << i << " parts";
                return 0;
            }
            else /* need to add exclusive copy of the key */
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
     * of preiously added keys. */
    size_t const old_size (size());
    int j(0);
    for (; i < kd.parts_num; ++i, ++j)
    {
        try
        {
            KeyPart kp(added_, *this, parent, kd, i);

#ifdef CHECK_PREVIOUS_KEY
            if (size_t(j) < new_.size())
            {
                new_[j] = kp;
            }
            else
            {
                new_.push_back (kp);
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
            /* There is a very small probability that child part thows DUPLICATE
             * even after parent was added as a new key. It does not matter:
             * a duplicate will be a duplicate in certification as well. */
#ifndef NDEBUG
            log_debug << "Returning after catching a DUPLICATE. Part: " << i;
#endif /* NDEBUG */
            goto out;
        }
    }

    assert (i == kd.parts_num);
    assert (anc + j == kd.parts_num);

#ifdef CHECK_PREVIOUS_KEY
    /* copy new parts to prev_ */
    if (gu_unlikely(prev_.size() < size_t(1 + kd.parts_num)))
    {
        prev_.resize(1 + kd.parts_num);
    }

    std::copy(new_.begin(), new_.begin() + j, prev_.begin() + anc + 1);

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
