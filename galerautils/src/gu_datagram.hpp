/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#ifndef GU_DATAGRAM_HPP
#define GU_DATAGRAM_HPP

#include "gu_buffer.hpp"

#include <boost/crc.hpp>

#include <limits>

#include <cstring>
#include <stdint.h>

namespace gu
{
    class Datagram;
}


/*!
 * @brief  Datagram container
 *
 * Datagram class provides consistent interface for managing
 * datagrams/byte buffers.
 */
class gu::Datagram
{
public:
    Datagram()
        :
        header_       (),
        header_offset_(header_size_),
        payload_      (new Buffer()),
        offset_       (0)
    { }
    /*!
     * @brief Construct new datagram from byte buffer
     *
     * @param[in] buf Const pointer to data buffer
     * @param[in] buflen Length of data buffer
     *
     * @throws std::bad_alloc
     */

    Datagram(const Buffer& buf, size_t offset = 0)
        :
        header_       (),
        header_offset_(header_size_),
        payload_      (new Buffer(buf)),
        offset_       (offset)
    {
        assert(offset_ <= payload_->size());
    }

    Datagram(const SharedBuffer& buf, size_t offset = 0)
        :
        header_       (),
        header_offset_(header_size_),
        payload_      (buf),
        offset_       (offset)
    {
        assert(offset_ <= payload_->size());
    }

    /*!
     * @brief Copy constructor.
     *
     * @note Only for normalized datagrams.
     *
     * @param[in] dgram Datagram to make copy from
     * @param[in] off
     * @throws std::bad_alloc
     */
    Datagram(const Datagram& dgram,
             size_t off = std::numeric_limits<size_t>::max()) :
        // header_(dgram.header_),
        header_offset_(dgram.header_offset_),
        payload_(dgram.payload_),
        offset_(off == std::numeric_limits<size_t>::max() ? dgram.offset_ : off)
    {
        assert(offset_ <= dgram.get_len());
        memcpy(header_ + header_offset_,
               dgram.header_ + dgram.get_header_offset(),
               dgram.get_header_len());
    }

    /*!
     * @brief Destruct datagram
     */
    ~Datagram() { }

    void normalize()
    {
        const SharedBuffer old_payload(payload_);
        payload_ = SharedBuffer(new Buffer);
        payload_->reserve(get_header_len() + old_payload->size() - offset_);

        if (get_header_len() > offset_)
        {
            payload_->insert(payload_->end(),
                             header_ + header_offset_ + offset_,
                             header_ + header_size_);
            offset_ = 0;
        }
        else
        {
            offset_ -= get_header_len();
        }
        header_offset_ = header_size_;
        payload_->insert(payload_->end(), old_payload->begin() + offset_,
                         old_payload->end());
        offset_ = 0;
    }

    gu::byte_t* get_header() { return header_; }
    const gu::byte_t* get_header() const { return header_; }
    size_t get_header_size() const { return header_size_; }
    size_t get_header_len() const { return (header_size_ - header_offset_); }
    size_t get_header_offset() const { return header_offset_; }
    void set_header_offset(const size_t off)
    {
        assert(off <= header_size_);
        header_offset_ = off;
    }

    const Buffer& get_payload() const
    {
        assert(payload_ != 0);
        return *payload_;
    }
    Buffer& get_payload()
    {
        assert(payload_ != 0);
        return *payload_;
    }
    size_t get_len() const { return (header_size_ - header_offset_ + payload_->size()); }
    size_t get_offset() const { return offset_; }

    uint32_t checksum() const
    {
        boost::crc_32_type crc;
        crc.process_block(header_ + header_offset_, header_ + header_size_);
        crc.process_block(&(*payload_)[0], &(*payload_)[0] + payload_->size());
        return crc.checksum();
    }
private:
    static const size_t header_size_ = 128;
    gu::byte_t          header_[header_size_];
    size_t              header_offset_;
    SharedBuffer        payload_;
    size_t              offset_;
};

#endif // GU_DATAGRAM_HPP
