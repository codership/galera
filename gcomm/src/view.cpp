/*
 * Copyright (C) 2009-2012 Codership Oy <info@codership.com>
 */

#include "common/common.h"

#include "gcomm/view.hpp"
#include "gcomm/types.hpp"
#include "gcomm/util.hpp"

#include "gu_logger.hpp"
#include "gu_exception.hpp"

#include <sstream>
#include <fstream>

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

std::ostream& gcomm::View::write_stream(std::ostream& os) const
{
    os << "#vwbeg" << std::endl;
    os << "view_id: ";
    view_id_.write_stream(os) << std::endl;
    os << "bootstrap: " << bootstrap_ << std::endl;
    for(NodeList::const_iterator it = members_.begin();
        it != members_.end(); ++it) {
        const UUID& uuid(it -> first);
        const Node& node(it -> second);
        os << "member: ";
        uuid.write_stream(os) << " ";
        node.write_stream(os) << std::endl;
    }
    os << "#vwend" << std::endl;
    return os;
}

std::istream& gcomm::View::read_stream(std::istream& is)
{
    std::string line;
    while(is.good()) {
        getline(is, line);
        std::istringstream istr(line);
        std::string param;
        istr >> param;
        if (param == "#vwbeg") continue;
        else if (param == "#vwend") break;
        if (param == "view_id:") {
            view_id_.read_stream(istr);
        } else if (param == "bootstrap:") {
            istr >> bootstrap_;
        } else if (param == "member:") {
            UUID uuid;
            Node node(0);
            uuid.read_stream(istr);
            node.read_stream(istr);
            add_member(uuid, node.segment());
        }
    }
    return is;
}

std::ostream& gcomm::ViewState::write_stream(std::ostream& os) const
{
    os << "my_uuid: ";
    my_uuid_.write_stream(os) << std::endl;
    view_.write_stream(os);
    return os;
}

std::istream& gcomm::ViewState::read_stream(std::istream& is)
{
    std::string param;
    std::string line;
    while(is.good()) {
        getline(is, line);
        std::istringstream istr(line);
        istr >> param;
        if (param == "my_uuid:") {
            my_uuid_.read_stream(istr);
        } else if (param == "#vwbeg") {
            // read from next line.
            view_.read_stream(is);
        }
    }
    return is;
}

void gcomm::ViewState::write_file() const
{
    // write to temporary file first.
    std::string tmp(COMMON_VIEW_STAT_FILE);
    tmp += ".tmp";
    FILE* fout = fopen(tmp.c_str(), "w");
    if (fout == NULL) {
        log_warn << "open file(" << tmp << ") failed("
                 << strerror(errno) << ")";
        return ;
    }
    std::ostringstream os;
    write_stream(os);
    std::string content(os.str());
    if (fwrite(content.c_str(), content.size(), 1, fout) == 0) {
        log_warn << "write file(" << tmp << ") failed("
                 << strerror(errno) << ")";
        fclose(fout);
        return ;
    }
    fflush(fout);
    fclose(fout);

    // rename atomically.
    if (rename(tmp.c_str(), COMMON_VIEW_STAT_FILE) != 0) {
        log_warn << "rename file(" << tmp << ") to file("
                 << COMMON_VIEW_STAT_FILE << ") failed("
                 << strerror(errno) << ")";
    }
}

bool gcomm::ViewState::read_file()
{
    if (access(COMMON_VIEW_STAT_FILE, R_OK) != 0) {
        log_warn << "access file(" << COMMON_VIEW_STAT_FILE << ") failed("
                 << strerror(errno) << ")";
        return false;
    }
    try {
        std::ifstream ifs(COMMON_VIEW_STAT_FILE, std::ifstream::in);
        read_stream(ifs);
        ifs.close();
        return true;
    } catch (const std::exception& e) {
        log_warn << "read file(" << COMMON_VIEW_STAT_FILE << ") failed("
                 << e.what() << ")";
        return false;
    }
}
