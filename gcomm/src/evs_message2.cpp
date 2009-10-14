/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "evs_message2.hpp"

#include <gu_exception.hpp>

using namespace std;

ostream& gcomm::evs::operator<<(std::ostream& os, const gcomm::evs::Range &r)
{
    return (os << "[" << r.get_lu() << "," << r.get_hs() << "]");
}

ostream& gcomm::evs::operator<<(ostream& os, const gcomm::evs::MessageNode& node)
{
    os << "node{}";
    return os;
}

ostream& gcomm::evs::operator<<(ostream& os, const gcomm::evs::Message& msg)
{
    os << "evs::msg{";
    os << "version=" << msg.get_version() << ",";
    os << "type=" << msg.get_type() << ",";
    os << "user_type=" << msg.get_user_type() << ",";
    os << "safety_prefix=" << msg.get_safety_prefix() << ",";
    os << "seq=" << msg.get_seq() << ",";
    os << "seq_range=" << msg.get_seq_range() << ",";
    os << "aru_seq=" << msg.get_aru_seq() << ",";
    os << "flags=" << msg.get_flags() << ",";
    os << "source=" << msg.get_source() << ",";
    os << "source_view_id=" << msg.get_source_view_id() << ",";
    os << "range_uuid=" << msg.get_range_uuid() << ",";
    os << "range=" << msg.get_range() << ",";
    os << "fifo_seq=" << msg.get_fifo_seq();
    if (msg.has_node_list())
    {
        os << ",";
        os << "node_list=\n" << msg.get_node_list();
    }
    os << "}";
    return os;
}

size_t gcomm::evs::MessageNode::serialize(byte_t* const buf,
                                          size_t  const buflen,
                                          size_t        offset) const
    throw(gu::Exception)
{
    uint8_t b = operational;
    gu_trace(offset = gcomm::serialize(b, buf, buflen, offset));
    b = leaving;
    gu_trace(offset = gcomm::serialize(b, buf, buflen, offset));
    uint16_t pad(0);
    gu_trace(offset = gcomm::serialize(pad, buf, buflen, offset));
    gu_trace(offset = current_view.serialize(buf, buflen, offset));
    gu_trace(offset = safe_seq.serialize(buf, buflen, offset));
    gu_trace(offset = im_range.serialize(buf, buflen, offset));    
    return offset;
}


size_t gcomm::evs::MessageNode::unserialize(const byte_t* const buf,
                                            size_t        const buflen,
                                            size_t              offset)
    throw(gu::Exception)
{
    uint8_t b;
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &b));
    if (not (b == 0 || b == 1))
    {
        gcomm_throw_runtime(EINVAL) << "invalid operational flag " << b;
    }
    operational = b;
    
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &b));
    if (not (b == 0 || b == 1))
    {
        gcomm_throw_runtime(EINVAL) << "invalid leaving flag " << b;
    }
    leaving = b;
    
    uint16_t pad(0);
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &pad));
    if (pad != 0)
    {
        gcomm_throw_runtime(EINVAL) << "invalid pad" << pad;
    }
    gu_trace(offset = current_view.unserialize(buf, buflen, offset));
    gu_trace(offset = safe_seq.unserialize(buf, buflen, offset));
    gu_trace(offset = im_range.unserialize(buf, buflen, offset));    
    return offset;
}

size_t gcomm::evs::MessageNode::serial_size()
{
    return 4 +                  // 4 bytes reserved for flags
        ViewId::serial_size() + 
        Seqno::serial_size() + 
        Range::serial_size();
}


bool gcomm::evs::Message::operator==(const Message& cmp) const
{
    return version == cmp.version &&
        type == cmp.type &&
        user_type == cmp.user_type &&
        safety_prefix == cmp.safety_prefix &&
        seq == cmp.seq &&
        seq_range == cmp.seq_range &&
        aru_seq == cmp.aru_seq &&
        fifo_seq == cmp.fifo_seq &&
        flags == cmp.flags &&
        source == cmp.source &&
        source_view_id == cmp.source_view_id &&
        range_uuid == cmp.range_uuid &&
        range == cmp.range &&
        (node_list != 0 ? 
         ( cmp.node_list != 0 && 
           *node_list == *cmp.node_list ) : 
         true);
}


size_t gcomm::evs::Message::serialize(byte_t* const buf, 
                                      size_t  const buflen,
                                      size_t        offset) const 
    throw(gu::Exception)
{
    
    uint8_t b = static_cast<uint8_t>(version | (type << 2) | (safety_prefix << 5));
    gu_trace(offset = gcomm::serialize(b, buf, buflen, offset));
    gu_trace(offset = gcomm::serialize(flags, buf, buflen, offset));
    uint16_t pad(0);
    gu_trace(offset = gcomm::serialize(pad, buf, buflen, offset));
    // Source is not serialized, it should be got from underlying
    // transport
    gcomm_assert(not (flags & F_SOURCE));
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
        gcomm_throw_runtime(EINVAL) << "invalid version " << version;
    }
    
    type    = static_cast<Type>((b >> 2) & 0x7);
    if (type <= T_NONE || type > T_LEAVE)
    {
        gcomm_throw_runtime(EINVAL) << "invalid type " << type;
    }
    
    safety_prefix = static_cast<SafetyPrefix>((b >> 5) & 0x7);
    if (safety_prefix < SP_DROP || safety_prefix > SP_SAFE)
    {
        gcomm_throw_runtime(EINVAL) << "invalid safety prefix " 
                                    << safety_prefix;
    }
    
    uint16_t pad;
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &pad));
    if (pad != 0)
    {
        gcomm_throw_runtime(EINVAL) << "invalid pad" << pad;
    }
    
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &flags));
    if (flags & F_SOURCE)
    {
        gcomm_throw_runtime(EMSGSIZE) << "invalid flags " << flags;
    }
    
    gu_trace(offset = source_view_id.unserialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::Message::serial_size() const
{
    return (1 +                // version | type | safety_prefix
            1 +              // flags
            2 +              // pad
            ViewId::serial_size()); // source_view_id
}


size_t gcomm::evs::UserMessage::serialize(byte_t* const buf,
                                          size_t  const buflen,
                                          size_t        offset) const
    throw(gu::Exception)
{
    gu_trace(offset = Message::serialize(buf, buflen, offset));
    gu_trace(offset = gcomm::serialize(user_type, buf, buflen, offset));
    
    gcomm_assert(seq_range <= 0xff);
    uint8_t b = static_cast<uint8_t>(seq_range.get());
    gu_trace(offset = gcomm::serialize(b, buf, buflen, offset));
    gu_trace(offset = seq.serialize(buf, buflen, offset));
    gu_trace(offset = aru_seq.serialize(buf, buflen, offset));
    
    return offset;
}

size_t gcomm::evs::UserMessage::unserialize(const byte_t* const buf,
                                            size_t        const buflen,
                                            size_t              offset)
    throw(gu::Exception)
{
    gu_trace(offset = Message::unserialize(buf, buflen, offset));
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &user_type));
    uint8_t b;
    gu_trace(offset = gcomm::unserialize(buf, buflen, offset, &b));
    seq_range = b;
    gu_trace(offset = seq.unserialize(buf, buflen, offset));
    gu_trace(offset = aru_seq.unserialize(buf, buflen, offset));

    return offset;
}

size_t gcomm::evs::UserMessage::serial_size() const
{
    return Message::serial_size() + // Header
        1 +                         // User type
        1 +                         // Seq range
        2 +                         // Seq
        2;                          // Aru seq
        
}

size_t gcomm::evs::GapMessage::serialize(byte_t* const buf,
                                         size_t  const buflen,
                                         size_t        offset) const
    throw(gu::Exception)
{
    gu_trace(offset = Message::serialize(buf, buflen, offset));
    gu_trace(offset = seq.serialize(buf, buflen, offset));
    gu_trace(offset = aru_seq.serialize(buf, buflen, offset));
    gu_trace(offset = range_uuid.serialize(buf, buflen, offset));
    gu_trace(offset = range.serialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::GapMessage::unserialize(const byte_t* const buf,
                                           size_t        const buflen,
                                           size_t              offset)
    throw(gu::Exception)
{
    gu_trace(offset = Message::unserialize(buf, buflen, offset));
    gu_trace(offset = seq.unserialize(buf, buflen, offset));
    gu_trace(offset = aru_seq.unserialize(buf, buflen, offset));
    gu_trace(offset = range_uuid.unserialize(buf, buflen, offset));
    gu_trace(offset = range.unserialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::GapMessage::serial_size() const
{
    return (Message::serial_size()
            + 2*Seqno::serial_size()
            + UUID::serial_size() 
            + Range::serial_size());
}

size_t gcomm::evs::JoinMessage::serialize(byte_t* const buf,
                                         size_t  const buflen,
                                         size_t        offset) const
    throw(gu::Exception)
{
    gu_trace(offset = Message::serialize(buf, buflen, offset));
    gu_trace(offset = seq.serialize(buf, buflen, offset));
    gu_trace(offset = aru_seq.serialize(buf, buflen, offset));
    gu_trace(offset = node_list->serialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::JoinMessage::unserialize(const byte_t* const buf,
                                           size_t        const buflen,
                                           size_t              offset)
    throw(gu::Exception)
{
    gu_trace(offset = Message::unserialize(buf, buflen, offset));
    gu_trace(offset = seq.unserialize(buf, buflen, offset));
    gu_trace(offset = aru_seq.unserialize(buf, buflen, offset));
    gu_trace(offset = node_list->unserialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::JoinMessage::serial_size() const
{
    return (Message::serial_size()
            + 2*Seqno::serial_size()
            + node_list->serial_size());
}

size_t gcomm::evs::InstallMessage::serialize(byte_t* const buf,
                                         size_t  const buflen,
                                         size_t        offset) const
    throw(gu::Exception)
{
    gu_trace(offset = Message::serialize(buf, buflen, offset));
    gu_trace(offset = seq.serialize(buf, buflen, offset));
    gu_trace(offset = aru_seq.serialize(buf, buflen, offset));
    gu_trace(offset = node_list->serialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::InstallMessage::unserialize(const byte_t* const buf,
                                           size_t        const buflen,
                                           size_t              offset)
    throw(gu::Exception)
{
    gu_trace(offset = Message::unserialize(buf, buflen, offset));
    gu_trace(offset = seq.unserialize(buf, buflen, offset));
    gu_trace(offset = aru_seq.unserialize(buf, buflen, offset));
    gu_trace(offset = node_list->unserialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::InstallMessage::serial_size() const
{
    return (Message::serial_size()
            + 2*Seqno::serial_size()
            + node_list->serial_size());
}


size_t gcomm::evs::LeaveMessage::serialize(byte_t* const buf,
                                         size_t  const buflen,
                                         size_t        offset) const
    throw(gu::Exception)
{
    gu_trace(offset = Message::serialize(buf, buflen, offset));
    gu_trace(offset = seq.serialize(buf, buflen, offset));
    gu_trace(offset = aru_seq.serialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::LeaveMessage::unserialize(const byte_t* const buf,
                                             size_t        const buflen,
                                             size_t              offset)
    throw(gu::Exception)
{
    gu_trace(offset = Message::unserialize(buf, buflen, offset));
    gu_trace(offset = seq.unserialize(buf, buflen, offset));
    gu_trace(offset = aru_seq.unserialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::evs::LeaveMessage::serial_size() const
{
    return (Message::serial_size() + 2*Seqno::serial_size());
}
