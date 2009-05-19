#ifndef VIEW_HPP
#define VIEW_HPP

#include <gcomm/common.hpp>
#include <gcomm/uuid.hpp>
#include <gcomm/string.hpp>
#include <gcomm/logger.hpp>

#include <map>

BEGIN_GCOMM_NAMESPACE

using std::map;

class ViewId
{
    UUID uuid;
    uint32_t seq;
public:
    ViewId() :
        uuid(),
        seq(-1)
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
    
    size_t read(const void* buf, const size_t buflen, const size_t offset);
    size_t write(void* buf, const size_t buflen, const size_t offset) const;

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




struct NodeList : map<const UUID, string>
{

    size_t length() const;
    
    static const size_t node_name_size = 16;
    size_t read(const void*, size_t, size_t);
    size_t write(void*, size_t, size_t) const;
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
        V_REG
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
        type(V_NONE)
    {
    }
    
    View(const Type type_, const ViewId& view_id_) : 
        type(type_),
        view_id(view_id_)
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
    size_t read(const void* buf, const size_t buflen, const size_t offset);
    size_t write(void* buf, const size_t buflen, const size_t offset) const;
    size_t size() const;
    string to_string() const;
};

bool operator==(const View&, const View&);


END_GCOMM_NAMESPACE

#endif // VIEW_HPP
