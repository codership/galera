
#include "gu_resolver.hpp"
#include "gu_logger.hpp"
#include "gu_string.hpp"

#include <stdexcept>
#include <map>

#include <cerrno>

#include <netdb.h>

class SchemeMap
{
public:
    typedef std::map<const std::string, struct addrinfo> Map;
    typedef Map::const_iterator const_iterator;
private:
    Map map;
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
        map()
    {
        map.insert(std::make_pair("tcp", get_addrinfo(0, AF_UNSPEC, SOCK_STREAM, 0)));
        // TODO:
    }

    const_iterator find(const std::string& key) const
    {
        return map.find(key);
    }

    const_iterator end() const
    {
        return map.end();
    }

};

static SchemeMap scheme_map;

void gu::net::Resolver::resolve(const std::string& scheme,
                                const std::string& authority,
                                struct addrinfo** ai)
{
    SchemeMap::const_iterator i;
    if ((i = scheme_map.find(scheme)) == scheme_map.end())
    {
        log_error << "invalid scheme: '" << scheme << "'";
        throw std::invalid_argument("invalid scheme");
    }

    std::vector<std::string> auth_split = strsplit(authority, ':');
    if (auth_split.size() != 2)
    {
        log_error << "invalid authority: '" << authority << "'";
        throw std::invalid_argument("invalid authority");
    }
    
    int err;
    if ((err = getaddrinfo(auth_split[0].c_str(), auth_split[1].c_str(), 
                           &i->second, ai)) != 0)
    {
        log_error << "getaddrinfo failed: " << errno;
        throw std::runtime_error("getaddrinfo failed");
    }

}
