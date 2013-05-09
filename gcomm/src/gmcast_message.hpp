/*
 * Copyright (C) 2009-2012 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_GMCAST_MESSAGE_HPP
#define GCOMM_GMCAST_MESSAGE_HPP

#include "gcomm/types.hpp"
#include "gcomm/uuid.hpp"
#include "gmcast_node.hpp"
#include "gcomm/map.hpp"

namespace gcomm
{
    namespace gmcast
    {
        class Message;
    }
}


class gcomm::gmcast::Message
{
public:

    enum Flags {
        F_GROUP_NAME     = 1 << 0,
        F_NODE_NAME      = 1 << 1,
        F_NODE_ADDRESS   = 1 << 2,
        F_NODE_LIST      = 1 << 3,
        F_HANDSHAKE_UUID = 1 << 4,
        // relay message to all peers in the same segment (excluding source)
        // and to all other segments except source segment
        F_RELAY          = 1 << 5,
        // relay message to all peers in the same segment
        F_SEGMENT_RELAY  = 1 << 6
    };

    enum Type
    {
        T_INVALID            = 0,
        T_HANDSHAKE          = 1,
        T_HANDSHAKE_RESPONSE = 2,
        T_OK                 = 3,
        T_FAIL               = 4,
        T_TOPOLOGY_CHANGE    = 5,
        T_KEEPALIVE          = 6,
        /* Leave room for future use */
        T_USER_BASE          = 8,
        T_MAX                = 255
    };

    class NodeList : public Map<UUID, Node> { };

private:

    gu::byte_t        version_;
    Type              type_;
    gu::byte_t        flags_;
    gu::byte_t        segment_id_;
    gcomm::UUID       handshake_uuid_;
    gcomm::UUID       source_uuid_;
    gcomm::String<64> node_address_;
    gcomm::String<32> group_name_;


    Message& operator=(const Message&);

    NodeList node_list_;
public:

    static const char* type_to_string (Type t)
    {
        static const char* str[T_MAX] =
            {
            "INVALID",
            "HANDSHAKE",
            "HANDSHAKE_RESPONSE",
            "HANDSHAKE_OK",
            "HANDSHAKE_FAIL",
            "TOPOLOGY_CHANGE",
            "KEEPALIVE",
            "RESERVED_7",
            "USER_BASE"
            };

        if (T_MAX > t) return str[t];

        return "UNDEFINED PACKET TYPE";
    }

    Message(const Message& msg) :
        version_        (msg.version_),
        type_           (msg.type_),
        flags_          (msg.flags_),
        segment_id_     (msg.segment_id_),
        handshake_uuid_ (msg.handshake_uuid_),
        source_uuid_    (msg.source_uuid_),
        node_address_   (msg.node_address_),
        group_name_     (msg.group_name_),
        node_list_      (msg.node_list_)
    { }

    /* Default ctor */
    Message ()
        :
        version_        (0),
        type_           (T_INVALID),
        flags_          (0),
        segment_id_     (0),
        handshake_uuid_ (),
        source_uuid_    (),
        node_address_   (),
        group_name_     (),
        node_list_      ()
    {}

    /* Ctor for handshake */
    Message (int v,
             const Type  type,
             const UUID& handshake_uuid,
             const UUID& source_uuid,
             uint8_t     segment_id)
        :
        version_        (v),
        type_           (type),
        flags_          (F_HANDSHAKE_UUID),
        segment_id_     (segment_id),
        handshake_uuid_ (handshake_uuid),
        source_uuid_    (source_uuid),
        node_address_   (),
        group_name_     (),
        node_list_      ()
    {
        if (type_ != T_HANDSHAKE)
            gu_throw_fatal << "Invalid message type " << type_to_string(type_)
                           << " in handshake constructor";
    }

    /* ok, fail and keepalive */
    Message (int v,
             const Type  type,
             const UUID& source_uuid,
             uint8_t     segment_id)
        :
        version_        (v),
        type_           (type),
        flags_          (),
        segment_id_     (segment_id),
        handshake_uuid_ (),
        source_uuid_    (source_uuid),
        node_address_   (),
        group_name_     (),
        node_list_      ()
    {
        if (type_ != T_OK && type_ != T_FAIL && type_ != T_KEEPALIVE)
            gu_throw_fatal << "Invalid message type " << type_to_string(type_)
                              << " in ok/fail/keepalive constructor";
    }


    /* Ctor for user message */
    Message (int v,
             const Type    type,
             const UUID&   source_uuid,
             const int     ttl,
             uint8_t       segment)
        :
        version_        (v),
        type_           (type),
        flags_          (0),
        segment_id_     (segment),
        handshake_uuid_ (),
        source_uuid_    (source_uuid),
        node_address_   (),
        group_name_     (),
        node_list_      ()
    {
        if (type_ < T_USER_BASE)
            gu_throw_fatal << "Invalid message type " << type_to_string(type_)
                              << " in user message constructor";
    }

    /* Ctor for handshake response */
    Message (int v,
             const Type         type,
             const gcomm::UUID& handshake_uuid,
             const gcomm::UUID& source_uuid,
             const std::string& node_address,
             const std::string& group_name,
             uint8_t            segment_id)
        :
        version_        (v),
        type_           (type),
        flags_          (F_GROUP_NAME | F_NODE_ADDRESS | F_HANDSHAKE_UUID),
        segment_id_     (segment_id),
        handshake_uuid_ (handshake_uuid),
        source_uuid_    (source_uuid),
        node_address_   (node_address),
        group_name_     (group_name),
        node_list_      ()

    {
        if (type_ != T_HANDSHAKE_RESPONSE)
            gu_throw_fatal << "Invalid message type " << type_to_string(type_)
                           << " in handshake response constructor";
    }

    /* Ctor for topology change */
    Message (int v,
             const Type         type,
             const gcomm::UUID& source_uuid,
             const std::string& group_name,
             const NodeList&    nodes)
        :
        version_        (v),
        type_           (type),
        flags_          (F_GROUP_NAME | F_NODE_LIST),
        segment_id_     (0),
        handshake_uuid_ (),
        source_uuid_    (source_uuid),
        node_address_   (),
        group_name_     (group_name),
        node_list_      (nodes)
    {
        if (type_ != T_TOPOLOGY_CHANGE)
            gu_throw_fatal << "Invalid message type " << type_to_string(type_)
                              << " in topology change constructor";
    }

    ~Message() { }


    size_t serialize(gu::byte_t* buf, const size_t buflen,
                     const size_t offset) const
    {
        size_t off;

        gu_trace (off = gu::serialize1(version_, buf, buflen, offset));
        gu_trace (off = gu::serialize1(static_cast<gu::byte_t>(type_),buf,buflen,off));
        gu_trace (off = gu::serialize1(flags_, buf, buflen, off));
        gu_trace (off = gu::serialize1(segment_id_, buf, buflen, off));
        gu_trace (off = source_uuid_.serialize(buf, buflen, off));

        if (flags_ & F_HANDSHAKE_UUID)
        {
            gu_trace(off = handshake_uuid_.serialize(buf, buflen, off));
        }

        if (flags_ & F_NODE_ADDRESS)
        {
            gu_trace (off = node_address_.serialize(buf, buflen, off));
        }

        if (flags_ & F_GROUP_NAME)
        {
            gu_trace (off = group_name_.serialize(buf, buflen, off));
        }

        if (flags_ & F_NODE_LIST)
        {
            gu_trace(off = node_list_.serialize(buf, buflen, off));
        }
        return off;
    }

    size_t read_v0(const gu::byte_t* buf, const size_t buflen, const size_t offset)
    {
        size_t off;
        gu::byte_t t;

        gu_trace (off = gu::unserialize1(buf, buflen, offset, t));
        type_ = static_cast<Type>(t);
        switch (type_)
        {
        case T_HANDSHAKE:
        case T_HANDSHAKE_RESPONSE:
        case T_OK:
        case T_FAIL:
        case T_TOPOLOGY_CHANGE:
        case T_KEEPALIVE:
        case T_USER_BASE:
            break;
        default:
            gu_throw_error(EINVAL) << "invalid message type "
                                   << static_cast<int>(type_);
        }
        gu_trace (off = gu::unserialize1(buf, buflen, off, flags_));
        gu_trace (off = gu::unserialize1(buf, buflen, off, segment_id_));
        gu_trace (off = source_uuid_.unserialize(buf, buflen, off));

        if (flags_ & F_HANDSHAKE_UUID)
        {
            gu_trace(off = handshake_uuid_.unserialize(buf, buflen, off));
        }

        if (flags_ & F_NODE_ADDRESS)
        {
            gu_trace (off = node_address_.unserialize(buf, buflen, off));
        }

        if (flags_ & F_GROUP_NAME)
        {
            gu_trace (off = group_name_.unserialize(buf, buflen, off));
        }

        if (flags_ & F_NODE_LIST)
        {
            gu_trace(off = node_list_.unserialize(buf, buflen, off));
        }

        return off;
    }

    size_t unserialize(const gu::byte_t* buf, const size_t buflen, const size_t offset)
    {
        size_t off;

        gu_trace (off = gu::unserialize1(buf, buflen, offset, version_));

        switch (version_) {
        case 0:
            gu_trace (return read_v0(buf, buflen, off));
        default:
            gu_throw_error(EPROTONOSUPPORT) << "Unsupported/unrecognized gmcast protocol version: " << version_;
        }
    }

    size_t serial_size() const
    {
        return 4 /* Common header: version, type, flags, segment_id */
            + source_uuid_.serial_size()
            + (flags_ & F_HANDSHAKE_UUID ? handshake_uuid_.serial_size() : 0)
            /* GMCast address if set */
            + (flags_ & F_NODE_ADDRESS ? node_address_.serial_size() : 0)
            /* Group name if set */
            + (flags_ & F_GROUP_NAME ? group_name_.serial_size() : 0)
            /* Node list if set */
            + (flags_ & F_NODE_LIST ? node_list_.serial_size() : 0);
    }

    int version() const { return version_; }

    Type    type()    const { return type_;    }

    void set_flags(uint8_t f) { flags_ = f; }
    uint8_t flags()   const { return flags_;   }
    uint8_t segment_id() const { return segment_id_; }

    const UUID& handshake_uuid() const { return handshake_uuid_; }

    const UUID&     source_uuid()  const { return source_uuid_;  }

    const std::string&   node_address() const { return node_address_.to_string(); }

    const std::string&   group_name()   const { return group_name_.to_string();   }

    const NodeList& node_list()    const { return node_list_;    }
};

#endif // GCOMM_GMCAST_MESSAGE_HPP
