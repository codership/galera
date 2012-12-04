/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_GMCAST_LINK_HPP
#define GCOMM_GMCAST_LINK_HPP

#include "gcomm/uuid.hpp"

#include <set>
#include <string>

namespace gcomm
{
    namespace gmcast
    {
        class Link;
        class LinkMapCmp;
        class LinkMap;
        std::ostream& operator<<(std::ostream& os, const LinkMap&);
    }
}

class gcomm::gmcast::Link
{
public:
    Link(const gcomm::UUID& uuid,
         const std::string& addr,
         const std::string& mcast_addr) :
        uuid_      (uuid),
        addr_      (addr),
        mcast_addr_(mcast_addr)
    { }

    bool operator==(const Link& cmp) const
    { return (uuid_ == cmp.uuid_ && addr_ == cmp.addr_); }

    bool operator<(const Link& cmp) const
    {
        return (uuid_ < cmp.uuid_ ||
                (uuid_ == cmp.uuid_ && addr_ < cmp.addr_));

    }

    const gcomm::UUID& uuid() const { return uuid_; }
    const std::string& addr() const { return addr_; }
    const std::string& mcast_addr() const { return mcast_addr_; }
private:
    UUID uuid_;
    std::string addr_;
    std::string mcast_addr_;
};



class gcomm::gmcast::LinkMap
{
    typedef std::set<Link> MType;
public:
    LinkMap() : link_map_() { }
    typedef MType::iterator iterator;
    typedef MType::const_iterator const_iterator;
    typedef MType::value_type value_type;

    std::pair<iterator, bool> insert(const Link& i)
    { return link_map_.insert(i); }

    iterator begin() { return link_map_.begin(); }
    const_iterator begin() const { return link_map_.begin(); }
    iterator end() { return link_map_.end(); }
    const_iterator end() const { return link_map_.end(); }
    const_iterator find(const value_type& vt) const { return link_map_.find(vt); }
    size_t size() const { return link_map_.size(); }
    static const UUID& key(const_iterator i) { return i->uuid(); }
    static const Link& value(const_iterator i) { return *i; }
    static const UUID& key(const value_type& vt) { return vt.uuid(); }
    static const Link& value(const value_type& vt) { return vt; }
    bool operator==(const LinkMap& cmp) const
    { return (link_map_ == cmp.link_map_); }
private:
    MType link_map_;
};

inline std::ostream& gcomm::gmcast::operator<<(std::ostream& os, const LinkMap& lm)
{
    for (LinkMap::const_iterator i = lm.begin(); i != lm.end();
         ++i)
    {
        os << "\n(" << LinkMap::key(i) << ","
           << LinkMap::value(i).addr() << ")";
    }
    return (os << "\n");
}


#endif // GCOMM_GMCAST_LINK_HPP
