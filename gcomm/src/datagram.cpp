/*
 * Copyright (C) 2013 Codership Oy <info@codership.com>
 */

#include "gcomm/datagram.hpp"

#include "gu_crc.hpp"    // CRC-32C - optimized and potentially accelerated
#include "gu_logger.hpp"
#include "gu_throw.hpp"

#include <boost/crc.hpp> // CRC32   - backward compatible


gcomm::NetHeader::checksum_t
gcomm::NetHeader::checksum_type (int i)
{
    switch(i)
    {
    case CS_NONE:
        log_info << "Message checksums disabled.";
        return CS_NONE;
    case CS_CRC32:
        log_info << "Using CRC-32 (backward-compatible) for message checksums.";
        return CS_CRC32;
    case CS_CRC32C:
        log_info << "Using CRC-32C for message checksums.";
        return CS_CRC32C;
    }

    log_warn << "Ignoring unknown checksum type: " << i
             << ". Falling back to CRC-32.";

    return CS_CRC32;
}


uint16_t
gcomm::crc16(const gcomm::Datagram& dg, size_t offset)
{
    assert(offset < dg.len());

    gu::byte_t lenb[4];
    gu::serialize4(static_cast<int32_t>(dg.len() - offset),
                   lenb, sizeof(lenb), 0);

    boost::crc_16_type crc;

    crc.process_block(lenb, lenb + sizeof(lenb));

    if (offset < dg.header_len())
    {
        crc.process_block(dg.header_ + dg.header_offset_ + offset,
                          dg.header_ + dg.header_size_);
        offset = 0;
    }
    else
    {
        offset -= dg.header_len();
    }

    crc.process_block(dg.payload_->data() + offset,
                      dg.payload_->data() + dg.payload_->size());

    return crc.checksum();
}

uint32_t
gcomm::crc32(gcomm::NetHeader::checksum_t const type,
             const gcomm::Datagram& dg, size_t offset)
{
    gu::byte_t lenb[4];

    gu::serialize4(static_cast<int32_t>(dg.len() - offset),
                   lenb, sizeof(lenb), 0);

    if (NetHeader::CS_CRC32 == type)
    {
        boost::crc_32_type crc;

        crc.process_block(lenb, lenb + sizeof(lenb));

        if (offset < dg.header_len())
        {
            crc.process_block(dg.header_ + dg.header_offset_ + offset,
                              dg.header_ + dg.header_size_);
            offset = 0;
        }
        else
        {
            offset -= dg.header_len();
        }

        crc.process_block(dg.payload_->data() + offset,
                          dg.payload_->data() + dg.payload_->size());

        return crc.checksum();
    }
    else if (NetHeader::CS_CRC32C == type)
    {
        gu::CRC32C crc;

        crc.append (lenb, sizeof(lenb));

        if (offset < dg.header_len())
        {
            crc.append (dg.header_ + dg.header_offset_ + offset,
                        dg.header_size_ - dg.header_offset_ - offset);
            offset = 0;
        }
        else
        {
            offset -= dg.header_len();
        }

        crc.append (dg.payload_->data() + offset, dg.payload_->size() - offset);

        return crc();
    }

    gu_throw_error(EINVAL) << "Unsupported checksum algorithm: " << type;
}

