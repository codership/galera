/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*!
 * @file gu_resolver.hpp Simple resolver utility
 */
#ifndef __GU_RESOLVER_HPP__
#define __GU_RESOLVER_HPP__

#include <string>

/* Forward declarations */
struct addrinfo;

namespace gu
{
    namespace net
    {
        /*!
         * Name resolving
         */
        class Resolver
        {
        public:
            /*!
             * @brief Resolve address according to scheme and authority
             *
             * @param scheme Transport scheme (tcp, ...)
             * @param authoriry Authority in for of <hostname>:<port>
             * @param[out] ai Struct addrinfo as returned by getaddrinfo
             *
             * @throws std::invalid_argument If scheme was not recognized or
             *         if authority string was not well formed
             */
            static void resolve(const std::string& scheme, 
                                const std::string& authority,
                                struct addrinfo** ai);
        };
    }
}

#endif /* __GU_RESOLVER_HPP__ */
