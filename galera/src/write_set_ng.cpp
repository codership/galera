//
// Copyright (C) 2013 Codership Oy <info@codership.com>
//


#include "write_set_ng.hpp"

#include "gu_time.h"


namespace galera
{

size_t
WriteSetNG::Header::init (Version const ver,
                          bool const has_keys,
                          bool const has_unrd)
{
    size_t hsize = size(ver);

    try
    {
        gu::byte_t* hptr = new gu::byte_t[hsize];

        hptr[0] = ver;
        hptr[1] = (F_HAS_KEYS * has_keys) | (F_HAS_UNRD * has_unrd);

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

/* only VER0 sypported so far */
    gu::byte_t* ptr     = const_cast<gu::byte_t*>(buf_.ptr);
    uint64_t* tstamp    = reinterpret_cast<uint64_t*>(ptr + 2);
    uint64_t* last_seen = tstamp + 1;
    uint32_t* crc       = reinterpret_cast<uint32_t*>(last_seen + 1);

    *tstamp    = gu::htog<uint64_t>(gu_time_monotonic());
    *last_seen = gu::htog<uint64_t>(ls);

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
        case VER0:
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

} /* namespace galera */

