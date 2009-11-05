
#include "gu_resolver.hpp"
#include "gu_logger.hpp"
#include "gu_string.hpp"
#include "gu_utils.hpp"
#include "gu_throw.hpp"

#include <map>

#include <cerrno>

#include <netdb.h>
#include <arpa/inet.h>

using namespace std;

class SchemeMap
{
public:
    typedef map<string, struct addrinfo> Map;
    typedef Map::const_iterator const_iterator;
private:
    Map ai_map;
    struct addrinfo get_addrinfo(int flags, int family, int socktype, int protocol)
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
public:
    SchemeMap() :
        ai_map()
    {
        ai_map.insert(make_pair("tcp", get_addrinfo(0, AF_INET, SOCK_STREAM, 0)));
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
    
};

static SchemeMap scheme_map;

void gu::net::Resolver::resolve(const string& scheme,
                                const string& authority,
                                addrinfo** ai)
{
    SchemeMap::const_iterator i;
    if ((i = scheme_map.find(scheme)) == scheme_map.end())
    {
        gu_throw_error(EINVAL) << "invalid scheme: " << scheme;
    }
    
    vector<string> auth_split = strsplit(authority, ':');
    if (auth_split.size() != 2)
    {
        gu_throw_error(EINVAL) << "invalid authority" << authority;
    }
    
    int err;
    if ((err = getaddrinfo(auth_split[0].c_str(), auth_split[1].c_str(), 
                           &i->second, ai)) != 0)
    {
        gu_throw_error(errno) << "getaddrinfo failed";
    }
}


string gu::net::Resolver::addrinfo_to_string(const addrinfo& ai)
{
    if (ai.ai_addr == 0)
    {
        gu_throw_error(EINVAL) << "null address";
    }
    
    string ret;
    switch (ai.ai_socktype)
    {
    case SOCK_STREAM:
        ret += "tcp://";
        break;
    case SOCK_DGRAM:
        ret += "udp://";
        break;
    default:
        gu_throw_error(EINVAL) << "invalid socket type: " << ai.ai_socktype;
        throw;
    }
    
    char dst[INET6_ADDRSTRLEN + 1];
    int port;
    const sockaddr& sa(*ai.ai_addr);
    
    if (sa.sa_family == AF_INET)
    {
        const sockaddr_in* sin(reinterpret_cast<const sockaddr_in*>(&sa));
        in_addr_t addr = sin->sin_addr.s_addr;
        if (inet_ntop(sin->sin_family, 
                      &addr, dst, sizeof(dst)) == 0)
        {
            gu_throw_error(EINVAL);
        }
        port = sin->sin_port;
    }
    else if (sa.sa_family == AF_INET6)
    {
        const sockaddr_in6* sin(reinterpret_cast<const sockaddr_in6*>(&sa));
        if (inet_ntop(sin->sin6_family, 
                      &sin->sin6_addr, dst, sizeof(dst)) == 0)
        {
            gu_throw_error(EINVAL);
        }
        port = sin->sin6_port;
    }
    else
    {
        gu_throw_error(EINVAL) 
            << "unsupported address family: " << sa.sa_family; 
        throw;
    }
    ret += dst;
    ret += ":" + to_string(ntohs(port));
    return ret;
}
