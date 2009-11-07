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
    Link(const gcomm::UUID& uuid_,
         const std::string& addr_) :
        uuid(uuid_),
        addr(addr_)
    { }

    bool operator==(const Link& cmp) const
    { return (uuid == cmp.uuid && addr == cmp.addr); }

    bool operator<(const Link& cmp) const
    { 
        return (uuid < cmp.uuid ||
                (uuid == cmp.uuid && addr < cmp.addr));
        
    }

    const gcomm::UUID& get_uuid() const { return uuid; }
    const std::string& get_addr() const { return addr; }
private:
    UUID uuid;
    std::string addr;
};



class gcomm::gmcast::LinkMap
{
    typedef std::set<Link> MType;
public:
    LinkMap() : link_map() { }
    typedef MType::iterator iterator;
    typedef MType::const_iterator const_iterator;
    typedef MType::value_type value_type;
    
    std::pair<iterator, bool> insert(const Link& i) 
    { return link_map.insert(i); }
    
    iterator begin() { return link_map.begin(); }
    const_iterator begin() const { return link_map.begin(); }
    iterator end() { return link_map.end(); }
    const_iterator end() const { return link_map.end(); }
    const_iterator find(const value_type& vt) const { return link_map.find(vt); } 
    size_t size() const { return link_map.size(); }
    static const UUID& get_key(const_iterator i) { return i->get_uuid(); }
    static const Link& get_value(const_iterator i) { return *i; }
    static const UUID& get_key(const value_type& vt) { return vt.get_uuid(); }
    static const Link& get_value(const value_type& vt) { return vt; }
    bool operator==(const LinkMap& cmp) const 
    { return (link_map == cmp.link_map); }
private:
    MType link_map;
};

inline std::ostream& gcomm::gmcast::operator<<(std::ostream& os, const LinkMap& lm)
{
    for (LinkMap::const_iterator i = lm.begin(); i != lm.end();
         ++i)
    {
        os << "\n(" << LinkMap::get_key(i) << ","
           << LinkMap::get_value(i).get_addr() << ")";
    }
    return (os << "\n");
}


#endif // GCOMM_GMCAST_LINK_HPP
