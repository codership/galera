/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef EVS_MESSAGE2_HPP
#define EVS_MESSAGE2_HPP


#include "gcomm/order.hpp"
#include "gcomm/view.hpp"
#include "gcomm/map.hpp"

#include "evs_seqno.hpp"

#include "gu_datetime.hpp"

namespace gcomm
{
    namespace evs
    {
        class MessageNode;
        std::ostream& operator<<(std::ostream&, const MessageNode&);
        class MessageNodeList;
        class Message;
        std::ostream& operator<<(std::ostream&, const Message&);
        class UserMessage;
        class DelegateMessage;
        class GapMessage;
        class JoinMessage;
        class LeaveMessage;
        class InstallMessage;
        class SelectNodesOp;
        class RangeHsCmp;
    }
}




class gcomm::evs::MessageNode
{
public:
    MessageNode(const bool    operational_     = false,
                const Seqno   leave_seq_       = Seqno::max(),
                const ViewId& view_id_         = ViewId(V_REG),
                const Seqno   safe_seq_        = Seqno::max(),
                const Range   im_range_        = Range()) :
        operational(operational_),
        leave_seq(leave_seq_),
        view_id(view_id_),
        safe_seq(safe_seq_),
        im_range(im_range_)
    { }

    MessageNode(const MessageNode& mn) :
        operational (mn.operational),
        leave_seq   (mn.leave_seq),
        view_id     (mn.view_id),
        safe_seq    (mn.safe_seq),
        im_range    (mn.im_range)
    { }
    
    bool get_operational() const { return operational; }
    bool get_leaving() const { return not (leave_seq == Seqno::max()); }
    Seqno get_leave_seq() const { return leave_seq; }
    const ViewId& get_view_id() const { return view_id; }
    Seqno get_safe_seq() const { return safe_seq; }
    Range get_im_range() const { return im_range; }
    
    bool operator==(const MessageNode& cmp) const
    {
        return operational == cmp.operational &&
            leave_seq == cmp.leave_seq &&
            view_id == cmp.view_id && 
            safe_seq == cmp.safe_seq &&
            im_range == cmp.im_range;
    }
    
    size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset) const
        throw(gu::Exception);
    size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset)
        throw(gu::Exception);
    static size_t serial_size();
private:
    bool     operational;     // Is operational
    Seqno    leave_seq;
    ViewId   view_id;         // Current view as seen by source of this message
    Seqno    safe_seq;        // Safe seq as seen...
    Range    im_range;        // Input map range as seen...
};

class gcomm::evs::MessageNodeList : 
    public gcomm::Map<gcomm::UUID, MessageNode>
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
        T_NONE     = 0,
        T_USER     = 1, /*!< User generated message */
        T_DELEGATE = 2, /*!< Delegate message       */
        T_GAP      = 3, /*!< Gap message            */
        T_JOIN     = 4, /*!< Join message           */
        T_INSTALL  = 5, /*!< Install message        */
        T_LEAVE    = 6  /*!< Leave message          */
    };
    
    
    static const uint8_t F_MSG_MORE = 0x1; /*!< Sender has more messages to send  */
    static const uint8_t F_RETRANS   = 0x2; /*!< Message is resent upon request    */
    /*! 
     * @brief Message source has been set explicitly via set_source()
     */
    static const uint8_t F_SOURCE   = 0x4;  

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
     * Get message order type.
     *
     * @return Order type of the message.
     */
    Order get_order() const { return order; }
    
    /*!
     * Get sequence number associated to the message.
     *
     * @return Const reference to sequence number associated to the message.
     */
    Seqno get_seq() const { return seq; }

    /*!
     * Get sequence numer range associated to the message.
     * 
     * @return Sequence number range associated to the message.
     */
    Seqno get_seq_range() const { return seq_range; }

    /*!
     * Get all-received-upto sequence number associated the the message.
     *
     * @return All-received-upto sequence number associated to the message.
     */
    Seqno get_aru_seq() const { return aru_seq; }
    
    /*!
     * Get message flags.
     *
     * @return Message flags.
     */
    uint8_t get_flags() const { return flags; }
    
    /*!
     * Set message source
     *
     * @param uuid Source node uuid
     */
    void set_source(const UUID& uuid) 
    { 
        source = uuid; 
        flags |= F_SOURCE;
    }
    
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
    Range get_range() const { return range; }
    
    /*!
     * Get fifo sequence number associated to the message. This is 
     * applicable only for messages of membership type.
     *
     * @return Fifo sequence number associated to the message.
     */
    int64_t get_fifo_seq() const { return fifo_seq; }

    bool has_node_list() const { return (node_list != 0); }
    
    /*!
     * Get message node list.
     *
     * @return Const reference to message node list.
     */
    const MessageNodeList& get_node_list() const { return *node_list; }
    
    /*!
     * Get timestamp associated to the message.
     */
    gu::datetime::Date get_tstamp() const { return tstamp; }
    
    size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset)
        throw(gu::Exception);
    
    bool operator==(const Message& cmp) const;
    
    /*!
     * Copy constructor.
     */
    
    Message(const Message& msg) :
        version        (msg.version),
        type           (msg.type),
        user_type      (msg.user_type),
        order          (msg.order),
        seq            (msg.seq),
        seq_range      (msg.seq_range),
        aru_seq        (msg.aru_seq),
        fifo_seq       (msg.fifo_seq),
        flags          (msg.flags),
        source         (msg.source),
        source_view_id (msg.source_view_id),
        range_uuid     (msg.range_uuid),
        range          (msg.range),
        tstamp         (msg.tstamp),
        node_list      (msg.node_list != 0 ? 
                        new MessageNodeList(*msg.node_list) : 0)
    { }

    Message& operator=(const Message& msg)
    {
        version        = msg.version;
        type           = msg.type;
        user_type      = msg.user_type;
        order          = msg.order;
        seq            = msg.seq;
        seq_range      = msg.seq_range;
        aru_seq        = msg.aru_seq;
        fifo_seq       = msg.fifo_seq;
        flags          = msg.flags;
        source         = msg.source;
        source_view_id = msg.source_view_id;
        range_uuid     = msg.range_uuid;
        range          = msg.range;
        tstamp         = msg.tstamp;
        node_list      = (msg.node_list != 0 ? 
                          new MessageNodeList(*msg.node_list) : 0);
        return *this;
    }

    virtual ~Message() { delete node_list; }


    /*! Default constructor */
    Message(const uint8_t          version_        = 0,
            const Type             type_           = T_NONE,
            const UUID&            source_         = UUID::nil(),
            const ViewId&          source_view_id_ = ViewId(),
            const uint8_t          user_type_      = 0xff,
            const Order            order_          = O_DROP,
            const int64_t          fifo_seq_       = -1,
            const Seqno            seq_            = Seqno::max(),
            const Seqno            seq_range_      = Seqno::max(),
            const Seqno            aru_seq_        = Seqno::max(),
            const uint8_t          flags_          = 0,
            const UUID&            range_uuid_     = UUID(),
            const Range            range_          = Range(),
            const MessageNodeList* node_list_      = 0) :
        version        (version_),
        type           (type_),
        user_type      (user_type_),
        order          (order_),
        seq            (seq_),
        seq_range      (seq_range_),
        aru_seq        (aru_seq_),
        fifo_seq       (fifo_seq_),
        flags          (flags_),
        source         (source_),
        source_view_id (source_view_id_),
        range_uuid     (range_uuid_),
        range          (range_),
        tstamp         (gu::datetime::Date::now()),
        node_list      (node_list_ != 0 ? new MessageNodeList(*node_list_) : 0)
    { }

protected:

    size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset) const
        throw(gu::Exception);

    size_t serial_size() const;
    
    uint8_t            version;
    Type               type;
    uint8_t            user_type;
    Order              order;
    Seqno              seq;
    Seqno              seq_range;
    Seqno              aru_seq;
    int64_t            fifo_seq;
    uint8_t            flags;
    UUID               source;
    ViewId             source_view_id;
    UUID               range_uuid;
    Range              range;
    gu::datetime::Date tstamp;
    MessageNodeList*   node_list;
    

};

/*!
 * User message class.
 */
class gcomm::evs::UserMessage : public Message
{
public:
    UserMessage(const UUID&        source         = UUID::nil(),
                const ViewId&      source_view_id = ViewId(),
                const Seqno        seq            = Seqno::max(),
                const Seqno        aru_seq        = Seqno::max(),
                const Seqno        seq_range      = 0,
                const Order        order          = O_SAFE,
                const int64_t      fifo_seq       = -1,
                const uint8_t      user_type      = 0xff,
                const uint8_t      flags          = 0) :
        Message(0,
                Message::T_USER,
                source,
                source_view_id,
                user_type,
                order,
                fifo_seq,
                seq,
                seq_range,
                aru_seq,
                flags,
                UUID(),
                Range(),
                0)
    { }
    
    void set_aru_seq(const Seqno as) { aru_seq = as; }
    
    size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset) const
        throw(gu::Exception);
    size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset,
                       bool skip_header = false)
        throw(gu::Exception);
    size_t serial_size() const;

};

class gcomm::evs::DelegateMessage : public Message
{
public:
    DelegateMessage(const UUID&   source         = UUID::nil(), 
                    const ViewId& source_view_id = ViewId(),
                    const int64_t fifo_seq       = -1) : 
        Message(0, 
                T_DELEGATE,
                source,
                source_view_id,
                0xff,
                O_UNRELIABLE,
                fifo_seq)
    { }
    size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset) const
        throw(gu::Exception);
    size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset,
                       bool skip_header = false)
        throw(gu::Exception);
    size_t serial_size() const;
};

class gcomm::evs::GapMessage : public Message
{
public:
    GapMessage(const UUID&   source         = UUID::nil(),
               const ViewId& source_view_id = ViewId(),
               const Seqno   seq            = Seqno::max(),
               const Seqno   aru_seq        = Seqno::max(),
               const int64_t fifo_seq       = -1,
               const UUID&   range_uuid     = UUID::nil(),
               const Range   range          = Range()) : 
        Message(0, 
                T_GAP,
                source,
                source_view_id,                
                0xff,
                O_UNRELIABLE,
                fifo_seq,
                seq,
                Seqno::max(),
                aru_seq,
                0,
                range_uuid,
                range,
                0)
    { }
    size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset) const
        throw(gu::Exception);
    size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset,
                       bool skip_header = false)
        throw(gu::Exception);
    size_t serial_size() const;
};

class gcomm::evs::JoinMessage : public Message
{
public:
    JoinMessage(const UUID&            source         = UUID::nil(),
                const ViewId&          source_view_id = ViewId(),
                const Seqno            seq            = Seqno::max(), 
                const Seqno            aru_seq        = Seqno::max(),
                const int64_t          fifo_seq       = -1,
                const MessageNodeList* node_list      = 0) :
        Message(0,
                Message::T_JOIN,
                source,
                source_view_id,
                0xff,
                O_UNRELIABLE,
                fifo_seq,
                seq,
                Seqno::max(),
                aru_seq,
                0,
                UUID(),
                Range(),
                node_list)
    { }
    size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset) const
        throw(gu::Exception);
    size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset,
                       bool skip_header = false)
        throw(gu::Exception);
    size_t serial_size() const;
};

class gcomm::evs::InstallMessage : public Message
{
public:
    InstallMessage(const UUID&            source         = UUID::nil(),
                   const ViewId&          source_view_id = ViewId(),
                   const Seqno            seq            = Seqno::max(), 
                   const Seqno            aru_seq        = Seqno::max(),
                   const int64_t          fifo_seq       = -1,
                   const MessageNodeList* node_list      = 0) :
        Message(0,
                Message::T_INSTALL,
                source,
                source_view_id,
                0xff,
                O_UNRELIABLE,
                fifo_seq,
                seq,
                Seqno::max(),
                aru_seq,
                0,
                UUID(),
                Range(),
                node_list)
    { }
    size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset) const
        throw(gu::Exception);
    size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset,
                       bool skip_header = false)
        throw(gu::Exception);
    size_t serial_size() const;
};

class gcomm::evs::LeaveMessage : public Message
{
public:
    LeaveMessage(const UUID&   source         = UUID::nil(),
                 const ViewId& source_view_id = ViewId(),
                 const Seqno   seq            = Seqno::max(),
                 const Seqno   aru_seq        = Seqno::max(),
                 const int64_t fifo_seq       = -1,
                 const uint8_t flags          = 0) :
        Message(0,
                T_LEAVE,
                source,
                source_view_id,
                0xff,
                O_UNRELIABLE,
                fifo_seq,
                seq,
                Seqno::max(),
                aru_seq,
                flags)
    { }
    size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset) const
        throw(gu::Exception);
    size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset,
                       bool skip_header = false)
        throw(gu::Exception);
    size_t serial_size() const;
};


class gcomm::evs::SelectNodesOp
{
public:
    SelectNodesOp(MessageNodeList& nl_, 
                  const gcomm::ViewId& view_id_, 
                  const bool operational_,
                  const bool leaving_ ) : 
        nl          (nl_), 
        view_id     (view_id_),
        operational (operational_),
        leaving     (leaving_)
    { }
    
    void operator()(const MessageNodeList::value_type& vt) const
    {
        const MessageNode& node(MessageNodeList::get_value(vt));
        if (node.get_view_id()       == view_id           &&
            ((operational            == true          && 
              leaving                == true            ) ||
             (node.get_operational() == operational &&
              node.get_leaving()     == leaving        ) ) )
            
        {
            nl.insert_checked(vt);
        }
    }
private:
    MessageNodeList&       nl;
    ViewId           const view_id;
    bool             const operational;
    bool             const leaving;
};


class gcomm::evs::RangeHsCmp
{
public:
    bool operator()(const MessageNodeList::value_type& a,
                    const MessageNodeList::value_type& b) const
    {
        if (MessageNodeList::get_value(a).get_im_range().get_hs() == Seqno::max())
        {
            return true;
        }
        else if (MessageNodeList::get_value(b).get_im_range().get_hs() == Seqno::max())
        {
            return false;
        }
        else
        {
            return MessageNodeList::get_value(a).get_im_range().get_hs() < 
                MessageNodeList::get_value(b).get_im_range().get_hs();
        }
    }
};


#endif // EVS_MESSAGE2_HPP
