/*!
 * @file Group view class (used in the ProtoUpMeta (protolay.hpp)
 */

#ifndef _GCOMM_VIEW_HPP_
#define _GCOMM_VIEW_HPP_

#include <gcomm/common.hpp>
#include <gcomm/uuid.hpp>
#include <gcomm/logger.hpp>
#include <gcomm/types.hpp>
#include <gcomm/map.hpp>

namespace gcomm
{
    class ViewId
    {
        UUID uuid; // uniquely identifies the sequence of group views (?)
        uint32_t    seq;  // position in the sequence                        (?)
    public:
        
        ViewId () : uuid(), seq(0) {}
        
        ViewId (const UUID& uuid_, const uint32_t seq_) :
            uuid  (uuid_),
            seq (seq_)
        {}
        
        const UUID& get_uuid()  const { return uuid; }
    
        uint32_t     get_seq() const { return seq; }
    
        size_t unserialize(const byte_t* buf, size_t buflen, size_t offset)
            throw (gu::Exception);
    
        size_t serialize(byte_t* buf, size_t buflen, size_t offset) const
            throw (gu::Exception);
    
        static size_t serial_size() 
        {
            return UUID::serial_size() + sizeof(reinterpret_cast<ViewId*>(0)->seq);
        }
    
        bool operator<(const ViewId& cmp) const
        {
            return (seq < cmp.seq || (seq == cmp.seq && uuid < cmp.uuid));
        }

        bool operator==(const ViewId& cmp) const
        {
            return uuid == cmp.uuid && seq == cmp.seq;
        }
    
        bool operator!=(const ViewId& cmp) const
        {
            return !(*this == cmp);
        }
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

    

    class NodeList : public gcomm::Map<gcomm::UUID, gcomm::Node> { };
    
    inline std::ostream& operator<<(std::ostream& os, const NodeList::value_type& vt)
    {
        return (os << "");
    }

    class View
    {    
    public:
        
        typedef enum
        {
            V_NONE,
            V_TRANS,
            V_REG,
            V_NON_PRIM,
            V_PRIM
        } Type;

        std::string to_string (const Type) const;

    private:

        Type     type;
        ViewId   view_id;
    
        NodeList members;
        NodeList joined;
        NodeList left;
        NodeList partitioned;
    
    public:

        View() :
            type        (V_NONE),
            view_id     (),
            members     (),
            joined      (),
            left        (),
            partitioned ()
        {}
    
        View(const Type type_, const ViewId& view_id_) : 
            type        (type_),
            view_id     (view_id_),
            members     (),
            joined      (),
            left        (),
            partitioned ()
        {}
    
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

        Type          get_type           () const;
        const ViewId& get_id             () const;
        const UUID&   get_representative () const;

        bool is_empty() const;

        size_t unserialize(const byte_t* buf, const size_t buflen, const size_t offset)
            throw (gu::Exception);

        size_t serialize(byte_t* buf, const size_t buflen, const size_t offset) const
            throw (gu::Exception);

        size_t serial_size() const;
    };

    bool operator==(const gcomm::View&, const gcomm::View&);
    std::ostream& operator<<(std::ostream&, const View&);
    
} // namespace gcomm

#endif // _GCOMM_VIEW_HPP_
