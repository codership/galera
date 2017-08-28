/*
 * Copyright (C) 2009-2012 Codership Oy <info@codership.com>
 */

#ifndef PC_MESSAGE_HPP
#define PC_MESSAGE_HPP

#include "gcomm/view.hpp"
#include "gcomm/types.hpp"
#include "gcomm/uuid.hpp"
#include "gcomm/map.hpp"

#include "gu_serialize.hpp"
#include "protocol_version.hpp"

#include <limits>

namespace gcomm
{
    namespace pc
    {
        class Node;
        class NodeMap;
        class Message;
        class UserMessage;
        class StateMessage;
        class InstallMessage;
        std::ostream& operator<<(std::ostream&, const Node&);
        std::ostream& operator<<(std::ostream&, const Message&);
        bool operator==(const Message&, const Message&);
    }
}


class gcomm::pc::Node
{
public:
    enum Flags
    {
        F_PRIM    = 0x1,
        F_WEIGHT  = 0x2,
        F_UN      = 0x4,
        F_EVICTED = 0x8
    };

    Node(const bool     prim      = false,
         const bool     un        = false,
         const bool     evicted   = false,
         const uint32_t last_seq  = std::numeric_limits<uint32_t>::max(),
         const ViewId&  last_prim = ViewId(V_NON_PRIM),
         const int64_t  to_seq    = -1,
         const int      weight    = -1,
         const SegmentId segment = 0)
        :
        prim_      (prim     ),
        un_        (un       ),
        evicted_   (evicted  ),
        last_seq_  (last_seq ),
        last_prim_ (last_prim),
        to_seq_    (to_seq   ),
        weight_    (weight),
        segment_   (segment)
    { }

    void set_prim      (const bool val)          { prim_      = val      ; }
    void set_un        (const bool un)           { un_        = un       ; }
    void set_evicted   (const bool evicted)      { evicted_   = evicted  ; }
    void set_last_seq  (const uint32_t seq)      { last_seq_  = seq      ; }
    void set_last_prim (const ViewId& last_prim) { last_prim_ = last_prim; }
    void set_to_seq    (const uint64_t seq)      { to_seq_    = seq      ; }
    void set_weight    (const int weight)        { weight_    = weight   ; }
    void set_segment   (const SegmentId segment) { segment_   = segment  ; }

    bool          prim()      const { return prim_     ; }
    bool          un()        const { return un_       ; }
    bool          evicted()   const { return evicted_  ; }
    uint32_t      last_seq()  const { return last_seq_ ; }
    const ViewId& last_prim() const { return last_prim_; }
    int64_t       to_seq()    const { return to_seq_   ; }
    int           weight()    const { return weight_   ; }
    SegmentId     segment()   const { return segment_  ; }

    //
    // Serialized header
    //  0                   1                   2                   3
    //  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    // |             flags             |   segment id  |    weight     ¡
    //

    size_t unserialize(const gu::byte_t* buf, const size_t buflen, const size_t offset)
    {
        size_t   off = offset;
        uint32_t header;

        gu_trace (off = gu::unserialize4(buf, buflen, off, header));

        prim_ = header & F_PRIM;
        un_   = header & F_UN;
        if (header & F_WEIGHT)
        {
            weight_ = header >> 24;
        }
        else
        {
            weight_ = -1;
        }
        evicted_ = header & F_EVICTED;
        segment_ = (header >> 16) & 0xff;
        gu_trace (off = gu::unserialize4(buf, buflen, off, last_seq_));
        gu_trace (off = last_prim_.unserialize(buf, buflen, off));
        gu_trace (off = gu::unserialize8(buf, buflen, off, to_seq_));

        return off;
    }

    size_t serialize(gu::byte_t* buf, const size_t buflen, const size_t offset) const
    {
        size_t   off   = offset;
        uint32_t header = 0;

        header |= prim_ ? F_PRIM : 0;
        header |= un_   ? F_UN   : 0;
        if (weight_ >= 0)
        {
            header |= F_WEIGHT;
            header |= weight_ << 24;
        }
        header |= evicted_ ? F_EVICTED : 0;
        header |= static_cast<uint32_t>(segment_) << 16;
        gu_trace (off = gu::serialize4(header, buf, buflen, off));
        gu_trace (off = gu::serialize4(last_seq_, buf, buflen, off));
        gu_trace (off = last_prim_.serialize(buf, buflen, off));
        gu_trace (off = gu::serialize8(to_seq_, buf, buflen, off));

        assert (serial_size() == (off - offset));

        return off;
    }

    static size_t serial_size()
    {
        Node* node(reinterpret_cast<Node*>(0));

        //             header
        return (sizeof(uint32_t) + sizeof(node->last_seq_) +
                ViewId::serial_size() + sizeof(node->to_seq_));
    }

    bool operator==(const Node& cmp) const
    {
        return (prim()   == cmp.prim()         &&
                un()     == cmp.un()           &&
                last_seq()  == cmp.last_seq()  &&
                last_prim() == cmp.last_prim() &&
                to_seq()    == cmp.to_seq()    &&
                weight()    == cmp.weight()    &&
                segment()   == cmp.segment()     );
    }

    std::string to_string() const
    {
        std::ostringstream ret;

        ret << "prim="       << prim_
            << ",un="        << un_
            << ",last_seq="  << last_seq_
            << ",last_prim=" << last_prim_
            << ",to_seq="    << to_seq_
            << ",weight="    << weight_
            << ",segment="   << static_cast<int>(segment_);

        return ret.str();
    }

private:

    bool      prim_;      // Is node in prim comp
    bool      un_;        // The prim status of the node is unknown
    bool      evicted_;   // Node has been evicted permanently from the group
    uint32_t  last_seq_;  // Last seen message seq from the node
    ViewId    last_prim_; // Last known prim comp view id for the node
    int64_t   to_seq_;    // Last known TO seq for the node
    int       weight_;    // Node weight
    SegmentId segment_;
};


inline std::ostream& gcomm::pc::operator<<(std::ostream& os, const Node& n)
{
    return (os << n.to_string());
}


class gcomm::pc::NodeMap : public Map<UUID, Node> { };


class gcomm::pc::Message
{
public:

    enum Type {PC_T_NONE, PC_T_STATE, PC_T_INSTALL, PC_T_USER, PC_T_MAX};
    enum
    {
        F_CRC16 = 0x1,
        F_BOOTSTRAP = 0x2,
        F_WEIGHT_CHANGE = 0x4
    };

    static const char* to_string(Type t)
    {
        static const char* str[PC_T_MAX] =
            { "NONE", "STATE", "INSTALL", "USER" };

        if (t < PC_T_MAX) return str[t];

        return "unknown";
    }


    Message(const int      version  = -1,
            const Type     type     = PC_T_NONE,
            const uint32_t seq      = 0,
            const NodeMap& node_map = NodeMap())
        :
        version_ (version ),
        flags_   (0       ),
        type_    (type    ),
        seq_     (seq     ),
        crc16_   (0       ),
        node_map_(node_map)
    {
        // Note:
        // PC message wire format has room only for version numbers up to 15.
        // At version 15 (latest) the wire format must change to match
        // 8 bit version width of EVS.
        assert(version < 15);
    }

    Message(const Message& msg)
        :
        version_ (msg.version_ ),
        flags_   (msg.flags_   ),
        type_    (msg.type_    ),
        seq_     (msg.seq_     ),
        crc16_   (msg.crc16_   ),
        node_map_(msg.node_map_)
    { }

    virtual ~Message() { }


    int      version()  const { return version_; }
    Type     type()     const { return type_; }
    uint32_t seq()      const { return seq_; }

    void flags(int flags) { flags_ = flags; }
    int flags() const { return flags_; }
    void checksum(uint16_t crc16, bool flag)
    {
        crc16_ = crc16;
        if (flag == true)
        {
            flags_ |= F_CRC16;
        }
        else
        {
            flags_ &= ~F_CRC16;
        }
    }
    uint16_t checksum() const { return crc16_; }

    const NodeMap& node_map() const { return node_map_; }
    NodeMap&       node_map()       { return node_map_; }

    const Node&    node(const UUID& uuid) const
    { return NodeMap::value(node_map_.find_checked(uuid)); }
    Node& node(const UUID& uuid)
    { return NodeMap::value(node_map_.find_checked(uuid)); }

    size_t unserialize(const gu::byte_t* buf, const size_t buflen, const size_t offset)
    {
        size_t   off;
        uint32_t b;

        node_map_.clear();

        gu_trace (off = gu::unserialize4(buf, buflen, offset, b));

        version_ = b & 0x0f;
        if (version_ > GCOMM_PROTOCOL_MAX_VERSION)
            gu_throw_error (EPROTONOSUPPORT)
                << "Unsupported protocol varsion: " << version_;
        flags_   = (b & 0xf0) >> 4;

        type_ = static_cast<Type>((b >> 8) & 0xff);
        if (type_ <= PC_T_NONE || type_ >= PC_T_MAX)
            gu_throw_error (EINVAL) << "Bad type value: " << type_;

        crc16_ = ((b >> 16) & 0xffff);

        gu_trace (off = gu::unserialize4(buf, buflen, off, seq_));

        if (type_ == PC_T_STATE || type_ == PC_T_INSTALL)
        {
            gu_trace (off = node_map_.unserialize(buf, buflen, off));
        }

        return off;
    }

    size_t serialize(gu::byte_t* buf, const size_t buflen, const size_t offset) const
    {
        size_t   off;
        uint32_t b;

        b = crc16_;
        b <<= 8;
        b |= type_ & 0xff;
        b <<= 8;
        b |= version_ & 0x0f;
        b |= (flags_ << 4) & 0xf0;

        gu_trace (off = gu::serialize4(b, buf, buflen, offset));
        gu_trace (off = gu::serialize4(seq_, buf, buflen, off));


        if (type_ == PC_T_STATE || type_ == PC_T_INSTALL)
        {
            gu_trace (off = node_map_.serialize(buf, buflen, off));
        }

        assert (serial_size() == (off - offset));

        return off;
    }

    size_t serial_size() const
    {
        //            header
        return (sizeof(uint32_t)
                + sizeof(seq_)
                + (type_ == PC_T_STATE || type_ == PC_T_INSTALL  ?
                   node_map_.serial_size()                 :
                   0));
    }


    std::string to_string() const
    {
        std::ostringstream ret;

        ret << "pcmsg{ type=" << to_string(type_) << ", seq=" << seq_;
        ret << ", flags=" << std::setw(2) << std::hex << flags_;
        ret << ", node_map {" << node_map() << "}";
        ret << '}';

        return ret.str();
    }

private:
    Message& operator=(const Message&);

    int      version_;  // Message version
    int      flags_;    // Flags
    Type     type_;     // Message type
    uint32_t seq_;      // Message seqno
    uint16_t crc16_;    // 16-bit crc
    NodeMap  node_map_; // Message node map
};


inline std::ostream& gcomm::pc::operator<<(std::ostream& os, const Message& m)
{
    return (os << m.to_string());
}


class gcomm::pc::StateMessage : public Message
{
public:
    StateMessage(int version) :  Message(version, Message::PC_T_STATE, 0) {}
};


class gcomm::pc::InstallMessage : public Message
{
public:
    InstallMessage(int version) : Message(version, Message::PC_T_INSTALL, 0) {}
};


class gcomm::pc::UserMessage : public Message
{
public:
    UserMessage(int version, uint32_t seq)
        : Message(version, Message::PC_T_USER, seq) {}
};


inline bool gcomm::pc::operator==(const Message& a, const Message& b)
{
    return (a.version()  == b.version() &&
            a.checksum()     == b.checksum()    &&
            a.type()     == b.type()    &&
            a.seq()      == b.seq()     &&
            a.node_map() == b.node_map());
}


#endif // PC_MESSAGE_HPP
