/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */


/*!
 * @file Group view class (used in the ProtoUpMeta (protolay.hpp)
 */

#ifndef _GCOMM_VIEW_HPP_
#define _GCOMM_VIEW_HPP_


#include "gcomm/uuid.hpp"
#include "gcomm/logger.hpp"
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
        

        ViewId(const ViewType type_    = V_NONE, 
               const UUID&    uuid_   = UUID::nil(), 
               const uint32_t seq_ = 0) :
            type(type_),
            uuid(uuid_),
            seq(seq_)
        { }
        
        ViewId(const ViewType type_,
               const ViewId& vi) :
            type(type_),
            uuid(vi.get_uuid()),
            seq(vi.get_seq())
        { }
        
        virtual ~ViewId() { }
        
        ViewType    get_type() const { return type; }
        
        const UUID& get_uuid() const { return uuid; }
        
        uint32_t    get_seq()  const { return seq; }
        
        size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset)
            throw (gu::Exception);
        
        size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset) const
            throw (gu::Exception);
        
        static size_t serial_size() 
        {
            return UUID::serial_size() + sizeof(reinterpret_cast<ViewId*>(0)->seq);
        }
        
        bool operator<(const ViewId& cmp) const
        {
            return (seq < cmp.seq || 
                    (seq == cmp.seq && 
                     (uuid < cmp.uuid ||
                      (uuid == cmp.uuid && type < cmp.type) ) ) );
        }
        
        bool operator==(const ViewId& cmp) const
        {
            return (uuid == cmp.uuid && seq == cmp.seq && type == cmp.type);
        }
        
        bool operator!=(const ViewId& cmp) const
        {
            return !(*this == cmp);
        }
        
    private:
        ViewType type;
        UUID uuid; // uniquely identifies the sequence of group views (?)
        uint32_t    seq;  // position in the sequence                        (?)
    };
    
    std::ostream& operator<<(std::ostream&, const ViewId&);

    class Node : public String<16> 
    { 
    public:
        Node() : String<16>("") { }
        bool operator==(const Node& cmp) const { return true; }
    };
    
    inline std::ostream& operator<<(std::ostream& os, const Node& n)
    {
        return (os << "");
    }
    
    
    class NodeList : public gcomm::Map<UUID, Node> { };
    
    class View
    {    
    public:

        View() :
            view_id     (V_NONE),
            members     (),
            joined      (),
            left        (),
            partitioned ()
        { }
        
        View(const ViewId& view_id_) : 
            view_id     (view_id_),
            members     (),
            joined      (),
            left        (),
            partitioned ()
        { }
        
        ~View() {}
    
        void add_member  (const UUID& pid, const std::string& name = "");
        
        void add_members (NodeList::const_iterator begin,
                          NodeList::const_iterator end);
        
        void add_joined      (const UUID& pid, const std::string& name);
        void add_left        (const UUID& pid, const std::string& name);
        void add_partitioned (const UUID& pid, const std::string& name);
        
        const NodeList& get_members     () const;
        const NodeList& get_joined      () const;
        const NodeList& get_left        () const;
        const NodeList& get_partitioned () const;
        

        bool is_member(const UUID& uuid) const
        { return members.find(uuid) != members.end(); }
        
        bool is_joining(const UUID& uuid) const
        { return joined.find(uuid) != joined.end(); }
        
        bool is_leaving(const UUID& uuid) const
        { return left.find(uuid) != left.end(); }

        bool is_partitioning(const UUID& uuid) const
        { return partitioned.find(uuid) != partitioned.end(); }
        
        
        ViewType      get_type           () const;
        const ViewId& get_id             () const;
        const UUID&   get_representative () const;
        
        bool is_empty() const;
        
        size_t unserialize(const gu::byte_t* buf, const size_t buflen, const size_t offset)
            throw (gu::Exception);
        
        size_t serialize(gu::byte_t* buf, const size_t buflen, const size_t offset) const
            throw (gu::Exception);
        
        size_t serial_size() const;
        
        
    private:
        ViewId   view_id;
        NodeList members;
        NodeList joined;
        NodeList left;
        NodeList partitioned;
    };

    bool operator==(const gcomm::View&, const gcomm::View&);
    std::ostream& operator<<(std::ostream&, const View&);
    
} // namespace gcomm

#endif // _GCOMM_VIEW_HPP_
