/* Copyright (C) 2013 Codership Oy <info@codership.com> */
/*!
 * @file common RecordSet implementation
 *
 * Record set is a collection of serialized records of the same type.
 *
 * It stores them in an iovec-like collection of buffers before sending
 * and restores from a single buffer when receiving.
 *
 * $Id$
 */

#include "gu_rset.hpp"

#include "gu_vlq.hpp"
#include "gu_hexdump.hpp"

#include <iomanip>

namespace gu
{

ssize_t
RecordSetOutBase::post_alloc (bool const          new_page,
                              const byte_t* const ptr,
                              ssize_t const       size)
{
    if (new_page)
    {
        Buf b = { ptr, size };
        bufs_.push_back (b);
    }
    else
    {
        bufs_.back().size += size;
    }

    size_ += size;

    return size_;
}

ssize_t
RecordSetOutBase::post_append (bool const          new_page,
                               const byte_t* const ptr,
                               ssize_t const       size)
{
    check_.append (ptr, size);
    count_++;

    return post_alloc (new_page, ptr, size);
}


static int check_size (RecordSet::CheckType const ct)
{
    switch (ct)
    {
    case RecordSet::CHECK_NONE:   return 0;
    case RecordSet::CHECK_MMH32:  return 4;
    case RecordSet::CHECK_MMH64:  return 8;
    case RecordSet::CHECK_MMH128: return 16;
    }

    log_fatal << "Non-existing RecordSet::CeckType value: " << ct;
    abort();
}

#define VER0_CRC_SIZE sizeof(uint32_t)


static int
header_size_max_v0()
{
    return
        1 + /* version + checksum type        */
        9 + /* max payload size in vlq format */
        9 + /* max record count in vlq format */
        VER0_CRC_SIZE;  /* header checksum    */
}


int
RecordSetOutBase::header_size_max() const
{
    switch (version_)
    {
    case VER0:
        return header_size_max_v0();
    }

    log_fatal << "Non-existing RecordSet::Version value: " << version_;
    abort();
}


static int
header_size_v0(ssize_t size, ssize_t const count)
{
    int hsize = header_size_max_v0();

    assert (size > hsize);
    assert (count > 0);

    /* need to converge on the header size as it depends on the total size */
    do
    {
        int new_hsize = 1 +      /* version + checksum type */
                        uleb128_size<size_t>(size) +  /* size  in vlq format */
                        uleb128_size<size_t>(count) + /* count in vlq format */
                        VER0_CRC_SIZE;                /* header checksum */

        assert (new_hsize <= hsize);

        if (new_hsize == hsize) break;

        size -= hsize - new_hsize;

        hsize = new_hsize;
    }
    while (true);

    assert (hsize > 0);
    assert (size > hsize);

    return hsize;
}


int
RecordSetOutBase::header_size() const
{
    switch (version_)
    {
    case 0:
        return header_size_v0 (size_, count_);
    }

    log_fatal << "Non-existing RecordSet::Version value: " << version_;
    abort();
}


ssize_t
RecordSetOutBase::write_header (byte_t* const buf, ssize_t const size)
{
    int const csize(check_size(check_type_));

    assert (header_size_max() + csize <= size);

    ssize_t const hdr_offset(header_size_max() - header_size());

    assert (hdr_offset >= 0);

    size_ -= hdr_offset;

    int off(hdr_offset);

    buf[off] = (static_cast<byte_t>(version_) << 4) | /* upper 4 bytes: ver */
               (static_cast<byte_t>(check_type_) & 0x0f);
    off += 1;

    off += uleb128_encode(size_,  buf + off, size - off);
    off += uleb128_encode(count_, buf + off, size - off);

    /* write header CRC */
    uint32_t crc;
    CRC::digest (buf + hdr_offset, off - hdr_offset, crc);
//    uint32_t const crc(
//        CRC::digest<uint32_t>(buf + hdr_offset, off - hdr_offset));
    *(reinterpret_cast<uint32_t*>(buf + off)) = htog(crc);
//    log_debug << "writing header CRC: " << std::showbase << std::internal
//              << std::hex << std::setfill('0') << std::setw(10) << crc;
    off += VER0_CRC_SIZE;

    /* append payload checksum */
    if (check_type_ != CHECK_NONE)
    {
        assert (csize <= size - off);
        // check_.append (buf + offset, off - offset);
        check_.serialize_to (buf + off, csize);
    }

    return hdr_offset;
}


ssize_t
RecordSetOutBase::gather (std::vector<Buf>& out)
{
    if (count_)
    {
        byte_t* const ptr    = const_cast<byte_t*>(bufs_.front().ptr);
        ssize_t const offset = write_header (ptr, bufs_.front().size);

        bufs_.front().ptr  += offset;
        bufs_.front().size -= offset;
        // size_ is taken care of in write_header()

        out.insert (out.end(), bufs_.begin(), bufs_.end());

        return size_;
    }
    else
    {
        return 0;
    }
}

RecordSet::RecordSet (Version ver, CheckType const ct)
    :
    version_   (ver),
    check_type_(ct),
    size_      (0),
    count_     (0),
    check_     ()
{
    if (version_ > MAX_VER)
    {
        gu_throw_error (EPROTO) << "Unsupported header version: " << version_;
    }
}


RecordSetOutBase::RecordSetOutBase (const std::string& base_name,
                                    CheckType const    ct,
                                    Version const      version,
                                    ssize_t const      max_size)
:
    RecordSet   (version, ct),
    max_size_   (max_size),
    alloc_      (base_name),
    bufs_       (),
    prev_stored_(true)
{
    /* reserve space for header */
    size_ = header_size_max() + check_size(check_type_);

    bool unused;
    byte_t* ptr = alloc_.alloc (size_, unused);

    Buf b = { ptr, size_ };
    bufs_.push_back (b);
}


static inline RecordSet::Version
header_version (const byte_t* buf, ssize_t const size)
{
    if (gu_unlikely (size < 1))
    {
        gu_throw_error (EINVAL) << "Buffer too short: " << size;
    }

    int ver = (buf[0] & 0xf0) >> 4;

    switch (ver)
    {
    case 0:  return RecordSet::VER0;
    }

    gu_throw_error (EPROTO) << "Unsupported RecordSet version: " << ver;
    throw;
}

static inline RecordSet::CheckType
ver0_check_type (const byte_t* buf, ssize_t const size)
{
    assert (size > 0);

    int const ct(buf[0] & 0x0f);

    switch (ct)
    {
    case RecordSet::CHECK_NONE:   return RecordSet::CHECK_NONE;
    case RecordSet::CHECK_MMH32:  return RecordSet::CHECK_MMH32;
    case RecordSet::CHECK_MMH64:  return RecordSet::CHECK_MMH64;
    case RecordSet::CHECK_MMH128: return RecordSet::CHECK_MMH128;
    }

    gu_throw_error (EPROTO) << "Unsupported RecordSet checksum type: " << ct;
    throw;
}

static inline RecordSet::CheckType
header_check_type(RecordSet::Version ver, const byte_t* ptr, ssize_t const size)
{
    assert (size > 0);

    switch (ver)
    {
    case RecordSet::VER0: return ver0_check_type (ptr, size);
    }

    gu_throw_error (EPROTO) << "Unsupported RecordSet version: " << ver;
    throw;
}

RecordSet::RecordSet (const byte_t* const ptr, ssize_t const size)
    :
    version_   (header_version (ptr, size)),
    check_type_(header_check_type (version_, ptr, size)),
    size_      (-1),
    count_     (-1),
    check_     ()
{}

void
RecordSetIn::parse_header_v0 (size_t const size)
{
    assert (size > 1);

    int off = 1;

    off += uleb128_decode (head_ + off, size - off, size_);

    if (gu_unlikely(static_cast<size_t>(size_) > static_cast<size_t>(size)))
    {
        gu_throw_error (EPROTO) << "RecordSet size " << size_
                                << " exceeds buffer size " << size
                                << "\nfirst 4 bytes: " << gu::Hexdump(head_, 4);
    }

    off += uleb128_decode (head_ + off, size - off, count_);

    if (gu_unlikely(static_cast<size_t>(size_) < static_cast<size_t>(count_)))
    {
        gu_throw_error (EPROTO) << "Corrupted RecordSet header: count "
                                << count_ << " exceeds size " << size_;
    }

    /* verify header CRC */
    uint32_t const crc_comp(CRC::digest<uint32_t>(head_, off));
    uint32_t const crc_orig(
        gtoh(*(reinterpret_cast<const uint32_t*>(head_ + off))));

    if (gu_unlikely(crc_comp != crc_orig))
    {
        gu_throw_error (EPROTO)
            << "RecordSet header CRC mismatch: "
            << std::showbase << std::internal << std::hex
            << std::setfill('0') << std::setw(10)
            << "\ncomputed: " << crc_comp
            << "\nfound:    " << crc_orig << std::dec;
    }
    off += VER0_CRC_SIZE;

    int const cs(check_size(check_type_));

    begin_ = off + cs;

    if (cs > 0 && check_first_) /* checksum records */
    {
        check_.append (head_ + begin_, size_ - begin_); /* records */

        std::vector<byte_t> checksum (cs, 0);
        check_.serialize_to (checksum.data(), cs);

        if (gu_unlikely(memcmp (checksum.data(), head_ + off, cs)))
        {
            gu_throw_error (EINVAL)
                << "RecordSet checksum does not match:\n"
                << "computed: "   << gu::Hexdump(head_ + off, cs)
                << "\nfound:    " << gu::Hexdump(checksum.data(), cs);
        }
    }
}

RecordSetIn::RecordSetIn (const byte_t* const ptr,
                          size_t const        size,
                          bool const          check_first)
    :
    RecordSet   (ptr, size),
    head_       (ptr),
    check_first_(check_first),
    begin_      (-1),
    next_       (begin_)
{
    switch (version_)
    {
    case VER0: parse_header_v0(size);
    }

    next_ = begin_;

    assert (size_  > 0);
    assert (count_ >= 0);
    assert (count_ <= size_);
    assert (begin_ > 0);
    assert (begin_ <= size_);
    assert (next_  == begin_);
}

} /* namespace gu */
