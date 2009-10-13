#include <sstream>

#include "gcomm/view.hpp"
#include "gcomm/logger.hpp"
#include "gcomm/types.hpp"
#include "gcomm/util.hpp"

using namespace std;
using namespace gcomm;

size_t gcomm::ViewId::unserialize(const byte_t* buf, 
                                  const size_t buflen, 
                                  const size_t offset)
    throw (gu::Exception)
{
    size_t off;
    
    gu_trace (off = uuid.unserialize(buf, buflen, offset));
    gu_trace (off = gcomm::unserialize(buf, buflen, off, &seq));
    
    return off;
}

size_t gcomm::ViewId::serialize(byte_t* buf, 
                                const size_t buflen, 
                                const size_t offset)
    const throw (gu::Exception)
{
    size_t off;
    
    gu_trace (off = uuid.serialize(buf, buflen, offset));
    gu_trace (off = gcomm::serialize(seq, buf, buflen, off));
    
    return off;
}

ostream& gcomm::operator<<(ostream& os, const gcomm::ViewId& vid)
{
    return (os << "(" << vid.get_uuid() << "," << vid.get_seq()) << ")";
}


string gcomm::View::to_string(const Type type) const
{
    switch (type)
    {
    case V_TRANS:    return "TRANS";
    case V_REG:      return "REG";
    case V_NON_PRIM: return "NON_PRIM";
    case V_PRIM:     return "PRIM";
    default:
        gcomm_throw_fatal << "Invalid type value"; throw;
    }
}


void gcomm::View::add_member(const UUID& pid, const string& name)
{
    gu_trace((void)members.insert_checked(make_pair(pid, Node())));
}
    
void gcomm::View::add_members(NodeList::const_iterator begin,
                              NodeList::const_iterator end)
{
    for (NodeList::const_iterator i = begin; i != end; ++i)
    {
        gu_trace((void)members.insert_checked(
                     make_pair(NodeList::get_key(i), 
                               NodeList::get_value(i))));
    }
}

void gcomm::View::add_joined(const UUID& pid, const string& name)
{
    gu_trace((void)joined.insert_checked(make_pair(pid, Node())));

}

void gcomm::View::add_left(const UUID& pid, const string& name)
{
    gu_trace((void)left.insert_checked(make_pair(pid, Node())));
}

void gcomm::View::add_partitioned(const UUID& pid, const string& name)
{
    gu_trace((void)partitioned.insert_checked(make_pair(pid, Node())));
}

const NodeList& gcomm::View::get_members() const
{
    return members;
}

const NodeList& gcomm::View::get_joined() const
{
    return joined;
}

const NodeList& gcomm::View::get_left() const
{
    return left;
}

const NodeList& gcomm::View::get_partitioned() const
{
    return partitioned;
}
    
View::Type gcomm::View::get_type() const
{
    return type;
}

const gcomm::ViewId& View::get_id() const
{
    return view_id;
}

const UUID& gcomm::View::get_representative() const
{
    if (members.empty())
    {
        return UUID::nil();
    }
    else
    {
        return NodeList::get_key(members.begin());
    }
}

bool gcomm::View::is_empty() const
{
    return view_id == ViewId() && members.size() == 0;
}

bool gcomm::operator==(const gcomm::View& a, const gcomm::View& b)
{
    return a.get_id()   == b.get_id() && 
        a.get_type()    == b.get_type() &&
        a.get_members() == b.get_members() &&
        a.get_joined()  == b.get_joined() &&
        a.get_left()    == b.get_left() &&
        a.get_partitioned() == b.get_partitioned();
}



size_t gcomm::View::unserialize(const byte_t* buf, const size_t buflen, const size_t offset)
    throw (gu::Exception)
{
    size_t off;
    uint32_t w;
    
    gu_trace (off = gcomm::unserialize(buf, buflen, offset, &w));

    type = static_cast<Type>(w);

    if (type != V_TRANS && type != V_REG)
    {
        gcomm_throw_runtime (EINVAL) << "Invalid type: " << w;
    }
    
    gu_trace (off = view_id.unserialize    (buf, buflen, off));
    gu_trace (off = members.unserialize    (buf, buflen, off));
    gu_trace (off = joined.unserialize     (buf, buflen, off));
    gu_trace (off = left.unserialize       (buf, buflen, off));
    gu_trace (off = partitioned.unserialize(buf, buflen, off));

    return off;
}

size_t gcomm::View::serialize(byte_t* buf, const size_t buflen, const size_t offset) const
    throw (gu::Exception)
{
    size_t   off;
    uint32_t w(type);

    gu_trace (off = gcomm::serialize  (w, buf, buflen, offset));
    gu_trace (off = view_id.serialize    (buf, buflen, off));
    gu_trace (off = members.serialize    (buf, buflen, off));
    gu_trace (off = joined.serialize     (buf, buflen, off));
    gu_trace (off = left.serialize       (buf, buflen, off));
    gu_trace (off = partitioned.serialize(buf, buflen, off));

    return off;
}

size_t gcomm::View::serial_size() const
{
    return 4 + view_id.serial_size() 
        + members.serial_size() 
        + joined.serial_size() 
        + left.serial_size() 
        + partitioned.serial_size();
}


std::ostream& gcomm::operator<<(std::ostream& os, const gcomm::View& view)
{
    os << "View (" << view.to_string(view.get_type()) << "):";
    if (view.is_empty() == true)
    {
        os << "(empty)";
    }
    else
    {
        os << view.get_id();
        os << " memb (";
        os << view.get_members();
        os << ") joined (";
        os << view.get_joined();
        os << ") left (";
        os << view.get_left();
        os << ") partitioned (";
        os << view.get_partitioned();
        os << ")";
    }
    return os;
}
