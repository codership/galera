/*
 * Copyright (C) 2009-2012 Codership Oy <info@codership.com>
 */

#include "gcomm/view.hpp"
#include "gcomm/types.hpp"
#include "gcomm/util.hpp"

#include "gu_logger.hpp"

#include <sstream>

size_t gcomm::ViewId::unserialize(const gu::byte_t* buf,
                                  const size_t buflen,
                                  const size_t offset)
{
    size_t off;

    gu_trace (off = uuid_.unserialize(buf, buflen, offset));
    uint32_t w;
    gu_trace (off = gu::unserialize4(buf, buflen, off, w));
    seq_ = w & 0x3fffffff;
    type_ = static_cast<ViewType>(w >> 30);
    return off;
}

size_t gcomm::ViewId::serialize(gu::byte_t* buf,
                                const size_t buflen,
                                const size_t offset) const
{
    size_t off;

    gcomm_assert(type_ != V_NONE);
    gu_trace (off = uuid_.serialize(buf, buflen, offset));
    uint32_t w((seq_ & 0x3fffffff) | (type_ << 30));
    gu_trace (off = gu::serialize4(w, buf, buflen, off));

    return off;
}


static std::string to_string(const gcomm::ViewType type)
{
    switch (type)
    {
    case gcomm::V_TRANS:    return "TRANS";
    case gcomm::V_REG:      return "REG";
    case gcomm::V_NON_PRIM: return "NON_PRIM";
    case gcomm::V_PRIM:     return "PRIM";
    default:
        return "UNKNOWN";
        // gcomm_throw_fatal << "Invalid type value";
    }
}

std::ostream& gcomm::operator<<(std::ostream& os, const gcomm::ViewId& vi)
{
    return (os << "view_id("
            << ::to_string(vi.type()) << ","
            << vi.uuid() << ","
            << vi.seq()) << ")";
}


void gcomm::View::add_member(const UUID& pid, SegmentId segment)
{
    gu_trace((void)members_.insert_unique(std::make_pair(pid, Node(segment))));
}

void gcomm::View::add_members(NodeList::const_iterator begin,
                              NodeList::const_iterator end)
{
    for (NodeList::const_iterator i = begin; i != end; ++i)
    {
        gu_trace((void)members_.insert_unique(
                     std::make_pair(NodeList::key(i),
                                    NodeList::value(i))));
    }
}

void gcomm::View::add_joined(const UUID& pid, SegmentId segment)
{
    gu_trace((void)joined_.insert_unique(std::make_pair(pid, Node(segment))));

}

void gcomm::View::add_left(const UUID& pid, SegmentId segment)
{
    gu_trace((void)left_.insert_unique(std::make_pair(pid, Node(segment))));
}

void gcomm::View::add_partitioned(const UUID& pid, SegmentId segment)
{
    gu_trace((void)partitioned_.insert_unique(std::make_pair(pid, Node(segment))));
}

const gcomm::NodeList& gcomm::View::members() const
{
    return members_;
}

const gcomm::NodeList& gcomm::View::joined() const
{
    return joined_;
}

const gcomm::NodeList& gcomm::View::left() const
{
    return left_;
}

const gcomm::NodeList& gcomm::View::partitioned() const
{
    return partitioned_;
}

gcomm::ViewType gcomm::View::type() const
{
    return view_id_.type();
}

const gcomm::ViewId& gcomm::View::id() const
{
    return view_id_;
}

const gcomm::UUID& gcomm::View::representative() const
{
    if (members_.empty())
    {
        return UUID::nil();
    }
    else
    {
        return NodeList::key(members_.begin());
    }
}

bool gcomm::View::is_empty() const
{
    return (view_id_.uuid() == UUID::nil() && members_.size() == 0);
}

bool gcomm::operator==(const gcomm::View& a, const gcomm::View& b)
{
    return (a.id()          == b.id()      &&
            a.members()     == b.members() &&
            a.joined()      == b.joined()  &&
            a.left()        == b.left()    &&
            a.partitioned() == b.partitioned());
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
        os << view.id();
        os << " memb {\n";
        os << view.members();
        os << "} joined {\n";
        os << view.joined();
        os << "} left {\n";
        os << view.left();
        os << "} partitioned {\n";
        os << view.partitioned();
        os << "}";
    }
    os << ")";
    return os;
}
