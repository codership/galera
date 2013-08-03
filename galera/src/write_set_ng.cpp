//
// Copyright (C) 2013 Codership Oy <info@codership.com>
//


#include "write_set_ng.hpp"

#include "gu_time.h"

#include <boost/static_assert.hpp>

#include <iomanip>

namespace galera
{

WriteSetNG::Header::Offsets::Offsets (
    int a01, int a02, int a03, int a04, int a05,
    int a06, int a07, int a08, int a09, int a10,
    int a11, int a12, int a13, int a14
    ) :
    header_ver_  (a01),
    keyset_ver_  (a02),
    dataset_ver_ (a03),
    unrdset_ver_ (a04),
    flags_       (a05),
    pa_range_    (a06),
    last_seen_   (a07),
    seqno_       (a08),
    timestamp_   (a09),
    source_id_   (a10),
    conn_id_     (a11),
    trx_id_      (a12),
    crc_         (a13),
    size_        (a14)
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
    V3_SOURCE_ID,
    V3_CONN_ID,
    V3_TRX_ID,
    V3_CRC,
    V3_SIZE
    );

size_t
WriteSetNG::Header::gather (Version const          ver,
                            KeySet::Version const  kver,
                            DataSet::Version const dver,
                            DataSet::Version const uver,
                            uint16_t const         flags,
                            const wsrep_uuid_t&    source,
                            const wsrep_conn_id_t& conn,
                            const wsrep_trx_id_t&  trx,
                            std::vector<gu::Buf>&  out)
{
    BOOST_STATIC_ASSERT(MAX_VERSION <= 255);
    BOOST_STATIC_ASSERT(KeySet::MAX_VERSION <= 15);
    BOOST_STATIC_ASSERT(DataSet::MAX_VERSION <= 3);

    assert (uint(ver)  <= MAX_VERSION);
    assert (uint(kver) <= KeySet::MAX_VERSION);
    assert (uint(dver) <= DataSet::MAX_VERSION);
    assert (uint(uver) <= DataSet::MAX_VERSION);

    /* this strange action is needed for compatibility with previous versions */
    gu::serialize4(uint32_t(ver) << 24, local_, V3_KEYSET_VER, 0);

    local_[V3_KEYSET_VER] = (kver << 4) | (dver << 2) | (uver);

    uint16_t* const fl(reinterpret_cast<uint16_t*>(local_ + V3_FLAGS));
    uint32_t* const pa(reinterpret_cast<uint32_t*>(local_ + V3_PA_RANGE));

    *fl = gu::htog<uint16_t>(flags);
    *pa = 0; // certified ws will have dep. window of at least 1

    wsrep_uuid_t* const sc(reinterpret_cast<wsrep_uuid_t*>(local_ +
                                                           V3_SOURCE_ID));
    *sc = source;

    uint64_t* const cn(reinterpret_cast<uint64_t*>(local_ + V3_CONN_ID));
    uint64_t* const tx(reinterpret_cast<uint64_t*>(local_ + V3_TRX_ID));

    *cn = gu::htog<uint64_t>(conn);
    *tx = gu::htog<uint64_t>(trx);

    gu::Buf const buf = { ptr_, size() };
    out.push_back(buf);

    return buf.size;
}


void
WriteSetNG::Header::set_last_seen(const wsrep_seqno_t& last_seen)
{
    assert (ptr_);
    assert (size_ > 0);

    /* only VER3 sypported so far */
    uint64_t*   const ls  (reinterpret_cast<uint64_t*>(ptr_ + V3_LAST_SEEN));
    uint64_t*   const ts  (reinterpret_cast<uint64_t*>(ptr_ + V3_TIMESTAMP));

    *ls = gu::htog<uint64_t>(last_seen);
    *ts = gu::htog<uint64_t>(gu_time_monotonic());

    update_checksum (ptr_, V3_CRC);
}


void
WriteSetNG::Header::set_seqno(const wsrep_seqno_t& seqno, int pa_range)
{
    assert (ptr_);
    assert (size_ > 0);
    assert (seqno > 0);
    assert (pa_range > 0);

    /* only VER3 sypported so far */
    uint32_t* const pa(reinterpret_cast<uint32_t*>(ptr_ + V3_PA_RANGE));
    uint64_t* const sq(reinterpret_cast<uint64_t*>(ptr_ + V3_SEQNO));

    *pa = gu::htog<uint32_t>(pa_range);
    *sq = gu::htog<uint64_t>(seqno);

    update_checksum (ptr_, V3_CRC);
}


gu::Buf
WriteSetNG::Header::copy(bool const include_keys, bool const include_unrd) const
{
    assert (ptr_ != &local_[0]);
    assert (size_t(size()) <= sizeof(local_));

    gu::byte_t* const lptr(&local_[0]);

    ::memcpy (lptr, ptr_, size_);

    gu::byte_t const mask(0x0c | (0xf0 * include_keys) | (0x03 * include_unrd));

    lptr[V3_KEYSET_VER] &= mask; // zero up versions of non-included sets

    update_checksum (lptr, V3_CRC);

    gu::Buf ret = { lptr, size_ };
    return ret;
}


void
WriteSetNG::Header::Checksum::verify (Version           ver,
                                      const void* const ptr,
                                      ssize_t const     hsize)
{
    assert (hsize > 0);

    type_t check(0), hcheck(0);

    switch (ver)
    {
    case VER3:
        size_t const hhsize(hsize - sizeof(check));

        compute (ptr, hhsize, check);

        hcheck = *(reinterpret_cast<const type_t*>(
                       reinterpret_cast<const gu::byte_t*>(ptr) + hhsize
                       ));

        if (gu_likely(check == hcheck)) return;
    }

    gu_throw_error (EINVAL) << "Header checksum mismatch: computed "
                            << std::hex << std::setfill('0')
                            << std::setw(sizeof(check) << 1)
                            << check << ", found "
                            << std::setw(sizeof(hcheck) << 1)
                            << hcheck;
}


void
WriteSetIn::init (ssize_t const st)
{
    const gu::byte_t* const pptr (header_.payload());
    ssize_t           const psize(size_ - header_.size());

    assert (psize >= 0);

    KeySet::Version const kver(header_.keyset_ver());

    if (kver != KeySet::EMPTY) gu_trace(keys_.init (kver, pptr, psize));

    if (size_ < st)
    {
        assert (false == check_);
        checksum();
        checksum_fin();
    }
    else if (st > 0) // st <= 0 means no checksumming (except for header) is
                     // performed.
    {
        assert (false == check_);
        int err = pthread_create (&check_thr_, NULL,
                                  checksum_thread, this);

        if (gu_unlikely(err != 0))
        {
            gu_throw_error(err) << "Starting checksum thread failed";
        }
    }
    else
    {
        assert (true == check_);
    }
}


void
WriteSetIn::checksum()
{
    const gu::byte_t* pptr (header_.payload());
    ssize_t           psize(size_ - header_.size());

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
            assert (psize > 0);
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
                   bool include_keys, bool include_unrd) const
{
    if (include_keys && include_unrd)
    {
        gu::Buf buf = { header_.ptr(), size_ };
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

