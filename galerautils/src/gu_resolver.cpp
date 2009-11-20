// Copyright (C) 2009 Codership Oy <info@codership.com>

#include "gu_resolver.hpp"
#include "gu_logger.hpp"
#include "gu_string.hpp"
#include "gu_utils.hpp"
#include "gu_throw.hpp"
#include "gu_uri.hpp"

#include <cerrno>
#include <cstdlib>
#include <netdb.h>
#include <arpa/inet.h>

#include <map>
#include <stdexcept>

using namespace std;


// Map from scheme string to addrinfo
class SchemeMap
{
public:

    typedef map<string, addrinfo> Map;
    typedef Map::const_iterator const_iterator;
    
    SchemeMap() : ai_map()
    {
        
        ai_map.insert(make_pair("tcp",
                                get_addrinfo(0, AF_UNSPEC, SOCK_STREAM, 0)));
        ai_map.insert(make_pair("udp",
                                get_addrinfo(0, AF_UNSPEC, SOCK_DGRAM,  0)));
        // TODO:
    }
    
    const_iterator find(const string& key) const
    {
        return ai_map.find(key);
    }
    
    const_iterator end() const
    {
        return ai_map.end();
    }
    
    static const addrinfo* get_addrinfo(const_iterator i)
    {
        return &i->second;
    }
    
private:
    
    Map ai_map;
    
    struct addrinfo get_addrinfo(int flags, int family, int socktype,
                                 int protocol)
    {
        struct addrinfo ret = {
            flags,
            family,
            socktype,
            protocol,
            sizeof(struct sockaddr),
            0,
            0,
            0
        };
        return ret;
    }
};

static SchemeMap scheme_map;


// Helper to copy addrinfo structs.
static void copy(const addrinfo& from, addrinfo& to)
{
    to.ai_flags = from.ai_flags;
    to.ai_family = from.ai_family;
    to.ai_socktype = from.ai_socktype;
    to.ai_protocol = from.ai_protocol;
    to.ai_addrlen = from.ai_addrlen;
    
    if (from.ai_addr != 0)
    {
        if ((to.ai_addr =
             reinterpret_cast<sockaddr*>(malloc(to.ai_addrlen))) == 0)
        {
            gu_throw_fatal 
                << "out of memory while trying to allocate " 
                << to.ai_addrlen << " bytes";
        }
        
        memcpy(to.ai_addr, from.ai_addr, to.ai_addrlen);
    }
    
    to.ai_canonname = 0;
    to.ai_next = 0;
}



/////////////////////////////////////////////////////////////////////////
//                     Sockaddr implementation
/////////////////////////////////////////////////////////////////////////


gu::net::Sockaddr::Sockaddr(const sockaddr* sa, socklen_t sa_len) :
    sa_    (0     ),
    sa_len_(sa_len)
{
    
    if ((sa_ = reinterpret_cast<sockaddr*>(malloc(sa_len_))) == 0)
    {
        gu_throw_fatal;
    }
    memcpy(sa_, sa, sa_len_);
}


gu::net::Sockaddr::Sockaddr(const Sockaddr& s) :
    sa_    (0        ),
    sa_len_(s.sa_len_)
{
    if ((sa_ = reinterpret_cast<sockaddr*>(malloc(sa_len_))) == 0)
    {
        gu_throw_fatal;
    }
    memcpy(sa_, s.sa_, sa_len_);
}


gu::net::Sockaddr::~Sockaddr()
{
    free(sa_);
}



/////////////////////////////////////////////////////////////////////////
//                     Addrinfo implementation
/////////////////////////////////////////////////////////////////////////


gu::net::Addrinfo::Addrinfo(const addrinfo& ai) :
    ai_()
{
    copy(ai, ai_);
}


gu::net::Addrinfo::Addrinfo(const Addrinfo& ai) :
    ai_()
{ 
    copy(ai.ai_, ai_);
}


gu::net::Addrinfo::Addrinfo(const Addrinfo& ai, const Sockaddr& sa) :
    ai_()
{
    if (ai.get_addrlen() != sa.get_sockaddr_len())
    {
        gu_throw_fatal;
    }
    copy(ai.ai_, ai_);
    memcpy(ai_.ai_addr, &sa.get_sockaddr(), ai_.ai_addrlen);
}


gu::net::Addrinfo::~Addrinfo()
{
    free(ai_.ai_addr);
}


string gu::net::Addrinfo::to_string() const
{
    static const size_t max_addr_str_len = (6 /* tcp|udp:// */ +
                                            INET6_ADDRSTRLEN + 2 /* [] */ +
                                            6 /* :portt */);
    string ret;
    
    ret.reserve(max_addr_str_len);
    
    Sockaddr addr(ai_.ai_addr, ai_.ai_addrlen);

    switch (get_socktype())
    {
    case SOCK_STREAM:
        ret += "tcp://";
        break;
    case SOCK_DGRAM:
        ret += "udp://";
        break;
    default:
        gu_throw_error(EINVAL) << "invalid socktype: " << get_socktype();
    }
    
    char dst[INET6_ADDRSTRLEN + 1];

    if (inet_ntop(get_family(), addr.get_addr(), 
                  dst, sizeof(dst)) == 0)
    {
        gu_throw_error(errno) << "inet ntop failed";
    }
    
    switch (get_family())
    {
    case AF_INET:
        ret += dst;
        break;
    case AF_INET6:
        ret += "[";
        ret += dst;
        ret += "]";
        break;
    default:
        gu_throw_error(EINVAL) << "invalid address family: " << get_family();
    }
    
    ret += ":" + gu::to_string(ntohs(addr.get_port()));
    ret.reserve(0); // free unused space if possible
    return ret;
}




/////////////////////////////////////////////////////////////////////////
//                       Public methods
/////////////////////////////////////////////////////////////////////////


gu::net::Addrinfo gu::net::resolve(const URI& uri)
{
    SchemeMap::const_iterator i(scheme_map.find(uri.get_scheme()));
    if (i == scheme_map.end())
    {
        gu_throw_error(EINVAL) << "invalid scheme: " << uri.get_scheme();
    }
    
    try
    {
        string host(uri.get_host());
        // remove [] if this is IPV6 address
        size_t pos(host.find_first_of('['));
        if (pos != string::npos)
        {
            host.erase(pos, pos + 1);
            pos = host.find_first_of(']');
            if (pos == string::npos)
            {
                gu_throw_error(EINVAL) << "invalid host: " << uri.get_host(); 

            } 
            host.erase(pos, pos + 1);
        }
        
        int err;
        addrinfo* ai(0);
        if ((err = getaddrinfo(host.c_str(), uri.get_port().c_str(),
                               SchemeMap::get_addrinfo(i), &ai)) == -1)
        {
            gu_throw_error(errno) << "getaddrinfo failed for: " << uri.to_string();
        }
        // Assume that the first entry is ok
        Addrinfo ret(*ai);
        freeaddrinfo(ai);
        return ret;
    }
    catch (NotFound& nf)
    {
        gu_throw_error(EINVAL) << "invalid URI: " << uri.to_string();
        throw;
    }
}
