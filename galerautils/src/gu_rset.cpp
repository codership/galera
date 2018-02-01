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
#include "gu_throw.hpp"
#include "gu_logger.hpp"
#include "gu_serialize.hpp"

#include "gu_hash.h"
#include "gu_limits.h"

#include <iomanip>

namespace gu
{

int
RecordSet::check_size (RecordSet::CheckType const ct)
{
    switch (ct)
    {
    case RecordSet::CHECK_NONE:   return 0;
    case RecordSet::CHECK_MMH32:  return 4;
    case RecordSet::CHECK_MMH64:  return 8;
    case RecordSet::CHECK_MMH128: return 16;
#define MAX_CHECKSUM_SIZE                16
    }

    log_fatal << "Non-existing RecordSet::CheckType value: " << ct;
    abort();
}


#define VER1_2_CRC_SIZE sizeof(uint32_t)

static inline int
header_size_max_v1()
{
    return
        1 + /* version + checksum type        */
        9 + /* max payload size in vlq format */
        9 + /* max record count in vlq format */
        VER1_2_CRC_SIZE;  /* header checksum  */
}

#define VER2_ALIGNMENT gu::RecordSet::VER2_ALIGNMENT

static inline int
header_size_max_v2()
{
    int const ret( 1 + /* version + checksum type        */
                   9 + /* max payload size in vlq format */
                   9 + /* max record count in vlq format */
                   1 + /* alignment padding              */
                   VER1_2_CRC_SIZE /* header checksum    */ );
    GU_COMPILE_ASSERT((ret % VER2_ALIGNMENT) == 0, bad_max_size);
    return ret;
}


inline int
RecordSetOutBase::header_size_max() const
{
    switch (version())
    {
    case EMPTY: assert (0);
        break;
    case VER1:
        return header_size_max_v1();
    case VER2:
        return header_size_max_v2();
    }

    log_fatal << "Unsupported RecordSet::Version value: " << version();
    abort();
}


template <bool VER2>
inline int
header_size_v1_2(ssize_t size, int const count)
{
    int hsize(VER2 ? header_size_max_v2() : header_size_max_v1());

    assert (size > hsize);
    assert (count > 0);

    /* need to converge on the header size as it depends on the total size */
    do
    {
        int new_hsize = 1 +                       /* version + checksum type */
                        uleb128_size<size_t>(size) +  /* size  in vlq format */
                        uleb128_size<size_t>(count) + /* count in vlq format */
                        VER1_2_CRC_SIZE;              /* header checksum     */

        if (VER2) new_hsize = GU_ALIGN(new_hsize, VER2_ALIGNMENT);

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

static int
header_size_v1(ssize_t size, ssize_t const count)
{
    return header_size_v1_2<false>(size, count);
}

/*
 * Since in VER2 we want everything to be aligned to 8 bytes, we can have
 * an important optimization for smaller sets by fitting count and size
 * into bytes 1-3 of the header, thus fitting whole header into 8 bytes:
 *
 * |    BYTE 0    |    BYTE 1    |    BYTE 2    |    BYTE 3    |    4 bytes
 * | VERSION BYTE |   COUNT BITS     |         SIZE BITS       | header checksum
 *
 * Optimizing for maximum count of 16-byte records, so size = count*16.
 * This will allow to encode up to 1K records of total 16K size. This is
 * more than can be represented by VLQ in 1 and 2 bytes respectively:
 * 127 records of total 16K-1 size.
 *
 * Assuming little-endian encoding.
 */
#define VER2_COUNT_SIZE_LEN 24 /* total bits avaiable in short version    */

#define VER2_COUNT_LEN  ((VER2_COUNT_SIZE_LEN - 4) / 2) /* bits for count */

/* max count value we can encode in VER2_COUNT_LEN bits [1, ...] */
#define VER2_COUNT_MAX  (1 << VER2_COUNT_LEN)

#define VER2_COUNT_OFF  8               /* count offset: 8 bits of byte 0 */
#define VER2_COUNT_MASK ((VER2_COUNT_MAX - 1) << VER2_COUNT_OFF)
#define VER2_COUNT(h)   ((((h) & VER2_COUNT_MASK) >> VER2_COUNT_OFF) + 1)

#define VER2_SIZE_LEN  (VER2_COUNT_SIZE_LEN - VER2_COUNT_LEN) /* bits for size*/

/* max size value we can encode in VER2_SIZE_LEN bits [1, ...] */
#define VER2_SIZE_MAX  (1 << VER2_SIZE_LEN)

#define VER2_SIZE_OFF  (VER2_COUNT_OFF + VER2_COUNT_LEN)        /* size offset*/
#define VER2_SIZE_MASK ((VER2_SIZE_MAX - 1) << VER2_SIZE_OFF)
#define VER2_SIZE(h)   ((((h) & VER2_SIZE_MASK) >> VER2_SIZE_OFF) + 1)

#define VER2_REDUCTION (2 * VER2_ALIGNMENT)
#define VER2_SHORT_FLAG 0x08 /* flag to distinguish between short and long ver */

static int
header_size_v2(ssize_t const size, int const count)
{
    assert(count > 0); // should never send empty recordsets

    /* if we potentially can fit count and size in 3 bytes
     * header (and the whole set) can be shortened by 16 */
    bool const can_reduce((count <= VER2_COUNT_MAX) &&
                          ((size - VER2_REDUCTION) <= VER2_SIZE_MAX));
    if (can_reduce)
    {
        return header_size_max_v2() - VER2_REDUCTION;
    }
    else
    {
        return header_size_v1_2<true>(size, count);
    }
}


inline int
RecordSetOutBase::header_size() const
{
    switch (version())
    {
    case EMPTY: assert(0);
        break;
    case VER1:
        return header_size_v1(size_, count_);
    case VER2:
        return header_size_v2(size_, count_);
    }

    log_fatal << "Unsupported RecordSet::Version value: " << version();
    abort();
}


ssize_t
RecordSetOutBase::write_header (byte_t* const buf, ssize_t const size)
{
    assert((uintptr_t(buf) % GU_WORD_BYTES) == 0);

    int const csize(check_size(check_type()));
    assert((csize % alignment()) == 0);

    assert (header_size_max() + csize <= size);

    int const hdr_size(header_size());
    ssize_t const hdr_offset(header_size_max() - hdr_size);

    assert (hdr_offset >= 0);
    assert ((hdr_offset % alignment()) == 0);

    size_ -= hdr_offset;

    int off(hdr_offset);

    /* Version byte: upper 4 bits: version, lower 3 - checksum type */
    byte_t ver_byte((byte_t(version()) << 4) | (byte_t(check_type()) & 0x07));
    assert(0 == (ver_byte & VER2_SHORT_FLAG));

    switch (version())
    {
    case VER2:
        if (VER2_REDUCTION == off) /* 4 byte header version */
        {
            /* comparison above is a valid condition only if VER2_SIZE_MAX is
             * greater than 0x3fff, otherwise there may be ambiguity about the
             * two encoding methods */
            GU_COMPILE_ASSERT(VER2_SIZE_MAX > 0x3FFF, fix_condition);
            assert(count_ <= VER2_COUNT_MAX);
            assert(size_  <= VER2_SIZE_MAX);
            assert(uintptr_t(buf + off)%sizeof(uint32_t) == 0);
            uint32_t const h((uint32_t(size_ - 1) << VER2_SIZE_OFF)  |
                             (uint32_t(count_- 1) << VER2_COUNT_OFF) |
                             (ver_byte | VER2_SHORT_FLAG));
            gu::serialize4(h, buf, off);
            assert(off + 8 == header_size_max());
            break;
        }
        else /* long header version */
        {
            /* zero up potential padding bytes */
            ::memset(buf + off + 4, 0, hdr_size - 8);
        }
        /* fall through *//* to uleb encoding */
    case VER1:
        buf[off] = ver_byte; off += 1;
        off += uleb128_encode(size_, buf + off, size - off);
        uleb128_encode(count_, buf + off, size - off);
        break;
    case EMPTY:
        assert(0);
    }
    assert(off <= header_size_max() - 4);
    off = hdr_offset + hdr_size - 4; // compensate for padding gap in VER2

    /* write header CRC */
    uint32_t const crc(gu_fast_hash32(buf + hdr_offset, off - hdr_offset));
    off = gu::serialize4(crc, buf, off);
    assert((off % alignment()) == 0);
    assert(header_size_max() == off);

    /* append payload checksum */
    if (check_type() != CHECK_NONE)
    {
        assert (csize <= size - off);
        check_.append (buf + hdr_offset, off - hdr_offset); /* append header */
        check_.gather (buf + off, csize);
    }

    return hdr_offset;
}


ssize_t
RecordSetOutBase::gather (GatherVector& out)
{
    if (count_)
    {
        assert(count_ > 0);
        assert(size_  > 0);
#ifndef NDEBUG
        ssize_t const saved_size(size_);
#endif /* NDEBUG */
        unsigned int pad_size(0);

        if (gu_likely(VER2 == version()))
        {
            /* make sure size_ is padded to multiple of VER2_ALIGNMENT */
            int const dangling_bytes(size_ % VER2_ALIGNMENT);

            if(dangling_bytes)
            {
                assert(dangling_bytes < VER2_ALIGNMENT);
                pad_size = VER2_ALIGNMENT - dangling_bytes;
                bool new_page;
                byte_t* const pad_ptr(alloc(pad_size, new_page));

                /* zero up padding bytes to pacify valgrind:
                 * these bytes are checksummed along with the rest of the set
                 * and it makes valgrind unhappy if they are not initialized.
                 * However they don't need to be initialized to anything specific
                 * - they just need to remain unaltered */
                ::memset(pad_ptr, 0, pad_size);

                post_append(new_page, pad_ptr, pad_size);
                // note that size_ should be preserved and not increased here
                assert(saved_size == size_);
            }
        }

        byte_t* const ptr
            (static_cast<byte_t*>(const_cast<void*>(bufs_->front().ptr)));

        ssize_t const offset = write_header (ptr, bufs_->front().size);

        bufs_->front().ptr   = ptr + offset;
        bufs_->front().size -= offset;
        // size_ is taken care of in write_header()

        out->insert (out->end(), bufs_->begin(), bufs_->end());

        assert(((size_ + pad_size) % alignment()) == 0);
        return size_ + pad_size;
    }
    else
    {
        return 0;
    }
}

static inline byte_t
rset_alignment(RecordSet::Version ver)
{
    return (ver >= RecordSet::VER2 ? VER2_ALIGNMENT : 1);
}

RecordSet::RecordSet (Version ver, CheckType const ct)
    :
    size_      (0),
    count_     (0),
    version_   (ver),
    check_type_(ct),
    alignment_ (rset_alignment(ver))
{
    assert(uint(version_) <= MAX_VERSION);
    assert(uint(check_type_) < VER2_SHORT_FLAG);
}


RecordSetOutBase::RecordSetOutBase (byte_t*                 reserved,
                                    size_t                  reserved_size,
                                    const BaseName&         base_name,
                                    CheckType const         ct,
                                    Version const           version
#ifdef GU_RSET_CHECK_SIZE
                                    ,ssize_t const          max_size
#endif
    )
:
    RecordSet   (version, ct),
#ifdef GU_RSET_CHECK_SIZE
    max_size_   (max_size),
#endif
    alloc_      (base_name, reserved, reserved_size),
    check_      (),
    bufs_       (),
    prev_stored_(true)
{
    /* reserve space for header */
    size_ = header_size_max() + check_size(check_type());

    bool unused;
    byte_t* ptr(alloc_.alloc (size_, unused));
    assert(0 == uintptr_t(ptr) % GU_WORD_BYTES);

    Buf b = { ptr, size_ };
    bufs_->push_back (b);
}


static inline RecordSet::Version
header_version (const byte_t* buf, ssize_t const size)
{
    assert (NULL != buf);
    assert (size > 0);

    uint const ver((buf[0] & 0xf0) >> 4);

    assert (ver > 0);

    if (gu_likely(ver <= RecordSet::MAX_VERSION)) return RecordSet::Version(ver);

    gu_throw_error (EPROTO) << "Unsupported RecordSet version: " << ver;
}


static inline RecordSet::CheckType
header_check_type(RecordSet::Version const ver,
                  const byte_t* ptr, ssize_t const size)
{
    assert (size > 0);

    switch (ver)
    {
    case RecordSet::EMPTY: assert(0); return RecordSet::CHECK_NONE;
    case RecordSet::VER1:
    case RecordSet::VER2:
    {
        int const ct(ptr[0] & 0x07);

        switch (ct)
        {
        case RecordSet::CHECK_NONE:   return RecordSet::CHECK_NONE;
        case RecordSet::CHECK_MMH32:  if (RecordSet::VER2 == ver) break;
            return RecordSet::CHECK_MMH32;
        case RecordSet::CHECK_MMH64:  return RecordSet::CHECK_MMH64;
        case RecordSet::CHECK_MMH128: return RecordSet::CHECK_MMH128;
        }

        gu_throw_error (EPROTO) << "Unsupported RecordSet checksum type: " << ct;
    }
    }

    gu_throw_error (EPROTO) << "Unsupported RecordSet version: " << ver;
}


void
RecordSet::init (const byte_t* const ptr, ssize_t const size)
{
    assert (EMPTY == version_);
    assert (size >= 0);
    assert (NULL != ptr || 0 == size);
    assert (NULL == ptr || 0 != size);

    if (gu_likely ((ptr && size)))
    {
        version_    = header_version    (ptr, size);
        check_type_ = header_check_type (Version(version_), ptr, size);
        alignment_  = rset_alignment    (Version(version_));
    }
}

static inline size_t
read_size_count_v1_2(const byte_t* head_, size_t const size, size_t off,
                     ssize_t& size_, int& count_)
{
    off += uleb128_decode (head_ + off, size - off, size_);
    off += uleb128_decode (head_ + off, size - off, count_);
    return off;
}

static inline size_t
read_size_count_v2_short(const byte_t* head_, ssize_t& size_, int& count_)
{
    uint32_t const h(gu_le32(*reinterpret_cast<const uint32_t*>(head_)));
    size_  = VER2_SIZE (h);
    count_ = VER2_COUNT(h);
    return sizeof(h);
}

#define MIN_HEADER_SIZE 8 // it can't be smaller

void
RecordSetInBase::parse_header_v1_2 (size_t const size)
{
    assert (size > 8);
    assert (EMPTY != version());
    assert (0 != alignment());

    size_t off;

    if (VER2 == version() && (head_[0] & VER2_SHORT_FLAG))
    {
        off = read_size_count_v2_short(head_, size_, count_);
    }
    else
    {
        off = read_size_count_v1_2(head_, size, 1, size_, count_);
        off = GU_ALIGN((off + VER1_2_CRC_SIZE), alignment()); // end of header
        off -= VER1_2_CRC_SIZE;                               // header checksum
    }

    if (gu_unlikely(static_cast<size_t>(size_) > size))
    {
        gu_throw_error (EPROTO) << "RecordSet size " << size_
                                << " exceeds buffer size " << size
                                << "\nfirst 4 bytes: " << gu::Hexdump(head_, 4);
    }

    if (gu_unlikely(static_cast<size_t>(size_) < static_cast<size_t>(count_)))
    {
        gu_throw_error (EPROTO) << "Corrupted RecordSet header: count "
                                << count_ << " exceeds size " << size_;
    }

    /* verify header CRC */
    uint32_t const crc_comp(gu_fast_hash32(head_, off));
    uint32_t crc_orig; unserialize4(head_, off, crc_orig);

    if (gu_unlikely(crc_comp != crc_orig))
    {
        gu_throw_error (EPROTO)
            << "RecordSet header CRC mismatch: "
            << std::showbase << std::internal << std::hex
            << std::setfill('0') << std::setw(10)
            << "\ncomputed: " << crc_comp
            << "\nfound:    " << crc_orig << std::dec;
    }
    off += VER1_2_CRC_SIZE;
    assert((off % alignment()) == 0);

    /* checksum is between header and records */
    begin_ = off + check_size(check_type());
}


/* returns false if checksum matched and true if failed */
void
RecordSetInBase::checksum() const
{
    int const cs(check_size(check_type()));

    if (cs > 0) /* checksum records */
    {
        Hash check;

        check.append (head_ + begin_, serial_size() - begin_); /* records */
        check.append (head_, begin_ - cs);                     /* header  */

        assert(cs <= MAX_CHECKSUM_SIZE);
        byte_t result[MAX_CHECKSUM_SIZE];
        check.gather<sizeof(result)>(result);

        const byte_t* const stored_checksum(head_ + begin_ - cs);

        if (gu_unlikely(memcmp (result, stored_checksum, cs)))
        {
            gu_throw_error(EINVAL)
                << "RecordSet checksum does not match:"
                << "\ncomputed: " << gu::Hexdump(result, cs)
                << "\nfound:    " << gu::Hexdump(stored_checksum, cs);
        }
    }
}

uint64_t
RecordSetInBase::get_checksum() const
{
    unsigned int const checksum_size(check_size(check_type()));
    const void* const stored_checksum(head_ + begin_ - checksum_size);
    uint64_t ret(0);

    if (checksum_size >= sizeof(uint64_t))
        ret = *(static_cast<const uint64_t*>(stored_checksum));
    else if (checksum_size >= sizeof(uint32_t))
        ret = *(static_cast<const uint32_t*>(stored_checksum));
    else if (checksum_size >= sizeof(uint16_t))
        ret = *(static_cast<const uint16_t*>(stored_checksum));
    else if (checksum_size >= sizeof(uint8_t))
        ret = *(static_cast<const uint8_t*>(stored_checksum));

    return gu::gtoh<uint64_t>(ret);
}

RecordSetInBase::RecordSetInBase (const byte_t* const ptr,
                                  size_t const        size,
                                  bool const          check_now)
    :
    RecordSet   (),
    head_       (),
    next_       (),
    begin_      ()
{
    init (ptr, size, check_now);
}

void
RecordSetInBase::init (const byte_t* const ptr,
                       size_t const        size,
                       bool const          check_now)
{
    assert (EMPTY == version());

    RecordSet::init (ptr, size);

    head_ = ptr;

    switch (version())
    {
    case EMPTY: return;
    case VER1:
    case VER2:
        assert(0 != alignment());
        if (alignment() > 1) assert((uintptr_t(head_) % GU_WORD_BYTES) == 0);
        parse_header_v1_2(size); // should set begin_
    }

    if (check_now) checksum();

    next_ = begin_;

    assert (size_  >  0);
    assert (count_ >= 0);
    assert (count_ <= size_);
    assert (begin_ >  0);
    assert (begin_ <= size_);
    assert (next_  == begin_);
}

void
RecordSetInBase::throw_error (Error code) const
{
    switch (code)
    {
    case E_PERM:
        gu_throw_error (EPERM) << "Access beyond record set end.";

    case E_FAULT:
        gu_throw_error (EFAULT) << "Corrupted record set: record extends "
                                << next_ << " beyond set boundary " << size_;
    }

    log_fatal << "Unknown error in RecordSetIn.";
    abort();
}

} /* namespace gu */
