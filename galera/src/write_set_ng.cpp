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

    size_t hsize = size(ver);

    try
    {
        gu::byte_t* hptr = new gu::byte_t[hsize];

        hptr[V3_HEADER_VER] = ver;
        hptr[V3_KEYSET_VER] = (kver << 4) | (dver << 2) | (uver);

        *(reinterpret_cast<uint16_t*>(hptr + V3_FLAGS))    = gu::htog(flags);
        *(reinterpret_cast<uint32_t*>(hptr + V3_PA_RANGE)) = 0;

        buf_.ptr  = hptr;
        buf_.size = hsize;
    }
    catch (std::bad_alloc& e)
    {
        gu_throw_error (ENOMEM) << "Could not allocate " << hsize << " bytes.";
    }

    return hsize;
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
    uint32_t* crc       = reinterpret_cast<uint32_t*>(ptr + V3_CRC);

    *last_seen = gu::htog<uint64_t>(ls);
    *timestamp = gu::htog<uint64_t>(gu_time_monotonic());

    uint32_t tmp;
    gu::CRC::digest(ptr, reinterpret_cast<gu::byte_t*>(crc) - ptr, tmp);
    *crc = gu::htog<uint32_t>(tmp);
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
    uint32_t* crc   = reinterpret_cast<uint32_t*>(ptr + V3_CRC);

    *pr = gu::htog<uint32_t>(pa_range);
    *sq = gu::htog<uint64_t>(seqno);

    uint32_t tmp;
    gu::CRC::digest(ptr, reinterpret_cast<gu::byte_t*>(crc) - ptr, tmp);
    *crc = gu::htog<uint32_t>(tmp);
}


WriteSetNG::Header::Checksum::Checksum(const gu::Buf& buf)
{
    assert (buf.size > 0);

    Version const ver   (Header::version(buf.ptr[0]));
    ssize_t const hsize (Header::size(ver));

    uint32_t check, hcheck;

    if (gu_likely (buf.size >= hsize))
    {
        switch (ver)
        {
        case VER3:
            size_t const hhsize(hsize - sizeof(uint32_t));
            gu::CRC::digest (buf.ptr, hhsize, check);

            hcheck = *(reinterpret_cast<const uint32_t*>(buf.ptr + hhsize));

            if (gu_likely(gu::htog(check) == hcheck)) return;
        }

        gu_throw_error (EINVAL) << "Header checksum mismatch: computed "
                                << gu::htog(check) << ", found " << hcheck;
    }

    gu_throw_error (EMSGSIZE) << "Buffer size " << buf.size
                              << " shorter than header size " << hsize;
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

} /* namespace galera */

