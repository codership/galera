//
// Copyright (C) 2013 Codership Oy <info@codership.com>
//


#include "write_set_ng.hpp"

#include "gu_time.h"

#include <boost/static_assert.hpp>

namespace galera
{

WriteSetNG::Header::Offsets::Offsets (int a1, int a2, int a3, int a4, int a5,
                                      int a6, int a7, int a8, int a9, int a10,
                                      int a11)
:
    header_ver_  (a1),
    keyset_ver_  (a2),
    dataset_ver_ (a3),
    unrdset_ver_ (a4),
    flags_       (a5),
    dep_window_  (a6),
    last_seen_   (a7),
    seqno_       (a8),
    timestamp_   (a9),
    crc_         (a10),
    size_        (a11)
{}

WriteSetNG::Header::Offsets const
WriteSetNG::Header::V3 (
    V3_HEADER_VER,
    V3_KEYSET_VER,
    V3_DATASET_VER,
    V3_UNRDSET_VER,
    V3_FLAGS,
    V3_PA_RANGE,
    V3_LAST_SEEN,
    V3_SEQNO,
    V3_TIMESTAMP,
    V3_CRC,
    V3_SIZE
    );

size_t
WriteSetNG::Header::init (Version const          ver,
                          KeySet::Version const  kver,
                          DataSet::Version const dver,
                          DataSet::Version const uver,
                          uint16_t const         flags)
{
    BOOST_STATIC_ASSERT(MAX_VERSION <= 255);
    BOOST_STATIC_ASSERT(KeySet::MAX_VERSION <= 15);
    BOOST_STATIC_ASSERT(DataSet::MAX_VERSION <= 3);

    assert (uint(ver)  <= MAX_VERSION);
    assert (uint(kver) <= KeySet::MAX_VERSION);
    assert (uint(dver) <= DataSet::MAX_VERSION);
    assert (uint(uver) <= DataSet::MAX_VERSION);

    local_[V3_HEADER_VER] = ver;
    local_[V3_KEYSET_VER] = (kver << 4) | (dver << 2) | (uver);

    uint16_t* const fl(reinterpret_cast<uint16_t*>(local_ + V3_FLAGS));
    uint32_t* const pa(reinterpret_cast<uint32_t*>(local_ + V3_PA_RANGE));

    *fl = gu::htog(flags);
    *pa = 0;

    return buf_.size;
}


void
WriteSetNG::Header::set_last_seen(const wsrep_seqno_t& ls)
{
    assert (buf_.ptr);
    assert (buf_.size);

    /* only VER3 sypported so far */
    gu::byte_t* ptr     = const_cast<gu::byte_t*>(buf_.ptr);
    uint64_t* last_seen = reinterpret_cast<uint64_t*>(ptr + V3_LAST_SEEN);
    uint64_t* timestamp = reinterpret_cast<uint64_t*>(ptr + V3_TIMESTAMP);

    *last_seen = gu::htog<uint64_t>(ls);
    *timestamp = gu::htog<uint64_t>(gu_time_monotonic());

    update_checksum (ptr, V3_CRC);
}


void
WriteSetNG::Header::set_seqno(const wsrep_seqno_t& seqno, int pa_range)
{
    assert (buf_.ptr);
    assert (buf_.size);
    assert (seqno > 0);
    assert (pa_range > 0);

    /* only VER3 sypported so far */
    gu::byte_t* ptr = const_cast<gu::byte_t*>(buf_.ptr);
    uint32_t* pr    = reinterpret_cast<uint32_t*>(ptr + V3_PA_RANGE);
    uint64_t* sq    = reinterpret_cast<uint64_t*>(ptr + V3_SEQNO);

    *pr = gu::htog<uint32_t>(pa_range);
    *sq = gu::htog<uint64_t>(seqno);

    update_checksum (ptr, V3_CRC);
}


gu::Buf
WriteSetNG::Header::copy(bool const include_keys, bool const include_unrd)
{
    assert (buf_.ptr != &local_[0]);
    assert (size_t(size()) <= sizeof(local_));

    gu::byte_t* const ptr(&local_[0]);

    ::memcpy (ptr, buf_.ptr, size());

    gu::byte_t const mask(0x0c | (0xf0 * include_keys) | (0x03 * include_unrd));

    ptr[V3_KEYSET_VER] &= mask; // zero up versions of non-included sets

    update_checksum (ptr, V3_CRC);

    gu::Buf ret = { ptr, size() };
    return ret;
}


void
WriteSetNG::Header::Checksum::verify (const gu::byte_t* const ptr,
                                      ssize_t const           size)
{
    assert (size > 0);

    Version const ver   (Header::version(ptr[0]));
    ssize_t const hsize (Header::size(ver));

    if (gu_likely (size >= hsize))
    {
        type_t check, hcheck;

        switch (ver)
        {
        case VER3:
            size_t const hhsize(hsize - sizeof(check));

            compute (ptr, hhsize, check);

            hcheck = *(reinterpret_cast<const type_t*>(ptr + hhsize));

            if (gu_likely(check == hcheck)) return;
        }

        gu_throw_error (EINVAL) << "Header checksum mismatch: computed "
                                << std::hex << std::setfill('0')
                                << std::setw(sizeof(check) << 1)
                                << check << ", found "
                                << std::setw(sizeof(hcheck) << 1)
                                << hcheck;
    }

    gu_throw_error (EMSGSIZE) << "Buffer size " << size
                              << " shorter than header size " << hsize;
}


void
WriteSetIn::init (ssize_t const st)
{
    WriteSetNG::Version const ver  (header_.version());
    const gu::byte_t*         pptr (header_.payload());
    ssize_t                   psize(size_ - header_.size(ver));

    assert (psize >= 0);

    KeySet::Version const kver(header_.keyset_ver());
    if (kver != KeySet::EMPTY) gu_trace(keys_.init (kver, pptr, psize));

    if (gu_likely(size_ < st))
    {
        checksum();
        checksum_fin();
    }
    else
    {
        int err = pthread_create (&check_thr_, NULL,
                                  checksum_thread, this);

        if (gu_unlikely(err != 0))
        {
            gu_throw_error(err) << "Starting checksum thread failed";
        }
    }
}


void
WriteSetIn::checksum()
{
    const gu::byte_t*         pptr (header_.payload());
    ssize_t                   psize(size_ - header_.size());

    assert (psize >= 0);

    try
    {
        if (keys_.size() > 0)
        {
            gu_trace(keys_.checksum());
            psize -= keys_.size();
            assert (psize >= 0);
            pptr  += keys_.size();
        }

        DataSet::Version const dver(header_.dataset_ver());

        if (gu_likely(dver != DataSet::EMPTY))
        {
            gu_trace(data_.init(dver, pptr, psize));
            gu_trace(data_.checksum());
            psize -= data_.size();
            assert (psize >= 0);
        }

        DataSet::Version const uver(header_.unrdset_ver());

        if (uver != DataSet::EMPTY)
        {
            pptr  += data_.size();
            gu_trace(unrd_.init(uver, pptr, psize));
            gu_trace(unrd_.checksum());
#ifndef NDEBUG
            psize -= unrd_.size();
            assert (psize == 0);
#endif
        }

        check_ = true;
    }
    catch (std::exception& e)
    {
        log_error << e.what();
    }
    catch (...)
    {
        log_error << "Non-standard exception in WriteSet::checksum()";
    }
}


size_t
WriteSetIn::gather(std::vector<gu::Buf>& out,
                   bool include_keys, bool include_unrd)
{
    if (include_keys && include_unrd)
    {
        gu::Buf buf(header_());
        buf.size = size_;
        out.push_back(buf);
        return size_;
    }
    else
    {
        out.reserve(out.size() + 4);

        gu::Buf buf(header_.copy(include_keys, include_unrd));
        out.push_back(buf);
        size_t ret(buf.size);

        if (include_keys)
        {
            buf = keys_.buf();
            out.push_back(buf);
            ret += buf.size;
        }

        buf = data_.buf();
        out.push_back (buf);
        ret += buf.size;

        if (include_unrd)
        {
            buf = unrd_.buf();
            out.push_back(buf);
            ret += buf.size;
        }

        return ret;
    }
}


} /* namespace galera */

