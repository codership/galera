//
// Copyright (C) 2013 Codership Oy <info@codership.com>
//

#include "key_set.hpp"

#include "gu_logger.hpp"
#include "gu_hexdump.hpp"

#include <limits>

namespace galera
{

size_t
KeySet::KeyPart::store_annotation (const wsrep_buf_t* const parts,
                                   int const part_num,
                                   gu::byte_t* buf, size_t const size)
{
    ann_size_t ann_size;

    size_t tmp_size(sizeof(ann_size));
    for (int i(0); i <= part_num; ++i)
    {
        tmp_size += 1 + std::min<size_t>(parts[i].len, 255);
    }
    tmp_size = std::min(tmp_size, size);
    ann_size = std::min<size_t>(tmp_size,std::numeric_limits<ann_size_t>::max());

    assert (ann_size <= size);

    *reinterpret_cast<ann_size_t*>(buf) = gu::htog(ann_size);

    size_t off(sizeof(ann_size_t));

    for (int i(0); i <= part_num && off < ann_size; ++i)
    {
        size_t left(ann_size - off - 1);
        gu::byte_t const part_len(
            std::min<size_t>(std::min(parts[i].len, left), 255U));

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

    size_t off(sizeof(ann_size_t));

    while (off < ann_size)
    {
        gu::byte_t const part_len(buf[off]); ++off;

        bool const last(ann_size == off + part_len);

        /* this is an attempt to guess whether we should interpret key part as
         * a string or numerical value */
        bool   const alpha(!last || part_len > 8);

        os << ' ' << gu::Hexdump (buf + off, part_len, alpha);

        off += part_len;
    }
}

static const char* ver_str[KeySet::MAX_VERSION + 1] =
{
    "EMPTY", "FLAT16", "FLAT16A", "FLAT8", "FLAT8A"
};

void
KeySet::KeyPart::print (std::ostream& os) const
{
    Version const ver(version());

    size_t const size(ver != EMPTY ? base_size(ver, data_, 1) : 0);

    os << '(' << int(exclusive()) << ',' << ver_str[ver] << ')'
       << gu::Hexdump(data_, size);

    if (annotated(ver))
    {
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
    bool const exclusive (!kd.shared && leaf);

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
                log_info << "KeyPart ctor: full duplicate of " << *found;
            else
                log_info << "Duplicate of exclusive: " << *found;
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

    os << " / " << gu::Hexdump(value_, size_, true);
}

size_t
KeySetOut::append (const KeyData& kd)
{
    int i(0);

    /* find common ancestor with the previous key */
    for (;
         i < kd.parts_num &&
             size_t(i + 1) < prev_.size() &&
             prev_[i + 1].match(kd.parts[i].ptr, kd.parts[i].len);
         ++i)
    {}
    /* matched i parts */

    /* if we have a fully matched key OR common ancestor is exclusive, return */
    if (i > 0)
    {
        assert (size_t(i) < prev_.size());

        if (prev_[i].exclusive())
        {
            assert (prev_.size() == (i + 1U));
            log_info << "Returning after matching exclusive key: " << prev_[i];
            return 0;
        }

        if (kd.parts_num == i) /* leaf */
        {
            assert (prev_[i].shared());
            if (kd.shared)
            {
                log_info << "Returning after matching all " << i << " parts";
                return 0;
            }
            else /* need to add exclusive copy of the key */
                --i;
        }
    }

    int const anc(i);
    const KeyPart* parent(&prev_[anc]);

//    log_debug << "Common ancestor: " << anc << ' ' << *parent;

    /* create parts that didn't match previous key and add to the set
     * of preiously added keys. */
    size_t const old_size (size());
    int j(0);
    for (; i < kd.parts_num; ++i, ++j)
    {
        try
        {
            KeyPart kp(added_, *this, parent, kd, i);
            if (size_t(j) < new_.size())
            {
                new_[j] = kp;
            }
            else
            {
                new_.push_back (kp);
            }
//            log_debug << "pushed " << kp;
        }
        catch (KeyPart::DUPLICATE& e)
        {
            assert (i + 1 == kd.parts_num);
            /* There is a very small probability that child part thows DUPLICATE
             * even after parent was added as a new key. It does not matter:
             * a duplicate will be a duplicate in certification as well. */
            log_info << "Returning after catching a DUPLICATE. Part: " << i;
            goto out;
        }

        parent = &new_[j];
    }

    assert (i == kd.parts_num);
    assert (anc + j == kd.parts_num);

    /* copy new parts to prev_ */
    if (gu_unlikely(prev_.size() != size_t(1 + kd.parts_num)))
    {
        prev_.resize(1 + kd.parts_num);
    }

    std::copy(new_.begin(), new_.begin() + j, prev_.begin() + anc + 1);

    /* acquire key part value if it is volatile */
    if (!kd.nocopy)
        for (int k(anc + 1); size_t(k) < prev_.size(); ++k)
        {
            prev_[k].acquire();
        }
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
