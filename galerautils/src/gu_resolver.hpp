#ifndef __GU_RESOLVER_HPP__
#define __GU_RESOLVER_HPP__

#include <string>

struct addrinfo;

namespace gu
{
    class Resolver
    {
    public:
        static void resolve(const std::string& scheme, 
                            const std::string& authority,
                            struct addrinfo** ai);
    };
}

#endif /* __GU_RESOLVER_HPP__ */
