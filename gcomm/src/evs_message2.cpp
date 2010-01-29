/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "evs_message2.hpp"

#include "gu_exception.hpp"
#include "gu_logger.hpp"

using namespace std;
using namespace std::rel_ops;

using namespace gu;

ostream& gcomm::evs::operator<<(ostream& os, const gcomm::evs::MessageNode& node)
{
    os << "node: {";
    os << "operational=" << node.get_operational() << ",";
    os << "suspected=" << node.get_suspected() << ",";
    os << "leave_seq=" << node.get_leave_seq() << ",";
    os << "view_id=" << node.get_view_id() << ",";
    os << "safe_seq=" << node.get_safe_seq() << ",";
    os << "im_range=" << node.get_im_range() << ",";
    os << "}";
    return os;
}

ostream& gcomm::evs::operator<<(ostream& os, const gcomm::evs::Message& msg)
{
    os << "evs::msg{";
    os << "version=" << static_cast<int>(msg.get_version()) << ",";
    os << "type=" << msg.get_type() << ",";
    os << "user_type=" << static_cast<int>(msg.get_user_type()) << ",";
    os << "order=" << msg.get_order() << ",";
    os << "seq=" << msg.get_seq() << ",";
    os << "seq_range=" << msg.get_seq_range() << ",";
    os << "aru_seq=" << msg.get_aru_seq() << ",";
    os << "flags=" << static_cast<int>(msg.get_flags()) << ",";
    os << "source=" << msg.get_source() << ",";
    os << "source_view_id=" << msg.get_source_view_id() << ",";
    os << "range_uuid=" << msg.get_range_uuid() << ",";
    os << "range=" << msg.get_range() << ",";
    os << "fifo_seq=" << msg.get_fifo_seq() << ",";
    os << "node_list=(" << msg.get_node_list() << ") ";
    os << "}";
    return os;
}

size_t gcomm::evs::MessageNode::serialize(byte_t* const buf,
                                          size_t  const buflen,
                                          size_t        offset) const
    throw(gu::Exception)
{
    uint8_t b = 
        static_cast<uint8_t>((operational_ == true ? F_OPERATIONAL : 0) | 
                             (suspected_   == true ? F_SUSPECTED   : 0));
    gu_trace(offset = gcomm::serialize(b, buf, buflen, offset));
    uint8_t pad(0);
    gu_trace(offset = gcomm::serialize(pad, buf, buflen, offset));
    gu_trace(offset = gcomm::serialize(leave_seq_, buf, buflen, offset));
    gu_trace(offset = view_id_.serialize(buf, buflen, offset));
    gu_trace(offset = gcomm::serialize(safe_seq_, buf, buflen, offset));
    gu_trace(offset = im_range_.serialize(buf, buflen, offset));    
    return offset;
}


size_t gcomm::evs::MessageNode::unserialize(const byte_t* const buf,
                                            size_t        const buflen,
                                            size_t              offset)
    throw(gu::Exception)
{
    uint8_t b;
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &b));
    if ((b & ~(F_OPERATIONAL | F_SUSPECTED)) != 0)
    {
        log_warn << "unknown flags: " << static_cast<int>(b);
    }
    operational_ = b & F_OPERATIONAL;
    suspected_   = b & F_SUSPECTED;
    
    uint8_t pad(0);
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &pad));
    if (pad != 0)
    {
        gu_throw_error(EINVAL) << "invalid pad" << pad;
    }
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &leave_seq_));
    gu_trace(offset = view_id_.unserialize(buf, buflen, offset));
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &safe_seq_));
    gu_trace(offset = im_range_.unserialize(buf, buflen, offset));    
    return offset;
}

size_t gcomm::evs::MessageNode::serial_size()
{
    return 2 +                  // 4 bytes reserved for flags
        gcomm::serial_size(seqno_t()) +
        ViewId::serial_size() + 
        gcomm::serial_size(seqno_t()) + 
        Range::serial_size();
}


bool gcomm::evs::Message::operator==(const Message& cmp) const
{
    return (version         == cmp.version         &&
            type            == cmp.type            &&
            user_type       == cmp.user_type       &&
            order           == cmp.order           &&
            seq             == cmp.seq             &&
            seq_range       == cmp.seq_range       &&
            aru_seq         == cmp.aru_seq         &&
            fifo_seq        == cmp.fifo_seq        &&
            flags           == cmp.flags           &&
            source          == cmp.source          &&
            source_view_id  == cmp.source_view_id  &&
            install_view_id == cmp.install_view_id &&
            range_uuid      == cmp.range_uuid      &&
            range           == cmp.range           &&
            node_list       == cmp.node_list);
}


size_t gcomm::evs::Message::serialize(byte_t* const buf, 
                                      size_t  const buflen,
                                      size_t        offset) const 
    throw(gu::Exception)
{
    
    uint8_t b = static_cast<uint8_t>(version | (type << 2) | (order << 5));
    gu_trace(offset = gcomm::serialize(b, buf, buflen, offset));
    gu_trace(offset = gcomm::serialize(flags, buf, buflen, offset));
    uint16_t pad(0);
    gu_trace(offset = gcomm::serialize(pad, buf, buflen, offset));
    gu_trace(offset = gcomm::serialize(fifo_seq, buf, buflen, offset));
    if (flags & F_SOURCE)
    {
        gu_trace(offset = source.serialize(buf, buflen, offset));
    }
    gu_trace(offset = source_view_id.serialize(buf, buflen, offset));
    return offset;
}


size_t gcomm::evs::Message::unserialize(const byte_t* const buf, 
                                        size_t        const buflen,
                                        size_t              offset)
    throw(gu::Exception)
{
    uint8_t b;
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &b));
    
    version = static_cast<uint8_t>(b & 0x3);
    if (version != 0)
    {
        gu_throw_error(EINVAL) << "invalid version " << version;
    }
    
    type    = static_cast<Type>((b >> 2) & 0x7);
    if (type <= T_NONE || type > T_LEAVE)
    {
        gu_throw_error(EINVAL) << "invalid type " << type;
    }
    
    order = static_cast<Order>((b >> 5) & 0x7);
    if (order < O_DROP || order > O_SAFE)
    {
        gu_throw_error(EINVAL) << "invalid safety prefix " 
                                    << order;
    }
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &flags));

    uint16_t pad;
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &pad));
    if (pad != 0)
    {
        gu_throw_error(EINVAL) << "invalid pad" << pad;
    }
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &fifo_seq));
    if (flags & F_SOURCE)
    {
        gu_trace(offset = source.unserialize(buf, buflen, offset));
    }
    
    gu_trace(offset = source_view_id.unserialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::Message::serial_size() const
{
    return (1 +                // version | type | order
            1 +              // flags
            2 +              // pad
            sizeof(fifo_seq) +  // fifo_seq
            ((flags & F_SOURCE) ? UUID::serial_size() : 0) +
            ViewId::serial_size()); // source_view_id
}


size_t gcomm::evs::UserMessage::serialize(byte_t* const buf,
                                          size_t  const buflen,
                                          size_t        offset) const
    throw(gu::Exception)
{
    gu_trace(offset = Message::serialize(buf, buflen, offset));
    gu_trace(offset = gcomm::serialize(user_type, buf, buflen, offset));
    
    gcomm_assert(seq_range <= seqno_t(0xff));
    uint8_t b = static_cast<uint8_t>(seq_range);
    gu_trace(offset = gcomm::serialize(b, buf, buflen, offset));
    gu_trace(offset = gcomm::serialize(uint16_t(0), buf, buflen, offset));
    gu_trace(offset = gcomm::serialize(seq, buf, buflen, offset));
    gu_trace(offset = gcomm::serialize(aru_seq, buf, buflen, offset));
    
    return offset;
}

size_t gcomm::evs::UserMessage::unserialize(const byte_t* const buf,
                                            size_t        const buflen,
                                            size_t              offset,
                                            bool                skip_header)
    throw(gu::Exception)
{
    if (skip_header == false)
    {
        gu_trace(offset = Message::unserialize(buf, buflen, offset));
    }
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &user_type));
    uint8_t b;
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &b));
    seq_range = b;
    uint16_t pad;
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &pad));
    if (pad != 0)
    {
        log_warn << "invalid pad: " << pad;
    }
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &seq));
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &aru_seq));
    
    return offset;
}

size_t gcomm::evs::UserMessage::serial_size() const
{
    return Message::serial_size() +     // Header
        1 +                             // User type
        1 +                             // Seq range
        2 +                             // Pad/reserved
        gcomm::serial_size(seqno_t()) + // Seq
        gcomm::serial_size(seqno_t());  // Aru seq
        
}


size_t gcomm::evs::AggregateMessage::serialize(byte_t* const buf,
                                               size_t  const buflen,
                                               size_t        offset) const
    throw (gu::Exception)
{
    gu_trace(offset = gcomm::serialize(flags_, buf, buflen, offset));
    gu_trace(offset = gcomm::serialize(user_type_, buf, buflen, offset));
    gu_trace(offset = gcomm::serialize(len_, buf, buflen, offset));
    return offset;
}


size_t gcomm::evs::AggregateMessage::unserialize(const byte_t* const buf,
                                                 size_t const buflen,
                                                 size_t       offset)
    throw (gu::Exception)
{
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &flags_));
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &user_type_));
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &len_));
    return offset;
}

size_t gcomm::evs::AggregateMessage::serial_size() const
{
    return gcomm::serial_size(flags_) 
        + gcomm::serial_size(len_) 
        + gcomm::serial_size(user_type_);
}

size_t gcomm::evs::DelegateMessage::serialize(byte_t* const buf,
                                              size_t  const buflen,
                                              size_t        offset) const
    throw(gu::Exception)
{
    gu_trace(offset = Message::serialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::DelegateMessage::unserialize(const byte_t* const buf,
                                                size_t        const buflen,
                                                size_t              offset,
                                                bool                skip_header)
    throw(gu::Exception)
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



size_t gcomm::evs::GapMessage::serialize(byte_t* const buf,
                                         size_t  const buflen,
                                         size_t        offset) const
    throw(gu::Exception)
{
    gu_trace(offset = Message::serialize(buf, buflen, offset));
    gu_trace(offset = gcomm::serialize(seq, buf, buflen, offset));
    gu_trace(offset = gcomm::serialize(aru_seq, buf, buflen, offset));
    gu_trace(offset = range_uuid.serialize(buf, buflen, offset));
    gu_trace(offset = range.serialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::GapMessage::unserialize(const byte_t* const buf,
                                           size_t        const buflen,
                                           size_t              offset,
                                           bool                skip_header)
    throw(gu::Exception)
{
    if (skip_header == false)
    {
        gu_trace(offset = Message::unserialize(buf, buflen, offset));
    }
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &seq));
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &aru_seq));
    gu_trace(offset = range_uuid.unserialize(buf, buflen, offset));
    gu_trace(offset = range.unserialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::GapMessage::serial_size() const
{
    return (Message::serial_size()
            + 2 * gcomm::serial_size(seqno_t())
            + UUID::serial_size() 
            + Range::serial_size());
}

size_t gcomm::evs::JoinMessage::serialize(byte_t* const buf,
                                          size_t  const buflen,
                                          size_t        offset) const
    throw(gu::Exception)
{
    gu_trace(offset = Message::serialize(buf, buflen, offset));
    gu_trace(offset = gcomm::serialize(seq, buf, buflen, offset));
    gu_trace(offset = gcomm::serialize(aru_seq, buf, buflen, offset));
    gu_trace(offset = node_list.serialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::JoinMessage::unserialize(const byte_t* const buf,
                                            size_t        const buflen,
                                            size_t              offset,
                                            bool                skip_header)
    throw(gu::Exception)
{
    if (skip_header == false)
    {
        gu_trace(offset = Message::unserialize(buf, buflen, offset));
    }
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &seq));
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &aru_seq));
    node_list.clear();
    gu_trace(offset = node_list.unserialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::JoinMessage::serial_size() const
{
    return (Message::serial_size()
            + 2 * gcomm::serial_size(seqno_t())
            + node_list.serial_size());
}

size_t gcomm::evs::InstallMessage::serialize(byte_t* const buf,
                                             size_t  const buflen,
                                             size_t        offset) const
    throw(gu::Exception)
{
    gu_trace(offset = Message::serialize(buf, buflen, offset));
    gu_trace(offset = gcomm::serialize(seq, buf, buflen, offset));
    gu_trace(offset = gcomm::serialize(aru_seq, buf, buflen, offset));
    gu_trace(offset = install_view_id.serialize(buf, buflen, offset));
    gu_trace(offset = node_list.serialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::InstallMessage::unserialize(const byte_t* const buf,
                                           size_t        const buflen,
                                               size_t              offset,
                                               bool skip_header)
    throw(gu::Exception)
{
    if (skip_header == false)
    {
        gu_trace(offset = Message::unserialize(buf, buflen, offset));
    }
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &seq));
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &aru_seq));
    gu_trace(offset = install_view_id.unserialize(buf, buflen, offset));
    node_list.clear();
    gu_trace(offset = node_list.unserialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::InstallMessage::serial_size() const
{
    return (Message::serial_size()
            + 2 * gcomm::serial_size(seqno_t())
            + ViewId::serial_size()
            + node_list.serial_size());
}


size_t gcomm::evs::LeaveMessage::serialize(byte_t* const buf,
                                           size_t  const buflen,
                                           size_t        offset) const
    throw(gu::Exception)
{
    gu_trace(offset = Message::serialize(buf, buflen, offset));
    gu_trace(offset = gcomm::serialize(seq, buf, buflen, offset));
    gu_trace(offset = gcomm::serialize(aru_seq, buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::LeaveMessage::unserialize(const byte_t* const buf,
                                             size_t        const buflen,
                                             size_t              offset,
                                             bool skip_header)
    throw(gu::Exception)
{
    if (skip_header == false)
    {
        gu_trace(offset = Message::unserialize(buf, buflen, offset));
    }
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &seq));
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &aru_seq));
    return offset;
}

size_t gcomm::evs::LeaveMessage::serial_size() const
{
    return (Message::serial_size() + 2 * gcomm::serial_size(seqno_t()));
}
