//
// Copyright (C) 2013 Codership Oy <info@codership.com>
//


#include "write_set_ng.hpp"

#include "gu_time.h"

#include <gu_macros.hpp>

#include <iomanip>

namespace galera
{

WriteSetNG::Header::Offsets::Offsets (
    int a01, int a02, int a03, int a04, int a05, int a06,
    int a07, int a08, int a09, int a10, int a11, int a12
    ) :
    header_ver_  (a01),
    header_size_ (a02),
    sets_        (a03),
    flags_       (a04),
    pa_range_    (a05),
    last_seen_   (a06),
    seqno_       (a07),
    timestamp_   (a08),
    source_id_   (a09),
    conn_id_     (a10),
    trx_id_      (a11),
    crc_         (a12)
{}

WriteSetNG::Header::Offsets const
WriteSetNG::Header::V3 (
    V3_HEADER_VERS_OFF,
    V3_HEADER_SIZE_OFF,
    V3_SETS_OFF,
    V3_FLAGS_OFF,
    V3_PA_RANGE_OFF,
    V3_LAST_SEEN_OFF,
    V3_SEQNO_OFF,
    V3_TIMESTAMP_OFF,
    V3_SOURCE_ID_OFF,
    V3_CONN_ID_OFF,
    V3_TRX_ID_OFF,
    V3_CRC_OFF
    );

size_t
WriteSetNG::Header::gather (KeySet::Version const  kver,
                            DataSet::Version const dver,
                            bool                   unord,
                            bool                   annot,
                            uint16_t const         flags,
                            const wsrep_uuid_t&    source,
                            const wsrep_conn_id_t& conn,
                            const wsrep_trx_id_t&  trx,
                            GatherVector&          out)
{
    GU_COMPILE_ASSERT(MAX_VERSION         <= 15, header_version_too_big);
    GU_COMPILE_ASSERT(KeySet::MAX_VERSION <= 15, keyset_version_too_big);
    GU_COMPILE_ASSERT(DataSet::MAX_VERSION <= 3, dataset_version_too_big);

    assert (uint(ver_) <= MAX_VERSION);
    assert (uint(kver) <= KeySet::MAX_VERSION);
    assert (uint(dver) <= DataSet::MAX_VERSION);

    local_[V3_MAGIC_OFF]       = MAGIC_BYTE;
    local_[V3_HEADER_VERS_OFF] = (version() << 4) | VER3;
    local_[V3_HEADER_SIZE_OFF] = size();

    local_[V3_SETS_OFF] = (kver << 4) | (dver << 2) |
        (unord * V3_UNORD_FLAG) | (annot * V3_ANNOT_FLAG);

    uint16_t* const fl(reinterpret_cast<uint16_t*>(local_ + V3_FLAGS_OFF));
    uint16_t* const pa(reinterpret_cast<uint16_t*>(local_ + V3_PA_RANGE_OFF));

    *fl = gu::htog<uint16_t>(flags);
    *pa = 0; // certified ws will have dep. window of at least 1

    wsrep_uuid_t* const sc(reinterpret_cast<wsrep_uuid_t*>(local_ +
                                                           V3_SOURCE_ID_OFF));
    *sc = source;

    uint64_t* const cn(reinterpret_cast<uint64_t*>(local_ + V3_CONN_ID_OFF));
    uint64_t* const tx(reinterpret_cast<uint64_t*>(local_ + V3_TRX_ID_OFF));

    *cn = gu::htog<uint64_t>(conn);
    *tx = gu::htog<uint64_t>(trx);

    gu::Buf const buf = { ptr_, size() };
    out->push_back(buf);

    return buf.size;
}


void
WriteSetNG::Header::set_last_seen(const wsrep_seqno_t& last_seen)
{
    assert (ptr_);
    assert (size_ > 0);

    uint64_t*   const ls  (reinterpret_cast<uint64_t*>(ptr_ +V3_LAST_SEEN_OFF));
    uint64_t*   const ts  (reinterpret_cast<uint64_t*>(ptr_ +V3_TIMESTAMP_OFF));

    *ls = gu::htog<uint64_t>(last_seen);
    *ts = gu::htog<uint64_t>(gu_time_monotonic());

    update_checksum (ptr_, size() - V3_CHECKSUM_SIZE);
}


void
WriteSetNG::Header::set_seqno(const wsrep_seqno_t& seqno,
                              uint16_t const pa_range)
{
    assert (ptr_);
    assert (size_ > 0);
    assert (seqno > 0);

    uint16_t* const pa(reinterpret_cast<uint16_t*>(ptr_ + V3_PA_RANGE_OFF));
    uint64_t* const sq(reinterpret_cast<uint64_t*>(ptr_ + V3_SEQNO_OFF));

    *pa = gu::htog<uint32_t>(pa_range);
    *sq = gu::htog<uint64_t>(seqno);

    update_checksum (ptr_, size() - V3_CHECKSUM_SIZE);
}


gu::Buf
WriteSetNG::Header::copy(bool const include_keys, bool const include_unrd) const
{
    assert (ptr_ != &local_[0]);
    assert (size_t(size()) <= sizeof(local_));

    gu::byte_t* const lptr(&local_[0]);

    ::memcpy (lptr, ptr_, size_);

    gu::byte_t const mask(0x0c | (0xf0 * include_keys) | (0x02 * include_unrd));

    lptr[V3_SETS_OFF] &= mask; // zero up versions of non-included sets

    update_checksum (lptr, size() - V3_CHECKSUM_SIZE);

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

    size_t const csize(hsize - V3_CHECKSUM_SIZE);

    compute (ptr, csize, check);

    hcheck = *(reinterpret_cast<const type_t*>(
                   reinterpret_cast<const gu::byte_t*>(ptr) + csize
                   ));

    if (gu_likely(check == hcheck)) return;

    gu_throw_error (EINVAL) << "Header checksum mismatch: computed "
                            << std::hex << std::setfill('0')
                            << std::setw(sizeof(check) << 1)
                            << check << ", found "
                            << std::setw(sizeof(hcheck) << 1)
                            << hcheck;
}


const char WriteSetOut::keys_suffix[] = "_keys";
const char WriteSetOut::data_suffix[] = "_data";
const char WriteSetOut::unrd_suffix[] = "_unrd";
const char WriteSetOut::annt_suffix[] = "_annt";


void
WriteSetIn::init (ssize_t const st)
{
    assert(false == check_thr_);

    const gu::byte_t* const pptr (header_.payload());
    ssize_t           const psize(size_ - header_.size());

    assert (psize >= 0);

    KeySet::Version const kver(header_.keyset_ver());

    if (kver != KeySet::EMPTY) gu_trace(keys_.init (kver, pptr, psize));

    assert (false == check_);
    assert (false == check_thr_);

    if (gu_likely(st > 0)) /* checksum enforced */
    {
        if (size_ >= st)
        {
            /* buffer too big, start checksumming in background */
            int const err(pthread_create (&check_thr_id_, NULL,
                                          checksum_thread, this));

            if (gu_likely(0 == err))
            {
                check_thr_ = true;
                return;
            }

            log_warn << "Starting checksum thread failed: " << err
                     << '(' << ::strerror(err) << ')';

            /* fall through to checksum in foreground */
        }

        checksum();
        checksum_fin();
    }
    else /* checksum skipped, pretend it's alright */
    {
        check_ = true;
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
            size_t tmpsize(data_.size());
            psize -= tmpsize;
            pptr  += tmpsize;
            assert (psize >= 0);

            if (header_.has_unrd())
            {
                gu_trace(unrd_.init(dver, pptr, psize));
                gu_trace(unrd_.checksum());
                size_t tmpsize(unrd_.size());
                psize -= tmpsize;
                pptr  += tmpsize;
            }

            if (header_.has_annt())
            {
                annt_ = new DataSetIn();
                gu_trace(annt_->init(dver, pptr, psize));
                // we don't care for annotation checksum - it is not a reason
                // to throw an exception and abort execution
                // gu_trace(annt_->checksum());
#ifndef NDEBUG
                psize -= annt_->size();
#endif
            }
        }
#ifndef NDEBUG
        assert (psize == 0);
#endif
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


void
WriteSetIn::write_annotation(std::ostream& os) const
{
    annt_->rewind();
    ssize_t const count(annt_->count());

    for (ssize_t i = 0; os.good() && i < count; ++i)
    {
        gu::Buf abuf = annt_->next();
        os.write(static_cast<const char*>(abuf.ptr), abuf.size);
    }
}


size_t
WriteSetIn::gather(GatherVector& out,
                   bool include_keys, bool include_unrd) const
{
    if (include_keys && include_unrd)
    {
        gu::Buf buf = { header_.ptr(), size_ };
        out->push_back(buf);
        return size_;
    }
    else
    {
        out->reserve(out->size() + 4);

        gu::Buf buf(header_.copy(include_keys, include_unrd));
        out->push_back(buf);
        size_t ret(buf.size);

        if (include_keys)
        {
            buf = keys_.buf();
            out->push_back(buf);
            ret += buf.size;
        }

        buf = data_.buf();
        out->push_back (buf);
        ret += buf.size;

        if (include_unrd)
        {
            buf = unrd_.buf();
            out->push_back(buf);
            ret += buf.size;
        }

        if (annotated())
        {
            buf = annt_->buf();
            out->push_back (buf);
            ret += buf.size;
        }

        return ret;
    }
}


} /* namespace galera */

