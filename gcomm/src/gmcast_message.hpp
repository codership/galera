/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "gcomm/types.hpp"
#include "gcomm/uuid.hpp"
#include "gmcast_node.hpp"

#include <list>

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
        F_HANDSHAKE_UUID = 1 << 4
    };
    
    enum Type 
    {
        T_INVALID            = 0,
        T_HANDSHAKE          = 1,
        T_HANDSHAKE_RESPONSE = 2,
        T_HANDSHAKE_OK       = 3,
        T_HANDSHAKE_FAIL     = 4,
        T_TOPOLOGY_CHANGE    = 5,
        /* Leave room for future use */
        T_USER_BASE          = 8,
        T_MAX
    };

    typedef std::list<Node> NodeList;
    
private:
    
    gu::byte_t        version;
    Type              type;
    gu::byte_t        flags;
    gcomm::UUID       handshake_uuid;
    gcomm::UUID       source_uuid;
    gcomm::String<64> node_address;
    gcomm::String<32> group_name;
    
    
    Message& operator=(const Message&);
    
    NodeList* node_list; // @todo: since we do a full node list copy in ctor
                         //        below, do we really need a pointer here?
    
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
            "RESERVED_6",
            "RESERVED_7",
            "USER_BASE"
            };
        
        if (T_MAX > t) return str[t];

        return "UNDEFINED PACKET TYPE";
    }

    Message(const Message& msg) :
        version        (msg.version),
        type           (msg.type),
        flags          (msg.flags),
        handshake_uuid (msg.handshake_uuid),
        source_uuid    (msg.source_uuid),
        node_address   (msg.node_address),
        group_name     (msg.group_name),
        node_list      (msg.node_list != 0 ? new NodeList(*msg.node_list) : 0)
    { }

    /* Default ctor */
    Message ()
        :
        version        (0),
        type           (T_INVALID),
        flags          (0),
        handshake_uuid (),
        source_uuid    (),
        node_address   (),
        group_name     (),
        node_list      (0)
    {}
    
    /* Ctor for handshake, handshake ok and handshake fail */
    Message (const Type  type_,
             const UUID& handshake_uuid_,
             const UUID& source_uuid_)
        :
        version        (0), 
        type           (type_), 
        flags          (F_HANDSHAKE_UUID), 
        handshake_uuid (handshake_uuid_),
        source_uuid    (source_uuid_), 
        node_address   (),
        group_name     (),
        node_list      (0)
    {
        if (type != T_HANDSHAKE && type != T_HANDSHAKE_OK && 
            type != T_HANDSHAKE_FAIL)
            gcomm_throw_fatal << "Invalid message type " << type_to_string(type)
                              << " in handshake constructor";        
    }
    
    /* Ctor for user message */
    Message (const Type    type_,
             const UUID&   source_uuid_, 
             const uint8_t ttl_)
        :
        version        (0), 
        type           (type_), 
        flags          (0), 
        handshake_uuid (),
        source_uuid    (source_uuid_), 
        node_address   (),
        group_name     (),
        node_list      (0)
    {
        if (type < T_USER_BASE)
            gcomm_throw_fatal << "Invalid message type " << type_to_string(type)
                              << " in user message constructor";
    }
    
    /* Ctor for handshake response */
    Message (const Type         type_,
             const gcomm::UUID& handshake_uuid_,
             const gcomm::UUID& source_uuid_,
             const std::string& node_address_,
             const std::string& group_name_)
        : 
        version        (0),
        type           (type_), 
        flags          (F_GROUP_NAME | F_NODE_ADDRESS | F_HANDSHAKE_UUID), 
        handshake_uuid (handshake_uuid_),
        source_uuid    (source_uuid_),
        node_address   (node_address_),
        group_name     (group_name_),
        node_list      (0)
        
    {
        if (type != T_HANDSHAKE_RESPONSE)
            gcomm_throw_fatal << "Invalid message type " << type_to_string(type)
                              << " in handshake response constructor";
    }

    /* Ctor for topology change */
    Message (const Type         type_, 
             const gcomm::UUID& source_uuid_,
             const std::string& group_name_,
             const NodeList&    nodes)
        :
        version        (0),
        type           (type_),
        flags          (F_GROUP_NAME | F_NODE_LIST), 
        handshake_uuid (),
        source_uuid    (source_uuid_),
        node_address   (),
        group_name     (group_name_),
        node_list      (new NodeList(nodes))
    {
        if (type != T_TOPOLOGY_CHANGE)
            gcomm_throw_fatal << "Invalid message type " << type_to_string(type)
                              << " in topology change constructor";
    }
    
    ~Message() 
    {
        delete node_list;
    }
    
    
    size_t serialize(gu::byte_t* buf, const size_t buflen, 
                     const size_t offset) const
        throw (gu::Exception)
    {
        size_t off;
        
        gu_trace (off = gcomm::serialize(version, buf, buflen, offset));
        gu_trace (off = gcomm::serialize(static_cast<gu::byte_t>(type),buf,buflen,off));
        gu_trace (off = gcomm::serialize(flags, buf, buflen, off));
        gu_trace (off = gcomm::serialize<gu::byte_t>(0, buf, buflen, off));
        gu_trace (off = source_uuid.serialize(buf, buflen, off));
        
        if (flags & F_HANDSHAKE_UUID)
        {
            gu_trace(off = handshake_uuid.serialize(buf, buflen, off));
        }

        if (flags & F_NODE_ADDRESS)
        {
            gu_trace (off = node_address.serialize(buf, buflen, off));
        }
        
        if (flags & F_GROUP_NAME) 
        {
            gu_trace (off = group_name.serialize(buf, buflen, off));
        }
        
        if (flags & F_NODE_LIST) 
        {
            gu_trace (off = gcomm::serialize(
                          static_cast<uint16_t>(node_list->size()),
                          buf, buflen, off));
            
            for (NodeList::const_iterator i = node_list->begin();
                 i != node_list->end(); ++i) 
            {
                gu_trace (off = i->serialize(buf, buflen, off));
            }
        }
        return off;
    }
    
    size_t read_v0(const gu::byte_t* buf, const size_t buflen, const size_t offset)
        throw (gu::Exception)
    {
        size_t off;
        gu::byte_t t;

        gu_trace (off = gcomm::unserialize(buf, buflen, offset, &t));
        type = static_cast<Type>(t);
        gu_trace (off = gcomm::unserialize(buf, buflen, off, &flags));
        gu_trace (off = gcomm::unserialize(buf, buflen, off, &t));
        if (t != 0)
        {
            gu_throw_error(EINVAL);
        }
        gu_trace (off = source_uuid.unserialize(buf, buflen, off));
        
        if (flags & F_HANDSHAKE_UUID)
        {
            gu_trace(off = handshake_uuid.unserialize(buf, buflen, off));
        }
        
        if (flags & F_NODE_ADDRESS)
        {
            gu_trace (off = node_address.unserialize(buf, buflen, off));
        }
        
        if (flags & F_GROUP_NAME) 
        {
            gu_trace (off = group_name.unserialize(buf, buflen, off));
        }
        
        if (flags & F_NODE_LIST)
        {
            uint16_t size;

            gu_trace (off = gcomm::unserialize(buf, buflen, off, &size));

            node_list = new NodeList(); // @todo: danger! Prev. list not deleted
            
            for (uint16_t i = 0; i < size; ++i) 
            {
                Node node;
                
                gu_trace (off = node.unserialize(buf, buflen, off));
                node_list->push_back(node);
            }
        }

        return off;
    }
    
    size_t unserialize(const gu::byte_t* buf, const size_t buflen, const size_t offset)
        throw (gu::Exception)
    {
        size_t off;
        
        gu_trace (off = gcomm::unserialize(buf, buflen, offset, &version));
        
        switch (version) {
        case 0:
            gu_trace (return read_v0(buf, buflen, off));
        default:
            return 0;
        }
    }
    
    size_t serial_size() const
    {
        return 4 /* Common header: version, type, flags, ttl */ 
            + source_uuid.serial_size()
            + (flags & F_HANDSHAKE_UUID ? handshake_uuid.serial_size() : 0)
            /* GMCast address if set */
            + (flags & F_NODE_ADDRESS ? node_address.serial_size() : 0)
            /* Group name if set */
            + (flags & F_GROUP_NAME ? group_name.serial_size() : 0)
            /* Node list if set */
            + (flags & F_NODE_LIST ? 
               2 + node_list->size()*Node::serial_size() : 0);
    }
    
    uint8_t get_version() const { return version; }
    
    Type    get_type()    const { return type;    }
    
    uint8_t get_flags()   const { return flags;   }
    
    const UUID& get_handshake_uuid() const { return handshake_uuid; }
    
    const UUID&     get_source_uuid()  const { return source_uuid;  }
    
    const std::string&   get_node_address() const { return node_address.to_string(); }
    
    const std::string&   get_group_name()   const { return group_name.to_string();   }
    
    const NodeList* get_node_list()    const { return node_list;    }
};
