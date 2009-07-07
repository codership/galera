#ifndef VIEW_HPP
#define VIEW_HPP

#include <gcomm/common.hpp>
#include <gcomm/uuid.hpp>
#include <gcomm/string.hpp>
#include <gcomm/logger.hpp>

#include <map>

BEGIN_GCOMM_NAMESPACE

class ViewId
{
    UUID uuid;
    uint32_t seq;
public:
    ViewId() :
        uuid(),
        seq(0)
    {
        
    }
    
    ViewId(const UUID& uuid_, const uint32_t seq_) :
        uuid(uuid_),
        seq(seq_)
    {
        
    }

    uint32_t get_seq() const {
        return seq;
    }
    
    const UUID& get_uuid() const {
        return uuid;
    }
    
    size_t read(const byte_t* buf, const size_t buflen, const size_t offset);
    size_t write(byte_t* buf, const size_t buflen, const size_t offset) const;

    static size_t size() 
    {
        return UUID::size() + sizeof(uint32_t);
    }

    bool operator<(const ViewId& cmp) const
    {
        return uuid < cmp.uuid && seq < cmp.seq;
    }

    bool operator==(const ViewId& cmp) const
    {
        return uuid == cmp.uuid && seq == cmp.seq;
    }

    bool operator!=(const ViewId& cmp) const
    {
        return !(*this == cmp);
    }

    string to_string() const;
};




class NodeList 
{
public:
    typedef std::map<const UUID, std::string> Map;
    typedef Map::const_iterator const_iterator;
    typedef Map::iterator iterator;
private:
    Map nodes;
public:
    NodeList() : 
        nodes()
    {
    }
    ~NodeList() 
    {
    }
    
    const_iterator begin() const
    {
        return nodes.begin();
    }

    const_iterator end() const
    {
        return nodes.end();
    }
    
    const_iterator find(const UUID& uuid) const
    {
        return nodes.find(uuid);
    }

    std::pair<iterator, bool> insert(const std::pair<const UUID, const string>& p)
    {
        return nodes.insert(p);
    }

    bool empty() const
    {
        return nodes.empty();
    }

    bool operator==(const NodeList& other) const
    {
        return nodes == other.nodes;
    }

    size_t length() const;
    
    static const size_t node_name_size = 16;
    size_t read(const byte_t*, size_t, size_t);
    size_t write(byte_t*, size_t, size_t) const;
    size_t size() const;

};

static inline const UUID& get_uuid(const NodeList::const_iterator i)
{
    return i->first;
}

static inline const string& get_name(const NodeList::const_iterator i)
{
    return i->second;
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

    string to_string(const Type) const;
    


private:
    Type type;
    ViewId view_id;
    
    NodeList members;
    NodeList joined;
    NodeList left;
    NodeList partitioned;
    
    /* Map pid to human readable string */
    string pid_to_string(const UUID& pid) const;
public:
    View() :
        type(V_NONE),
        view_id(),
        members(),
        joined(),
        left(),
        partitioned()
    {
    }
    
    View(const Type type_, const ViewId& view_id_) : 
        type(type_),
        view_id(view_id_),
        members(),
        joined(),
        left(),
        partitioned()
    {
    }
    
    ~View()
    {
    }
    
    void add_member(const UUID& pid, const string& name);
    void add_members(NodeList::const_iterator begin, NodeList::const_iterator end);
    void add_joined(const UUID& pid, const string& name);
    void add_left(const UUID& pid, const string& name);
    void add_partitioned(const UUID& pid, const string& name);
    const NodeList& get_members() const;
    const NodeList& get_joined() const;
    const NodeList& get_left() const;
    const NodeList& get_partitioned() const;
    Type get_type() const;
    const ViewId& get_id() const;
    const UUID& get_representative() const;
    bool is_empty() const;
    size_t read(const byte_t* buf, const size_t buflen, const size_t offset);
    size_t write(byte_t* buf, const size_t buflen, const size_t offset) const;
    size_t size() const;
    string to_string() const;
};

bool operator==(const View&, const View&);


END_GCOMM_NAMESPACE

#endif // VIEW_HPP
