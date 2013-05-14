/*
 * Copyright (C) 2009-2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "evs_message2.hpp"

#include "gu_exception.hpp"
#include "gu_logger.hpp"


std::ostream&
gcomm::evs::operator<<(std::ostream& os, const gcomm::evs::MessageNode& node)
{
    os << "node: {";
    os << "operational=" << node.operational() << ",";
    os << "suspected=" << node.suspected() << ",";
    os << "leave_seq=" << node.leave_seq() << ",";
    os << "view_id=" << node.view_id() << ",";
    os << "safe_seq=" << node.safe_seq() << ",";
    os << "im_range=" << node.im_range() << ",";
    os << "}";
    return os;
}

std::ostream&
gcomm::evs::operator<<(std::ostream& os, const gcomm::evs::Message& msg)
{
    os << "evs::msg{";
    os << "version=" << static_cast<int>(msg.version()) << ",";
    os << "type=" << msg.type() << ",";
    os << "user_type=" << static_cast<int>(msg.user_type()) << ",";
    os << "order=" << msg.order() << ",";
    os << "seq=" << msg.seq() << ",";
    os << "seq_range=" << msg.seq_range() << ",";
    os << "aru_seq=" << msg.aru_seq() << ",";
    os << "flags=" << static_cast<int>(msg.flags()) << ",";
    os << "source=" << msg.source() << ",";
    os << "source_view_id=" << msg.source_view_id() << ",";
    os << "range_uuid=" << msg.range_uuid() << ",";
    os << "range=" << msg.range() << ",";
    os << "fifo_seq=" << msg.fifo_seq() << ",";
    os << "node_list=(" << msg.node_list() << ")\n";
    os << "}";
    return os;
}

size_t gcomm::evs::MessageNode::serialize(gu::byte_t* const buf,
                                          size_t      const buflen,
                                          size_t            offset) const
{
    uint8_t b =
        static_cast<uint8_t>((operational_ == true ? F_OPERATIONAL : 0) |
                             (suspected_   == true ? F_SUSPECTED   : 0));
    gu_trace(offset = gu::serialize1(b, buf, buflen, offset));
    gu_trace(offset = gu::serialize1(segment_, buf, buflen, offset));
    gu_trace(offset = gu::serialize8(leave_seq_, buf, buflen, offset));
    gu_trace(offset = view_id_.serialize(buf, buflen, offset));
    gu_trace(offset = gu::serialize8(safe_seq_, buf, buflen, offset));
    gu_trace(offset = im_range_.serialize(buf, buflen, offset));
    return offset;
}


size_t gcomm::evs::MessageNode::unserialize(const gu::byte_t* const buf,
                                            size_t            const buflen,
                                            size_t                  offset)
{
    uint8_t b;
    gu_trace(offset = gu::unserialize1(buf, buflen, offset, b));
    if ((b & ~(F_OPERATIONAL | F_SUSPECTED)) != 0)
    {
        log_warn << "unknown flags: " << static_cast<int>(b);
    }
    operational_ = b & F_OPERATIONAL;
    suspected_   = b & F_SUSPECTED;

    gu_trace(offset = gu::unserialize1(buf, buflen, offset, segment_));
    gu_trace(offset = gu::unserialize8(buf, buflen, offset, leave_seq_));
    gu_trace(offset = view_id_.unserialize(buf, buflen, offset));
    gu_trace(offset = gu::unserialize8(buf, buflen, offset, safe_seq_));
    gu_trace(offset = im_range_.unserialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::MessageNode::serial_size()
{
    return 2 +                  // 4 bytes reserved for flags
        sizeof(seqno_t) +
        ViewId::serial_size() +
        sizeof(seqno_t) +
        Range::serial_size();
}


bool gcomm::evs::Message::operator==(const Message& cmp) const
{
    return (version_         == cmp.version_         &&
            type_            == cmp.type_            &&
            user_type_       == cmp.user_type_       &&
            order_           == cmp.order_           &&
            seq_             == cmp.seq_             &&
            seq_range_       == cmp.seq_range_       &&
            aru_seq_         == cmp.aru_seq_         &&
            fifo_seq_        == cmp.fifo_seq_        &&
            flags_           == cmp.flags_           &&
            source_          == cmp.source_          &&
            source_view_id_  == cmp.source_view_id_  &&
            install_view_id_ == cmp.install_view_id_ &&
            range_uuid_      == cmp.range_uuid_      &&
            range_           == cmp.range_           &&
            node_list_       == cmp.node_list_);
}


size_t gcomm::evs::Message::serialize(gu::byte_t* const buf,
                                      size_t      const buflen,
                                      size_t            offset) const
{

    uint8_t b = static_cast<uint8_t>(version_ | (type_ << 2) | (order_ << 5));
    gu_trace(offset = gu::serialize1(b, buf, buflen, offset));
    gu_trace(offset = gu::serialize1(flags_, buf, buflen, offset));
    uint16_t pad(0);
    gu_trace(offset = gu::serialize2(pad, buf, buflen, offset));
    gu_trace(offset = gu::serialize8(fifo_seq_, buf, buflen, offset));
    if (flags_ & F_SOURCE)
    {
        gu_trace(offset = source_.serialize(buf, buflen, offset));
    }
    gu_trace(offset = source_view_id_.serialize(buf, buflen, offset));
    return offset;
}


size_t gcomm::evs::Message::unserialize(const gu::byte_t* const buf,
                                        size_t            const buflen,
                                        size_t                  offset)
{
    uint8_t b;
    gu_trace(offset = gu::unserialize1(buf, buflen, offset, b));

    version_ = static_cast<uint8_t>(b & 0x3);
    if (version_ != 0)
    {
        gu_throw_error(EPROTONOSUPPORT) << "protocol version not supported: "
                                        << version_;
    }

    type_    = static_cast<Type>((b >> 2) & 0x7);
    if (type_ <= T_NONE || type_ > T_LEAVE)
    {
        gu_throw_error(EINVAL) << "invalid type " << type_;
    }

    order_ = static_cast<Order>((b >> 5) & 0x7);
    if (order_ < O_DROP || order_ > O_SAFE)
    {
        gu_throw_error(EINVAL) << "invalid safety prefix "
                                    << order_;
    }

    gu_trace(offset = gu::unserialize1(buf, buflen, offset, flags_));

    uint16_t pad;
    gu_trace(offset = gu::unserialize2(buf, buflen, offset, pad));

    if (pad != 0)
    {
        gu_throw_error(EINVAL) << "invalid pad" << pad;
    }

    gu_trace(offset = gu::unserialize8(buf, buflen, offset, fifo_seq_));

    if (flags_ & F_SOURCE)
    {
        gu_trace(offset = source_.unserialize(buf, buflen, offset));
    }

    gu_trace(offset = source_view_id_.unserialize(buf, buflen, offset));

    return offset;
}

size_t gcomm::evs::Message::serial_size() const
{
    return (1 +                 // version | type | order
            1 +                 // flags
            2 +                 // pad
            sizeof(fifo_seq_) +  // fifo_seq
            ((flags_ & F_SOURCE) ? UUID::serial_size() : 0) +
            ViewId::serial_size()); // source_view_id
}


size_t gcomm::evs::UserMessage::serialize(gu::byte_t* const buf,
                                          size_t      const buflen,
                                          size_t            offset) const
{
    gu_trace(offset = Message::serialize(buf, buflen, offset));
    gu_trace(offset = gu::serialize1(user_type_, buf, buflen, offset));

    gcomm_assert(seq_range_ <= seqno_t(0xff));
    uint8_t b = static_cast<uint8_t>(seq_range_);
    gu_trace(offset = gu::serialize1(b, buf, buflen, offset));
    gu_trace(offset = gu::serialize2(uint16_t(0), buf, buflen, offset));
    gu_trace(offset = gu::serialize8(seq_, buf, buflen, offset));
    gu_trace(offset = gu::serialize8(aru_seq_, buf, buflen, offset));

    return offset;
}

size_t gcomm::evs::UserMessage::unserialize(const gu::byte_t* const buf,
                                            size_t            const buflen,
                                            size_t                  offset,
                                            bool                    skip_header)
{
    if (skip_header == false)
    {
        gu_trace(offset = Message::unserialize(buf, buflen, offset));
    }
    gu_trace(offset = gu::unserialize1(buf, buflen, offset, user_type_));
    uint8_t b;
    gu_trace(offset = gu::unserialize1(buf, buflen, offset, b));
    seq_range_ = b;
    uint16_t pad;
    gu_trace(offset = gu::unserialize2(buf, buflen, offset, pad));
    if (pad != 0)
    {
        log_warn << "invalid pad: " << pad;
    }
    gu_trace(offset = gu::unserialize8(buf, buflen, offset, seq_));
    gu_trace(offset = gu::unserialize8(buf, buflen, offset, aru_seq_));

    return offset;
}

size_t gcomm::evs::UserMessage::serial_size() const
{
    return Message::serial_size() + // Header
        1 +                         // User type
        1 +                         // Seq range
        2 +                         // Pad/reserved
        sizeof(seqno_t) +           // Seq
        sizeof(seqno_t);            // Aru seq

}


size_t gcomm::evs::AggregateMessage::serialize(gu::byte_t* const buf,
                                               size_t      const buflen,
                                               size_t            offset) const
{
    gu_trace(offset = gu::serialize1(flags_, buf, buflen, offset));
    gu_trace(offset = gu::serialize1(user_type_, buf, buflen, offset));
    gu_trace(offset = gu::serialize2(len_, buf, buflen, offset));
    return offset;
}


size_t gcomm::evs::AggregateMessage::unserialize(const gu::byte_t* const buf,
                                                 size_t            const buflen,
                                                 size_t                  offset)
{
    gu_trace(offset = gu::unserialize1(buf, buflen, offset, flags_));
    gu_trace(offset = gu::unserialize1(buf, buflen, offset, user_type_));
    gu_trace(offset = gu::unserialize2(buf, buflen, offset, len_));
    return offset;
}

size_t gcomm::evs::AggregateMessage::serial_size() const
{
    return sizeof(flags_)
         + sizeof(len_)
         + sizeof(user_type_);
}

size_t gcomm::evs::DelegateMessage::serialize(gu::byte_t* const buf,
                                              size_t      const buflen,
                                              size_t            offset) const
{
    gu_trace(offset = Message::serialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::DelegateMessage::unserialize(const gu::byte_t* const buf,
                                                size_t            const buflen,
                                                size_t                  offset,
                                                bool                    skip_header)
{
    if (skip_header == false)
    {
        gu_trace(offset = Message::unserialize(buf, buflen, offset));
    }
    return offset;
}

size_t gcomm::evs::DelegateMessage::serial_size() const
{
    return Message::serial_size();
}



size_t gcomm::evs::GapMessage::serialize(gu::byte_t* const buf,
                                         size_t      const buflen,
                                         size_t            offset) const
{
    gu_trace(offset = Message::serialize(buf, buflen, offset));
    gu_trace(offset = gu::serialize8(seq_, buf, buflen, offset));
    gu_trace(offset = gu::serialize8(aru_seq_, buf, buflen, offset));
    gu_trace(offset = range_uuid_.serialize(buf, buflen, offset));
    gu_trace(offset = range_.serialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::GapMessage::unserialize(const gu::byte_t* const buf,
                                           size_t            const buflen,
                                           size_t                  offset,
                                           bool                    skip_header)
{
    if (skip_header == false)
    {
        gu_trace(offset = Message::unserialize(buf, buflen, offset));
    }
    gu_trace(offset = gu::unserialize8(buf, buflen, offset, seq_));
    gu_trace(offset = gu::unserialize8(buf, buflen, offset, aru_seq_));
    gu_trace(offset = range_uuid_.unserialize(buf, buflen, offset));
    gu_trace(offset = range_.unserialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::GapMessage::serial_size() const
{
    return (Message::serial_size()
            + 2 * sizeof(seqno_t)
            + UUID::serial_size()
            + Range::serial_size());
}

size_t gcomm::evs::JoinMessage::serialize(gu::byte_t* const buf,
                                          size_t      const buflen,
                                          size_t            offset) const
{
    gu_trace(offset = Message::serialize(buf, buflen, offset));
    gu_trace(offset = gu::serialize8(seq_, buf, buflen, offset));
    gu_trace(offset = gu::serialize8(aru_seq_, buf, buflen, offset));
    gu_trace(offset = node_list_.serialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::JoinMessage::unserialize(const gu::byte_t* const buf,
                                            size_t            const buflen,
                                            size_t                  offset,
                                            bool                    skip_header)
{
    if (skip_header == false)
    {
        gu_trace(offset = Message::unserialize(buf, buflen, offset));
    }
    gu_trace(offset = gu::unserialize8(buf, buflen, offset, seq_));
    gu_trace(offset = gu::unserialize8(buf, buflen, offset, aru_seq_));
    node_list_.clear();
    gu_trace(offset = node_list_.unserialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::JoinMessage::serial_size() const
{
    return (Message::serial_size()
            + 2 * sizeof(seqno_t)
            + node_list_.serial_size());
}

size_t gcomm::evs::InstallMessage::serialize(gu::byte_t* const buf,
                                             size_t      const buflen,
                                             size_t            offset) const
{
    gu_trace(offset = Message::serialize(buf, buflen, offset));
    gu_trace(offset = gu::serialize8(seq_, buf, buflen, offset));
    gu_trace(offset = gu::serialize8(aru_seq_, buf, buflen, offset));
    gu_trace(offset = install_view_id_.serialize(buf, buflen, offset));
    gu_trace(offset = node_list_.serialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::InstallMessage::unserialize(const gu::byte_t* const buf,
                                               size_t            const buflen,
                                               size_t                  offset,
                                               bool skip_header)
{
    if (skip_header == false)
    {
        gu_trace(offset = Message::unserialize(buf, buflen, offset));
    }
    gu_trace(offset = gu::unserialize8(buf, buflen, offset, seq_));
    gu_trace(offset = gu::unserialize8(buf, buflen, offset, aru_seq_));
    gu_trace(offset = install_view_id_.unserialize(buf, buflen, offset));
    node_list_.clear();
    gu_trace(offset = node_list_.unserialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::InstallMessage::serial_size() const
{
    return (Message::serial_size()
            + 2 * sizeof(seqno_t)
            + ViewId::serial_size()
            + node_list_.serial_size());
}


size_t gcomm::evs::LeaveMessage::serialize(gu::byte_t* const buf,
                                           size_t      const buflen,
                                           size_t            offset) const
{
    gu_trace(offset = Message::serialize(buf, buflen, offset));
    gu_trace(offset = gu::serialize8(seq_, buf, buflen, offset));
    gu_trace(offset = gu::serialize8(aru_seq_, buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::LeaveMessage::unserialize(const gu::byte_t* const buf,
                                             size_t            const buflen,
                                             size_t                  offset,
                                             bool skip_header)
{
    if (skip_header == false)
    {
        gu_trace(offset = Message::unserialize(buf, buflen, offset));
    }
    gu_trace(offset = gu::unserialize8(buf, buflen, offset, seq_));
    gu_trace(offset = gu::unserialize8(buf, buflen, offset, aru_seq_));
    return offset;
}

size_t gcomm::evs::LeaveMessage::serial_size() const
{
    return (Message::serial_size() + 2 * sizeof(seqno_t));
}
