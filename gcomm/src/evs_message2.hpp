/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef EVS_MESSAGE2_HPP
#define EVS_MESSAGE2_HPP


#include "gcomm/safety_prefix.hpp"
#include "gcomm/view.hpp"
#include "gcomm/time.hpp"
#include "gcomm/map.hpp"

#include "evs_seqno.hpp"

namespace gcomm
{
    namespace evs
    {
        class Range;
        class MessageNode;
        class MessageNodeList;
        class Message;
        
        class UserMessage;
        class DelegateMessage;
        class GapMessage;
        class JoinMessage;
        class LeaveMessage;
        class InstallMessage;
    }
}

/*!
 *
 */
class gcomm::evs::Range
{
public:
    Range(const Seqno lu_ = Seqno::max(), const Seqno hs_ = Seqno::max()) :
        lu(lu_),
        hs(hs_)
    {}
    Seqno get_lu() const { return lu; }
    Seqno get_hs() const { return hs; }
    
    void set_lu(const Seqno s) { lu = s; }
    void set_hs(const Seqno s) { hs = s; }
    
    size_t serialize(byte_t* buf, size_t buflen, size_t offset) const
    {
        gu_trace(offset = lu.serialize(buf, buflen, offset));
        gu_trace(offset = hs.serialize(buf, buflen, offset));
        return offset;
    }
    
    size_t unserialize(const byte_t* buf, size_t buflen, size_t offset)
    {
        gu_trace(offset = lu.unserialize(buf, buflen, offset));
        gu_trace(offset = hs.unserialize(buf, buflen, offset));
        return offset;
    }

    static size_t serial_size()
    {
        return 2*Seqno::serial_size();
    }

private:
    Seqno lu; /*!< Lowest unseen seqno */
    Seqno hs; /*!< Highest seen seqno  */
};

inline std::ostream& operator<<(std::ostream& os, const gcomm::evs::Range &r)
{
    return (os << "[" << r.get_lu() << "," << r.get_hs() << "]");
}

class gcomm::evs::MessageNode
{
    
};

class gcomm::evs::MessageNodeList : 
    public gcomm::Map<const gcomm::UUID, MessageNode>
{
};

/*!
 * EVS message class
 */
class gcomm::evs::Message
{
public:
    enum Type
    {
        T_USER,     /*!< User generated message */
        T_DELEGATE, /*!< Delegate message       */
        T_GAP,      /*!< Gap message            */
        T_JOIN,     /*!< Join message           */
        T_LEAVE,    /*!< Leave message          */
        T_INSTALL   /*!< Install message        */
    };
    
    
    static const uint8_t F_MSG_MORE = 0x1; /*!< Sender has more messages to send  */
    static const uint8_t F_RESEND   = 0x2; /*!< Message is resent upon request    */
    static const uint8_t F_SOURCE   = 0x4;  /*!< Message has source set explicitly */

    /*!
     * Get version of the message
     *
     * @return Version number
     */
    uint8_t get_version() const { return version; }
    
    /*!
     * Get type of the message
     *
     * @return Message type
     */
    Type get_type() const { return type; }
    
    /*!
     * Check wheter message is of membership type
     *
     * @return True if message is of membership type, otherwise false
     */
    bool is_membership() const
    {
        return type == T_JOIN || type == T_INSTALL || type == T_LEAVE;
    }
    
    /*!
     * Get user type of the message. This is applicable only for 
     * messages of type T_USER.
     *
     * @return User type of the message.
     */
    uint8_t get_user_type() const { return user_type; }
    
    /*!
     * Get message safety prefix.
     *
     * @return Safety prefix of the message.
     */
    gcomm::SafetyPrefix get_safety_prefix() const { return safety_prefix; }
    
    /*!
     * Get sequence number associated to the message.
     *
     * @return Const reference to sequence number associated to the message.
     */
    const Seqno& get_seq() const { return seq; }

    /*!
     * Get sequence numer range associated to the message.
     * 
     * @return Sequence number range associated to the message.
     */
    const Seqno& get_seq_range() const { return seq_range; }

    /*!
     * Get all-received-upto sequence number associated the the message.
     *
     * @return All-received-upto sequence number associated to the message.
     */
    const Seqno& get_aru_seq() const { return aru_seq; }
    
    /*!
     * Get message flags.
     *
     * @return Message flags.
     */
    uint8_t get_flags() const { return flags; }
    
    /*!
     * Get message source UUID.
     *
     * @return Message source UUID.
     */
    const UUID& get_source() const { return source; }

    /*!
     * Get message source view id, view where the message was originated
     * from.
     *
     * @return Message source view id.
     */
    const gcomm::ViewId& get_source_view_id() const { return source_view_id; }
    
    /*!
     * Get range UUID associated to the message.
     *
     * @return Range UUID associated to the message.
     */
    const UUID& get_range_uuid() const { return range_uuid; }

    /*!
     * Get range associated to the message.
     *     
     * @return Range associated to the message.
     */
    const Range& get_range() const { return range; }
    
    /*!
     * Get fifo sequence number associated to the message. This is 
     * applicable only for messages of membership type.
     *
     * @return Fifo sequence number associated to the message.
     */
    int64_t get_fifo_seq() const { return fifo_seq; }
    
    /*!
     * Get message node list.
     *
     * @return Const reference to message node list.
     */
    const MessageNodeList& get_node_list() const { return *node_list; }
    
    /*!
     * Get timestamp associated to the message.
     */
    const gcomm::Time& get_tstamp() const { return tstamp; }

    /*!
     * Copy constructor.
     */

    Message(const Message& msg) :
        version(msg.version),
        type(msg.type),
        user_type(msg.user_type),
        safety_prefix(msg.safety_prefix),
        seq(msg.seq),
        seq_range(msg.seq_range),
        aru_seq(msg.aru_seq),
        fifo_seq(msg.fifo_seq),
        flags(msg.flags),
        source(msg.source),
        source_view_id(msg.source_view_id),
        range_uuid(msg.range_uuid),
        range(msg.range),
        tstamp(msg.tstamp),
        node_list(msg.node_list != 0 ? new MessageNodeList(*msg.node_list) : 0)
    { }

    virtual ~Message() { delete node_list; }

protected:
    /*! Default constructor */
    Message(const uint8_t version_,
            const Type    type_,
            const uint8_t user_type_,
            const gcomm::SafetyPrefix safety_prefix_,
            const Seqno& seq_,
            const Seqno& seq_range_,
            const Seqno& aru_seq_,
            const int64_t fifo_seq_,
            const uint8_t flags_,
            const gcomm::UUID& source_,
            const gcomm::ViewId& source_view_id_,
            const gcomm::UUID& range_uuid_,
            const Range range_,
            const Time& tstamp_,
            const MessageNodeList* node_list_) :
        version(version_),
        type(type_),
        user_type(user_type_),
        safety_prefix(safety_prefix_),
        seq(seq_),
        seq_range(seq_range_),
        aru_seq(aru_seq_),
        fifo_seq(fifo_seq_),
        flags(flags_),
        source(source_),
        source_view_id(source_view_id_),
        range_uuid(range_uuid_),
        range(range_),
        tstamp(Time::now()),
        node_list(node_list_ != 0 ? new MessageNodeList(*node_list_) : 0)
    { }
    
private:    
    
    uint8_t             const version;
    Type                const type;
    uint8_t             const user_type;
    gcomm::SafetyPrefix const safety_prefix;
    Seqno               const seq;
    Seqno               const seq_range;
    Seqno               const aru_seq;
    int64_t             const fifo_seq;
    uint8_t             const flags;
    gcomm::UUID               source;
    gcomm::ViewId       const source_view_id;
    gcomm::UUID         const range_uuid;
    Range               const range;
    Time                const tstamp;
    MessageNodeList*    const node_list;
    
    void operator=(const Message&);
};

/*!
 * User message class.
 */
class gcomm::evs::UserMessage : public Message
{
public:
    UserMessage(const gcomm::ViewId       source_view_id,
                const Seqno&              seq,
                const Seqno&              aru_seq       = Seqno::max(),
                const Seqno&              seq_range     = 0,
                const gcomm::SafetyPrefix safety_prefix = gcomm::SP_SAFE,
                const uint8_t             user_type     = 0xff,
                const uint8_t             flags         = 0             ) :
        Message(0,
                Message::T_USER,
                user_type,
                safety_prefix,
                seq,
                seq_range,
                aru_seq,
                -1,
                flags,
                UUID(),
                source_view_id,
                UUID(),
                Range(),
                Time::now(),
                0)
    { }
};


#endif // EVS_MESSAGE2_HPP
