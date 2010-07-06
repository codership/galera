/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "gcomm/view.hpp"
#include "gcomm/types.hpp"
#include "gcomm/util.hpp"

#include "gu_logger.hpp"

#include <sstream>

using namespace std;
using namespace gcomm;

using namespace gu;

size_t gcomm::ViewId::unserialize(const byte_t* buf, 
                                  const size_t buflen, 
                                  const size_t offset)
    throw (gu::Exception)
{
    size_t off;
    
    gu_trace (off = uuid.unserialize(buf, buflen, offset));
    uint32_t w;
    gu_trace (off = gcomm::unserialize(buf, buflen, off, &w));
    seq = w & 0x3fffffff;
    type = static_cast<ViewType>(w >> 30);
    return off;
}

size_t gcomm::ViewId::serialize(byte_t* buf, 
                                const size_t buflen, 
                                const size_t offset)
    const throw (gu::Exception)
{
    size_t off;
 
    gcomm_assert(type != V_NONE);
    gu_trace (off = uuid.serialize(buf, buflen, offset));
    uint32_t w((seq & 0x3fffffff) | (type << 30));
    gu_trace (off = gcomm::serialize(w, buf, buflen, off));
    
    return off;
}


static string to_string(const ViewType type)
{
    switch (type)
    {
    case V_TRANS:    return "TRANS";
    case V_REG:      return "REG";
    case V_NON_PRIM: return "NON_PRIM";
    case V_PRIM:     return "PRIM";
    default:
        return "UNKNOWN";
        // gcomm_throw_fatal << "Invalid type value"; throw;
    }
}

ostream& gcomm::operator<<(ostream& os, const gcomm::ViewId& vi)
{
    return (os << "view_id(" 
            << ::to_string(vi.get_type()) << "," 
            << vi.get_uuid() << "," 
            << vi.get_seq()) << ")";
}


void gcomm::View::add_member(const UUID& pid, const string& name)
{
    gu_trace((void)members.insert_unique(make_pair(pid, Node())));
}
    
void gcomm::View::add_members(NodeList::const_iterator begin,
                              NodeList::const_iterator end)
{
    for (NodeList::const_iterator i = begin; i != end; ++i)
    {
        gu_trace((void)members.insert_unique(
                     make_pair(NodeList::get_key(i), 
                               NodeList::get_value(i))));
    }
}

void gcomm::View::add_joined(const UUID& pid, const string& name)
{
    gu_trace((void)joined.insert_unique(make_pair(pid, Node())));

}

void gcomm::View::add_left(const UUID& pid, const string& name)
{
    gu_trace((void)left.insert_unique(make_pair(pid, Node())));
}

void gcomm::View::add_partitioned(const UUID& pid, const string& name)
{
    gu_trace((void)partitioned.insert_unique(make_pair(pid, Node())));
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
    
ViewType gcomm::View::get_type() const
{
    return view_id.get_type();
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
    return view_id.get_uuid() == UUID::nil() && members.size() == 0;
}

bool gcomm::operator==(const gcomm::View& a, const gcomm::View& b)
{
    return a.get_id()   == b.get_id() && 
        a.get_members() == b.get_members() &&
        a.get_joined()  == b.get_joined() &&
        a.get_left()    == b.get_left() &&
        a.get_partitioned() == b.get_partitioned();
}



size_t gcomm::View::unserialize(const byte_t* buf, const size_t buflen, 
                                size_t offset)
    throw (gu::Exception)
{
    gu_trace (offset = view_id.unserialize    (buf, buflen, offset));
    gu_trace (offset = members.unserialize    (buf, buflen, offset));
    gu_trace (offset = joined.unserialize     (buf, buflen, offset));
    gu_trace (offset = left.unserialize       (buf, buflen, offset));
    gu_trace (offset = partitioned.unserialize(buf, buflen, offset));

    return offset;
}

size_t gcomm::View::serialize(byte_t* buf, const size_t buflen, 
                              size_t offset) const
    throw (gu::Exception)
{
    gu_trace (offset = view_id.serialize    (buf, buflen, offset));
    gu_trace (offset = members.serialize    (buf, buflen, offset));
    gu_trace (offset = joined.serialize     (buf, buflen, offset));
    gu_trace (offset = left.serialize       (buf, buflen, offset));
    gu_trace (offset = partitioned.serialize(buf, buflen, offset));
    return offset;
}

size_t gcomm::View::serial_size() const
{
    return view_id.serial_size() 
        + members.serial_size() 
        + joined.serial_size() 
        + left.serial_size() 
        + partitioned.serial_size();
}


std::ostream& gcomm::operator<<(std::ostream& os, const gcomm::View& view)
{
    os << "view(";
    if (view.is_empty() == true)
    {
        os << "(empty)";
    }
    else
    {
        os << view.get_id();
        os << " memb {\n";
        os << view.get_members();
        os << "} joined {\n";
        os << view.get_joined();
        os << "} left {\n";
        os << view.get_left();
        os << "} partitioned {\n";
        os << view.get_partitioned();
        os << "}";
    }
    os << ")";
    return os;
}
