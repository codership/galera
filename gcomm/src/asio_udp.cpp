/*
 * Copyright (C) 2010-2020 Codership Oy <info@codership.com>
 */

#include "asio_udp.hpp"

#include "gcomm/util.hpp"
#include "gcomm/common.hpp"

#include "gu_array.hpp"

#include <boost/bind.hpp>

gcomm::AsioUdpSocket::AsioUdpSocket(AsioProtonet& net, const gu::URI& uri)
    :
    Socket(uri),
    net_(net),
    state_(S_CLOSED),
    socket_(net_.io_service_.make_datagram_socket(uri)),
    recv_buf_((1 << 15) + NetHeader::serial_size_)
{ }


gcomm::AsioUdpSocket::~AsioUdpSocket()
{
    socket_->close();
}


void gcomm::AsioUdpSocket::connect(const gu::URI& uri)
{
    gcomm_assert(state() == S_CLOSED);
    Critical<AsioProtonet> crit(net_);
    socket_->connect(uri);
    async_receive();
    state_ = S_CONNECTED;
}

void gcomm::AsioUdpSocket::close()
{
    Critical<AsioProtonet> crit(net_);
    socket_->close();
    state_ = S_CLOSED;
}

int gcomm::AsioUdpSocket::send(int /* segment */, const Datagram& dg)
{
    Critical<AsioProtonet> crit(net_);
    NetHeader hdr(dg.len(), net_.version_);

    if (net_.checksum_ != NetHeader::CS_NONE)
    {
        hdr.set_crc32(crc32(net_.checksum_, dg), net_.checksum_);
    }

    // Make copy of datagram to be able to adjust the header
    Datagram priv_dg(dg);
    priv_dg.set_header_offset(priv_dg.header_offset() -
                              NetHeader::serial_size_);
    serialize(hdr,
              priv_dg.header(),
              priv_dg.header_size(),
              priv_dg.header_offset());

    std::array<gu::AsioConstBuffer, 2> cbs;
    cbs[0] = gu::AsioConstBuffer(dg.header()
                                 + dg.header_offset(),
                                 dg.header_len());
    cbs[1] = gu::AsioConstBuffer(dg.payload().data(),
                                 dg.payload().size());

    try
    {
        socket_->write(cbs);
    }
    catch (const gu::Exception& e)
    {
        log_warn << "Error: " << e.what();
        return e.get_errno();
    }
    return 0;
}


void gcomm::AsioUdpSocket::read_handler(gu::AsioDatagramSocket&,
                                        const gu::AsioErrorCode& ec,
                                        size_t bytes_transferred)
{
    if (ec)
    {
        //
        return;
    }

    if (bytes_transferred >= NetHeader::serial_size_)
    {
        Critical<AsioProtonet> crit(net_);
        NetHeader hdr;
        try
        {
            unserialize(&recv_buf_[0], NetHeader::serial_size_, 0, hdr);
        }
        catch (gu::Exception& e)
        {
            log_warn << "hdr unserialize failed: " << e.get_errno();
            return;
        }
        if (NetHeader::serial_size_ + hdr.len() != bytes_transferred)
        {
            log_warn << "len " << hdr.len()
                     << " does not match to bytes transferred"
                     << bytes_transferred;
        }
        else
        {
            Datagram dg(
                gu::SharedBuffer(
                    new gu::Buffer(&recv_buf_[0] + NetHeader::serial_size_,
                                   &recv_buf_[0] + NetHeader::serial_size_
                                   + hdr.len())));
            if (net_.checksum_ == true && check_cs(hdr, dg))
            {
                log_warn << "checksum failed, hdr: len=" << hdr.len()
                         << " has_crc32="  << hdr.has_crc32()
                         << " has_crc32c=" << hdr.has_crc32c()
                         << " crc32=" << hdr.crc32();
            }
            else
            {
                net_.dispatch(id(), dg, ProtoUpMeta());
            }
        }
    }
    else
    {
        log_warn << "short read of " << bytes_transferred;
    }
    async_receive();
}

void gcomm::AsioUdpSocket::async_receive()
{
    Critical<AsioProtonet> crit(net_);
    socket_->async_read(gu::AsioMutableBuffer(&recv_buf_[0], recv_buf_.size()),
                        shared_from_this());
}


size_t gcomm::AsioUdpSocket::mtu() const
{
    return (1 << 15);
}

std::string gcomm::AsioUdpSocket::local_addr() const
{
    return socket_->local_addr();
}

std::string gcomm::AsioUdpSocket::remote_addr() const
{
    // Not defined
    return "";
}
