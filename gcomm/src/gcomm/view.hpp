/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */


/*!
 * @file Group view class (used in the ProtoUpMeta (protolay.hpp)
 */

#ifndef _GCOMM_VIEW_HPP_
#define _GCOMM_VIEW_HPP_


#include "gcomm/uuid.hpp"
#include "gcomm/types.hpp"
#include "gcomm/map.hpp"

namespace gcomm
{
    typedef enum
    {
        V_NONE     = -1,
        V_REG      = 0,
        V_TRANS    = 1,
        V_NON_PRIM = 2,
        V_PRIM     = 3
    } ViewType;

    class ViewId
    {
    public:


        ViewId(const ViewType type    = V_NONE,
               const UUID&    uuid    = UUID::nil(),
               const uint32_t seq     = 0) :
            type_(type),
            uuid_(uuid),
            seq_ (seq)
        { }

        ViewId(const ViewType type,
               const ViewId& vi) :
            type_(type),
            uuid_(vi.uuid()),
            seq_ (vi.seq())
        { }

        virtual ~ViewId() { }

        ViewType    type() const { return type_; }

        const UUID& uuid() const { return uuid_; }

        uint32_t    seq()  const { return seq_; }

        size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset);

        size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset) const;

        static size_t serial_size()
        {
            return UUID::serial_size() + sizeof(reinterpret_cast<ViewId*>(0)->seq_);
        }

        bool operator<(const ViewId& cmp) const
        {
            // View ordering:
            // 1) view seq less than
            // 2) uuid newer than
            // 3) type less than
            return (seq_ < cmp.seq_ ||
                    (seq_ == cmp.seq_ &&
                     (cmp.uuid_.older(uuid_) ||
                      (uuid_ == cmp.uuid_ && type_ < cmp.type_) ) ) );
        }

        bool operator==(const ViewId& cmp) const
        {
            return (seq_  == cmp.seq_  &&
                    type_ == cmp.type_ &&
                    uuid_ == cmp.uuid_);
        }

        bool operator!=(const ViewId& cmp) const
        {
            return !(*this == cmp);
        }

    private:
        ViewType type_;
        UUID     uuid_; // uniquely identifies the sequence of group views (?)
        uint32_t seq_;  // position in the sequence                        (?)
    };

    std::ostream& operator<<(std::ostream&, const ViewId&);

    typedef uint8_t SegmentId;

    class Node
    {
    public:
        Node(SegmentId segment) : segment_(segment)
        { }
        SegmentId segment() const { return segment_; }
        bool operator==(const Node& cmp) const { return true; }
    private:
        SegmentId segment_;
    };

    inline std::ostream& operator<<(std::ostream& os, const Node& n)
    {
        return (os << static_cast<int>(n.segment()) );
    }


    class NodeList : public gcomm::Map<UUID, Node> { };

    class View
    {
    public:

        View() :
            bootstrap_   (false),
            view_id_     (V_NONE),
            members_     (),
            joined_      (),
            left_        (),
            partitioned_ ()
        { }

        View(const ViewId& view_id, bool bootstrap = false) :
            bootstrap_   (bootstrap),
            view_id_     (view_id),
            members_     (),
            joined_      (),
            left_        (),
            partitioned_ ()
        { }

        ~View() {}

        void add_member  (const UUID& pid, SegmentId segment);

        void add_members (NodeList::const_iterator begin,
                          NodeList::const_iterator end);

        void add_joined      (const UUID& pid, SegmentId segment);
        void add_left        (const UUID& pid, SegmentId segment);
        void add_partitioned (const UUID& pid, SegmentId segment);

        const NodeList& members     () const;
        const NodeList& joined      () const;
        const NodeList& left        () const;
        const NodeList& partitioned () const;

        NodeList& members() { return members_; }

        bool is_member(const UUID& uuid) const
        { return members_.find(uuid) != members_.end(); }

        bool is_joining(const UUID& uuid) const
        { return joined_.find(uuid) != joined_.end(); }

        bool is_leaving(const UUID& uuid) const
        { return left_.find(uuid) != left_.end(); }

        bool is_partitioning(const UUID& uuid) const
        { return partitioned_.find(uuid) != partitioned_.end(); }


        ViewType      type           () const;
        const ViewId& id             () const;
        const UUID&   representative () const;

        bool is_empty() const;
        bool is_bootstrap() const { return bootstrap_; }

    private:
        bool     bootstrap_;   // Flag indicating if view was bootstrapped
        ViewId   view_id_;     // View identifier
        NodeList members_;     // List of members in view
        NodeList joined_;      // List of newly joined members in view
        NodeList left_;        // Fracefully left members from previous view
        NodeList partitioned_; // Partitioned members from previous view
    };

    bool operator==(const gcomm::View&, const gcomm::View&);
    std::ostream& operator<<(std::ostream&, const View&);

} // namespace gcomm

#endif // _GCOMM_VIEW_HPP_
