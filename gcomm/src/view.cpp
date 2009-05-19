
#include "gcomm/view.hpp"
#include "gcomm/logger.hpp"
#include "gcomm/types.hpp"
#include "gcomm/util.hpp"

BEGIN_GCOMM_NAMESPACE

size_t ViewId::read(const void* buf, const size_t buflen, const size_t offset)
{
    size_t off;
    if ((off = uuid.read(buf, buflen, offset)) == 0)
        return 0;
    if ((off = gcomm::read(buf, buflen, off, &seq)) == 0)
        return 0;
    return off;
}

size_t ViewId::write(void* buf, const size_t buflen, const size_t offset) const
{
    size_t off;
    if ((off = uuid.write(buf, buflen, offset)) == 0)
        return 0;
    if ((off = gcomm::write(seq, buf, buflen, off)) == 0)
        return 0;
    return off;
}

string ViewId::to_string() const
{
    return "view_id(" + uuid.to_string() + ":" + make_int(seq).to_string() + ")";
}

size_t NodeList::length() const
{
    return map<const UUID, string>::size();
}

size_t NodeList::read(const void* buf, const size_t buflen, const size_t offset)
{
    size_t off;
    uint32_t len;

    /* Clear map */
    clear();
    
    if ((off = gcomm::read(buf, buflen, offset, &len)) == 0)
    {
        LOG_WARN("read node list: read len");
        return 0;
    }
    for (uint32_t i = 0; i < len; ++i)
    {
        UUID uuid;
        char name[node_name_size + 1];
        if ((off = uuid.read(buf, buflen, off)) == 0)
        {
            LOG_WARN("read node list: read pid #" + make_int(i).to_string());
            return 0;
        }
        if ((off = read_bytes(buf, buflen, off, name, node_name_size)) == 0)
        {
            LOG_WARN("read node list: read name #" + make_int(i).to_string());
            return 0;
        }
        name[node_name_size] = '\0';
        if (insert(make_pair(uuid, string(name))).second == false)
        {
            LOG_WARN("read node list: duplicate entry: " + uuid.to_string());
            return 0;
        }
    }
    return off;
}

size_t NodeList::write(void* buf, const size_t buflen, const size_t offset) const
{
    size_t off;
        
    UInt32 len(length());
    if ((off = len.write(buf, buflen, offset)) == 0)
    {
        LOG_WARN("write node list: write len");
        return 0;
    }
    size_t cnt = 0;
    for (NodeList::const_iterator i = begin(); i != end(); ++i)
    {
        if ((off = get_uuid(i).write(buf, buflen, off)) == 0)
        {
            LOG_WARN("write node list: write pid #" + Size(cnt).to_string());
            return 0;
        }
        char name[node_name_size];
        strncpy(name, get_name(i).c_str(), node_name_size);
        if ((off = write_bytes(name, node_name_size, buf, buflen, off)) == 0)
        {
            LOG_WARN("write node list: write name #"
                     + make_int(cnt).to_string());
        }
        cnt++;
    }
    return off;
}

size_t NodeList::size() const
{
    return 4 + length()*(UUID::size() + node_name_size);
}


string View::to_string(const Type type) const
{
    switch (type)
    {
    case V_TRANS:
        return "TRANS";
    case V_REG:
        return "REG";
    default:
        break;
        /* Fall through to exception */
    }
    throw FatalException("invalid type value");
}

string View::pid_to_string(const UUID& pid) const
{
    string ret;
    NodeList::const_iterator i = members.find(pid);
    if (i != members.end())
    {
        ret = get_name(i);
    }
    else if ((i = left.find(pid)) != left.end())
    {
        ret = get_name(i);
    }
    else if ((i = partitioned.find(pid)) != partitioned.end())
    {
        ret = get_name(i);
    }
    else
    {
        string str = "view_id: " + view_id.to_string() + " ";
        for (i = members.begin(); i != members.end(); ++i)
        {
            str += "memb: " + get_uuid(i).to_string() + ":" + get_name(i) + " ";
        }
        LOG_FATAL("pid '" + pid.to_string() + "' not in view: " + str);
        throw FatalException("");
    }
    return ret;
}

void View::add_member(const UUID& pid, const string& name)
{
    if (members.insert(make_pair(pid, name)).second == false)
    {
        LOG_FATAL("member " + pid.to_string() + " already exists");
        throw FatalException("member already exists");
    }
}
    
void View::add_members(NodeList::const_iterator begin, NodeList::const_iterator end)
{
    for (NodeList::const_iterator i = begin; i != end; ++i)
    {
        if (members.insert(make_pair(get_uuid(i), get_name(i))).second == false)
        {
            LOG_FATAL("member " + get_uuid(i).to_string() + " already exists");
            throw FatalException("member already exists");
        }
    }
}

void View::add_joined(const UUID& pid, const string& name)
{
    if (joined.insert(make_pair(pid, name)).second == false)
    {
        LOG_FATAL("joiner " + pid.to_string() + " already exists");
        throw FatalException("joiner already exists");
    }
}
    
void View::add_left(const UUID& pid, const string& name)
{
    if (left.insert(make_pair(pid, name)).second == false)
    {
        LOG_FATAL("leaver " + pid.to_string() + " already exists");
        throw FatalException("leaver already exists");
    }
}

void View::add_partitioned(const UUID& pid, const string& name)
{
    if (partitioned.insert(make_pair(pid, name)).second == false)
    {
        LOG_FATAL("partitioned " + pid.to_string() + " already exists");
        throw FatalException("partitioned already exists");
    }
}
    
const NodeList& View::get_members() const
{
    return members;
}
    
const NodeList& View::get_joined() const
{
    return joined;
}
    
const NodeList& View::get_left() const
{
    return left;
}
    
const NodeList& View::get_partitioned() const
{
    return partitioned;
}
    
View::Type View::get_type() const
{
    return type;
}

const ViewId& View::get_id() const
{
    return view_id;
}

const UUID& View::get_representative() const
{
    if (members.empty())
    {
        return UUID::nil();
    }
    else
    {
        return get_uuid(members.begin());
    }
}

bool View::is_empty() const
{
    return view_id == ViewId() && members.length() == 0;
}

bool operator==(const View& a, const View& b)
{
    return a.get_id() == b.get_id() && 
        a.get_type() == b.get_type() &&
        a.get_members() == b.get_members() &&
        a.get_joined() == b.get_joined() &&
        a.get_left() == b.get_left() &&
        a.get_partitioned() == b.get_partitioned();
}



size_t View::read(const void* buf, const size_t buflen, const size_t offset)
{
    size_t off;
    UInt32 w;

    if ((off = w.read(buf, buflen, offset)) == 0)
    {
        LOG_WARN("read type");
        return 0;
    }
    type = static_cast<Type>(w.get());
    if (type != V_TRANS && type != V_REG)
    {
        LOG_WARN("invalid type: " + UInt32(w).to_string());
        return 0;
    }
    if ((off = view_id.read(buf, buflen, off)) == 0)
    {
        LOG_WARN("read view id");
        return 0;
    }
    if ((off = members.read(buf, buflen, off)) == 0)
    {
        LOG_WARN("read members");
        return 0;
    }
    if ((off = joined.read(buf, buflen, off)) == 0)
    {
        LOG_WARN("read joined");
        return 0;
    }
    if ((off = left.read(buf, buflen, off)) == 0)
    {
        LOG_WARN("read left");
        return 0;
    }
    if ((off = partitioned.read(buf, buflen, off)) == 0)
    {
        LOG_WARN("read partitioned");
    }
    return off;
}

size_t View::write(void* buf, const size_t buflen, const size_t offset) const
{
    size_t off;
    UInt32 w(type);
    if ((off = w.write(buf, buflen, offset)) == 0)
    {
        LOG_WARN("write type");
        return 0;
    }
    if ((off = view_id.write(buf, buflen, off)) == 0)
    {
        LOG_WARN("write view id");
        return 0;
    }
    if ((off = members.write(buf, buflen, off)) == 0)
    {
        LOG_WARN("write members");
        return 0;
    }
    if ((off = joined.write(buf, buflen, off)) == 0)
    {
        LOG_WARN("write joined");
        return 0;
    }
    if ((off = left.write(buf, buflen, off)) == 0)
    {
        LOG_WARN("write left");
        return 0;
    }
    if ((off = partitioned.write(buf, buflen, off)) == 0)
    {
        LOG_WARN("write partitioned");
        return 0;
    }
    return off;
}

size_t View::size() const
{
    return 4 + view_id.size() + members.size() + joined.size() + left.size() 
        + partitioned.size();
}

    
string View::to_string() const
{
    string ret = " VIEW: ";
    const UUID& repr_pid = view_id.get_uuid();
    if (is_empty() == true)
    {
        ret += "(empty)";
        return ret;
    }
    
    NodeList::const_iterator i;
    
    ret += to_string(type) + " (" + repr_pid.to_string() + "," 
        + make_int(view_id.get_seq()).to_string() + ") members {";
    for (i = members.begin(); i != members.end(); ++i)
    {
        NodeList::const_iterator i_next = i;
        ++i_next;
        ret += "(" + get_uuid(i).to_string() + ":" + get_name(i) + ")";;
        if (i_next != members.end())
        {
            ret += ",";
        }
    }
    ret += "}";
    ret += " joined {";
    for (i = joined.begin(); i != joined.end(); ++i)
    {
        NodeList::const_iterator i_next = i;
        ++i_next;
        ret += "(" + get_uuid(i).to_string() + ":" + get_name(i) + ")";;
        if (i_next != joined.end())
        {
            ret += ",";
        }
    }
    ret += "}";
    ret += " left {";
    for (i = left.begin(); i != left.end(); ++i)
    {
        NodeList::const_iterator i_next = i;
        ++i_next;
        ret += "(" + get_uuid(i).to_string() + ":" + get_name(i) + ")";;
        if (i_next != left.end())
        {
            ret += ",";
        }
    }
    ret += "}";
    ret += " partitioned {";
    for (i = partitioned.begin(); i != partitioned.end(); ++i)
    {
        NodeList::const_iterator i_next = i;
        ++i_next;
        ret += "(" + get_uuid(i).to_string() + ":" + get_name(i) + ")";;
        if (i_next != partitioned.end())
        {
            ret += ",";
        }
    }
    ret += "}";
    return ret;
}

END_GCOMM_NAMESPACE
