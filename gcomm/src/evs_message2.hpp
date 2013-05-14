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
#include "gu_convert.hpp"

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
        class AggregateMessage;
        std::ostream& operator<<(std::ostream&, const AggregateMessage&);
        class DelegateMessage;
        class GapMessage;
        class JoinMessage;
        class LeaveMessage;
        class InstallMessage;
        class SelectNodesOp;
        class RangeLuCmp;
        class RangeHsCmp;
    }
}




class gcomm::evs::MessageNode
{
public:
    MessageNode(const bool    operational  = false,
                const bool    suspected    = false,
                const SegmentId segment    = 0,
                const seqno_t leave_seq    = -1,
                const ViewId& view_id      = ViewId(V_REG),
                const seqno_t safe_seq     = -1,
                const Range   im_range     = Range()) :
        operational_(operational),
        suspected_  (suspected  ),
        segment_    (segment    ),
        leave_seq_  (leave_seq  ),
        view_id_    (view_id    ),
        safe_seq_   (safe_seq   ),
        im_range_   (im_range   )
    { }

    MessageNode(const MessageNode& mn)
        :
        operational_ (mn.operational_),
        suspected_   (mn.suspected_  ),
        segment_     (mn.segment_    ),
        leave_seq_   (mn.leave_seq_  ),
        view_id_     (mn.view_id_    ),
        safe_seq_    (mn.safe_seq_   ),
        im_range_    (mn.im_range_   )
    { }

    bool          operational() const { return operational_       ; }
    bool          suspected()   const { return suspected_         ; }
    bool          leaving()     const { return (leave_seq_ != -1) ; }
    seqno_t       leave_seq()   const { return leave_seq_         ; }
    const ViewId& view_id()     const { return view_id_           ; }
    seqno_t       safe_seq()    const { return safe_seq_          ; }
    Range         im_range()    const { return im_range_          ; }
    SegmentId     segment()     const { return segment_           ; }

    bool operator==(const MessageNode& cmp) const
    {
        return (operational_ == cmp.operational_ &&
                suspected_   == cmp.suspected_   &&
                leave_seq_   == cmp.leave_seq_   &&
                view_id_     == cmp.view_id_     &&
                safe_seq_    == cmp.safe_seq_    &&
                im_range_    == cmp.im_range_);
    }

    size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset) const;
    size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset);
    static size_t serial_size();
private:
    enum
    {
        F_OPERATIONAL = 1 << 0,
        F_SUSPECTED   = 1 << 1
    };
    bool      operational_;  // Is operational
    bool      suspected_;
    SegmentId segment_;
    seqno_t   leave_seq_;
    ViewId    view_id_;       // Current view as seen by source of this message
    seqno_t   safe_seq_;      // Safe seq as seen...
    Range     im_range_;      // Input map range as seen...
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
    static const uint8_t F_RETRANS  = 0x2; /*!< Message is resent upon request    */

    /*!
     * @brief Message source has been set explicitly via set_source()
     */
    static const uint8_t F_SOURCE   = 0x4;

    static const uint8_t F_AGGREGATE= 0x8; /*!< Message contains aggregated payload */
    static const uint8_t F_COMMIT   = 0x10;
    static const uint8_t F_BC       = 0x20;/*!< Message was sent in backward compatibility mode */
    /*!
     * Get version of the message
     *
     * @return Version number
     */
    uint8_t version() const { return version_; }

    /*!
     * Get type of the message
     *
     * @return Message type
     */
    Type type() const { return type_; }

    /*!
     * Check wheter message is of membership type
     *
     * @return True if message is of membership type, otherwise false
     */
    bool is_membership() const
    {
        return (type_ == T_JOIN || type_ == T_INSTALL || type_ == T_LEAVE);
    }

    /*!
     * Get user type of the message. This is applicable only for
     * messages of type T_USER.
     *
     * @return User type of the message.
     */
    uint8_t user_type() const { return user_type_; }

    /*!
     * Get message order type.
     *
     * @return Order type of the message.
     */
    Order order() const { return order_; }

    /*!
     * Get sequence number associated to the message.
     *
     * @return Const reference to sequence number associated to the message.
     */
    seqno_t seq() const { return seq_; }

    /*!
     * Get sequence numer range associated to the message.
     *
     * @return Sequence number range associated to the message.
     */
    seqno_t seq_range() const { return seq_range_; }

    /*!
     * Get all-received-upto sequence number associated the the message.
     *
     * @return All-received-upto sequence number associated to the message.
     */
    seqno_t aru_seq() const { return aru_seq_; }

    /*!
     * Get message flags.
     *
     * @return Message flags.
     */
    uint8_t flags() const { return flags_; }

    /*!
     * Set message source
     *
     * @param uuid Source node uuid
     */
    void set_source(const UUID& uuid)
    {
        source_ = uuid;
        flags_ |= F_SOURCE;
    }

    /*!
     * Get message source UUID.
     *
     * @return Message source UUID.
     */
    const UUID& source() const { return source_; }

    /*!
     * Get message source view id, view where the message was originated
     * from.
     *
     * @return Message source view id.
     */
    const gcomm::ViewId& source_view_id() const { return source_view_id_; }

    const gcomm::ViewId& install_view_id() const { return install_view_id_; }

    /*!
     * Get range UUID associated to the message.
     *
     * @return Range UUID associated to the message.
     */
    const UUID& range_uuid() const { return range_uuid_; }

    /*!
     * Get range associated to the message.
     *
     * @return Range associated to the message.
     */
    Range range() const { return range_; }

    /*!
     * Get fifo sequence number associated to the message. This is
     * applicable only for messages of membership type.
     *
     * @return Fifo sequence number associated to the message.
     */
    int64_t fifo_seq() const { return fifo_seq_; }

    /*!
     * Get message node list.
     *
     * @return Const reference to message node list.
     */
    const MessageNodeList& node_list() const { return node_list_; }

    /*!
     * Get timestamp associated to the message.
     */
    gu::datetime::Date tstamp() const { return tstamp_; }

    size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset);

    bool operator==(const Message& cmp) const;

    /*!
     * Copy constructor.
     */

    Message(const Message& msg) :
        version_         (msg.version_),
        type_            (msg.type_),
        user_type_       (msg.user_type_),
        order_           (msg.order_),
        seq_             (msg.seq_),
        seq_range_       (msg.seq_range_),
        aru_seq_         (msg.aru_seq_),
        fifo_seq_        (msg.fifo_seq_),
        flags_           (msg.flags_),
        source_          (msg.source_),
        source_view_id_  (msg.source_view_id_),
        install_view_id_ (msg.install_view_id_),
        range_uuid_      (msg.range_uuid_),
        range_           (msg.range_),
        tstamp_          (msg.tstamp_),
        node_list_       (msg.node_list_)
    { }

    Message& operator=(const Message& msg)
    {
        version_         = msg.version_;
        type_            = msg.type_;
        user_type_       = msg.user_type_;
        order_           = msg.order_;
        seq_             = msg.seq_;
        seq_range_       = msg.seq_range_;
        aru_seq_         = msg.aru_seq_;
        fifo_seq_        = msg.fifo_seq_;
        flags_           = msg.flags_;
        source_          = msg.source_;
        source_view_id_  = msg.source_view_id_;
        install_view_id_ = msg.install_view_id_;
        range_uuid_      = msg.range_uuid_;
        range_           = msg.range_;
        tstamp_          = msg.tstamp_;
        node_list_       = msg.node_list_;
        return *this;
    }

    virtual ~Message() {  }


    /*! Default constructor */
    Message(const uint8_t          version         = 0,
            const Type             type            = T_NONE,
            const UUID&            source          = UUID::nil(),
            const ViewId&          source_view_id  = ViewId(),
            const ViewId&          install_view_id = ViewId(),
            const uint8_t          user_type       = 0xff,
            const Order            order           = O_DROP,
            const int64_t          fifo_seq        = -1,
            const seqno_t          seq             = -1,
            const seqno_t          seq_range       = -1,
            const seqno_t          aru_seq         = -1,
            const uint8_t          flags           = 0,
            const UUID&            range_uuid      = UUID(),
            const Range            range           = Range(),
            const MessageNodeList& node_list       = MessageNodeList()) :
        version_         (version),
        type_            (type),
        user_type_       (user_type),
        order_           (order),
        seq_             (seq),
        seq_range_       (seq_range),
        aru_seq_         (aru_seq),
        fifo_seq_        (fifo_seq),
        flags_           (flags),
        source_          (source),
        source_view_id_  (source_view_id),
        install_view_id_ (install_view_id),
        range_uuid_      (range_uuid),
        range_           (range),
        tstamp_          (gu::datetime::Date::now()),
        node_list_       (node_list)
    { }

protected:

    size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset) const;

    size_t serial_size() const;

    uint8_t            version_;
    Type               type_;
    uint8_t            user_type_;
    Order              order_;
    seqno_t            seq_;
    seqno_t            seq_range_;
    seqno_t            aru_seq_;
    int64_t            fifo_seq_;
    uint8_t            flags_;
    UUID               source_;
    ViewId             source_view_id_;
    ViewId             install_view_id_;
    UUID               range_uuid_;
    Range              range_;
    gu::datetime::Date tstamp_;
    MessageNodeList    node_list_;


};

/*!
 * User message class.
 */
class gcomm::evs::UserMessage : public Message
{
public:
    UserMessage(const int          version        = -1,
                const UUID&        source         = UUID::nil(),
                const ViewId&      source_view_id = ViewId(),
                const seqno_t      seq            = -1,
                const seqno_t      aru_seq        = -1,
                const seqno_t      seq_range      = 0,
                const Order        order          = O_SAFE,
                const int64_t      fifo_seq       = -1,
                const uint8_t      user_type      = 0xff,
                const uint8_t      flags          = 0) :
        Message(version,
                Message::T_USER,
                source,
                source_view_id,
                ViewId(),
                user_type,
                order,
                fifo_seq,
                seq,
                seq_range,
                aru_seq,
                flags,
                UUID(),
                Range())
    { }

    void set_aru_seq(const seqno_t as) { aru_seq_ = as; }

    size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset) const;
    size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset,
                       bool skip_header = false);
    size_t serial_size() const;

};


class gcomm::evs::AggregateMessage
{
public:
    AggregateMessage(const int     flags     = 0,
                     const size_t  len       = 0,
                     const uint8_t user_type = 0xff)
        :
        flags_    (gu::convert(flags, uint8_t(0))),
        user_type_(user_type),
        len_      (gu::convert(len, uint16_t(0)))
    { }

    int    flags() const { return flags_; }
    size_t len()   const { return len_;   }
    uint8_t user_type() const { return user_type_; }

    size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset) const;
    size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset);
    size_t serial_size() const;
    bool operator==(const AggregateMessage& cmp) const
    {
        return (flags_ == cmp.flags_ && len_ == cmp.len_ && user_type_ == cmp.user_type_);
    }

private:
    uint8_t  flags_;
    uint8_t  user_type_;
    uint16_t len_;
};

inline std::ostream& gcomm::evs::operator<<(std::ostream& os, const AggregateMessage& am)
{
    return (os << "{flags=" << am.flags() << ",len=" << am.len() << "}");
}


class gcomm::evs::DelegateMessage : public Message
{
public:
    DelegateMessage(const int     version   = -1,
                    const UUID&   source         = UUID::nil(),
                    const ViewId& source_view_id = ViewId(),
                    const int64_t fifo_seq       = -1) :
        Message(version,
                T_DELEGATE,
                source,
                source_view_id,
                ViewId(),
                0xff,
                O_UNRELIABLE,
                fifo_seq)
    { }
    size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset) const;
    size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset,
                       bool skip_header = false);
    size_t serial_size() const;
};

class gcomm::evs::GapMessage : public Message
{
public:
    GapMessage(const int     version   = -1,
               const UUID&   source         = UUID::nil(),
               const ViewId& source_view_id = ViewId(),
               const seqno_t   seq            = -1,
               const seqno_t   aru_seq        = -1,
               const int64_t fifo_seq       = -1,
               const UUID&   range_uuid     = UUID::nil(),
               const Range   range          = Range(),
               const uint8_t flags          = 0) :
        Message(version,
                T_GAP,
                source,
                source_view_id,
                ViewId(),
                0xff,
                O_UNRELIABLE,
                fifo_seq,
                seq,
                -1,
                aru_seq,
                flags,
                range_uuid,
                range)
    { }
    size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset) const;
    size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset,
                       bool skip_header = false);
    size_t serial_size() const;
};

class gcomm::evs::JoinMessage : public Message
{
public:
    JoinMessage(const int     version   = -1,
                const UUID&            source         = UUID::nil(),
                const ViewId&          source_view_id = ViewId(),
                const seqno_t            seq            = -1,
                const seqno_t            aru_seq        = -1,
                const int64_t          fifo_seq       = -1,
                const MessageNodeList& node_list      = MessageNodeList()) :
        Message(version,
                Message::T_JOIN,
                source,
                source_view_id,
                ViewId(),
                0xff,
                O_UNRELIABLE,
                fifo_seq,
                seq,
                -1,
                aru_seq,
                0,
                UUID(),
                Range(),
                node_list)
    { }
    size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset) const;
    size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset,
                       bool skip_header = false);
    size_t serial_size() const;
};

class gcomm::evs::InstallMessage : public Message
{
public:
    InstallMessage(const int     version   = -1,
                   const UUID&            source          = UUID::nil(),
                   const ViewId&          source_view_id  = ViewId(),
                   const ViewId&          install_view_id = ViewId(),
                   const seqno_t            seq             = -1,
                   const seqno_t            aru_seq         = -1,
                   const int64_t          fifo_seq        = -1,
                   const MessageNodeList& node_list       = MessageNodeList()) :
        Message(version,
                Message::T_INSTALL,
                source,
                source_view_id,
                install_view_id,
                0xff,
                O_UNRELIABLE,
                fifo_seq,
                seq,
                -1,
                aru_seq,
                0,
                UUID(),
                Range(),
                node_list)
    { }
    size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset) const;
    size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset,
                       bool skip_header = false);
    size_t serial_size() const;
};

class gcomm::evs::LeaveMessage : public Message
{
public:
    LeaveMessage(const int     version   = -1,
                 const UUID&   source         = UUID::nil(),
                 const ViewId& source_view_id = ViewId(),
                 const seqno_t   seq            = -1,
                 const seqno_t   aru_seq        = -1,
                 const int64_t fifo_seq       = -1,
                 const uint8_t flags          = 0) :
        Message(version,
                T_LEAVE,
                source,
                source_view_id,
                ViewId(),
                0xff,
                O_UNRELIABLE,
                fifo_seq,
                seq,
                -1,
                aru_seq,
                flags)
    { }
    size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset) const;
    size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset,
                       bool skip_header = false);
    size_t serial_size() const;
};


class gcomm::evs::SelectNodesOp
{
public:
    SelectNodesOp(MessageNodeList& nl,
                  const gcomm::ViewId& view_id,
                  const bool operational,
                  const bool leaving)
        :
        nl_          (nl),
        view_id_     (view_id),
        operational_ (operational),
        leaving_     (leaving)
    { }

    void operator()(const MessageNodeList::value_type& vt) const
    {
        const MessageNode& node(MessageNodeList::value(vt));
        if ((view_id_                  == ViewId() ||
             node.view_id()        == view_id_    ) &&
            ((operational_             == true          &&
              leaving_                 == true   ) ||
             (node.operational() == operational_ &&
              node.leaving()     == leaving_ ) ) )

        {
            nl_.insert_unique(vt);
        }
    }
private:
    MessageNodeList&       nl_;
    ViewId           const view_id_;
    bool             const operational_;
    bool             const leaving_;
};


class gcomm::evs::RangeLuCmp
{
public:
    bool operator()(const MessageNodeList::value_type& a,
                    const MessageNodeList::value_type& b) const
    {
        gcomm_assert(MessageNodeList::value(a).view_id() ==
                     MessageNodeList::value(b).view_id());
        return (MessageNodeList::value(a).im_range().lu() <
                MessageNodeList::value(b).im_range().lu());
    }
};

class gcomm::evs::RangeHsCmp
{
public:
    bool operator()(const MessageNodeList::value_type& a,
                    const MessageNodeList::value_type& b) const
    {
        gcomm_assert(MessageNodeList::value(a).view_id() ==
                     MessageNodeList::value(b).view_id());
        return (MessageNodeList::value(a).im_range().hs() <
                MessageNodeList::value(b).im_range().hs());
    }
};


#endif // EVS_MESSAGE2_HPP
