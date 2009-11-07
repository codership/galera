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

#include "gu_throw.hpp"

#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <string>

namespace gu
{
    // Forward decls
    class URI;
    namespace net
    {
        
        class Sockaddr
        {
        public:
            Sockaddr(const sockaddr*, socklen_t);
            Sockaddr(const Sockaddr&);
            ~Sockaddr();
            
            sa_family_t get_family() const { return sa->sa_family; }
            
            
            short get_port() const
            {
                switch(sa->sa_family)
                {
                case AF_INET:
                    return reinterpret_cast<const sockaddr_in*>(sa)->sin_port;
                case AF_INET6:
                    return reinterpret_cast<const sockaddr_in6*>(sa)->sin6_port;
                default:
                    gu_throw_fatal; throw;
                }
            }

            const void* get_addr() const
            {
                switch(sa->sa_family)
                {
                case AF_INET:
                    return &reinterpret_cast<const sockaddr_in*>(sa)->sin_addr;
                case AF_INET6:
                    return &reinterpret_cast<const sockaddr_in6*>(sa)->sin6_addr;
                default:
                    gu_throw_fatal; throw;
                }                
            }
            
            sockaddr& get_sockaddr() { return *sa; }
            const sockaddr& get_sockaddr() const { return *sa; }
            socklen_t get_sockaddr_len() const { return sa_len; }
            
        public:
            void operator=(const Sockaddr&);
            sockaddr* sa;
            socklen_t sa_len;
        };

        
        // Class encapsulating struct addrinfo 
        class Addrinfo
        {
        public:
            // Ctor
            Addrinfo(const addrinfo&);
            // 
            Addrinfo(const Addrinfo&);
            //
            Addrinfo(const Addrinfo&, const Sockaddr&);
            // Dtor
            ~Addrinfo();
            // Get address family, AF_INET, AF_INET6 etc
            int get_family() const { return ai.ai_family; }
            // Get socket type, SOCK_STREAM, SOCK_DGRAM etc
            int get_socktype() const { return ai.ai_socktype; }
            // 
            int get_protocol() const { return 0; }
            // Get sockaddr struct size
            socklen_t get_addrlen() const { return ai.ai_addrlen; }
            // Get pointer to sockaddr struct
            Sockaddr get_addr() const 
            { return Sockaddr(ai.ai_addr, ai.ai_addrlen); }
            // Get string representation of the addrinfo
            std::string to_string() const;
        private:
            addrinfo ai;
        };
        
        // Resolve address in URI
        Addrinfo resolve(const gu::URI&);
    } // namespace net
} // namespace gu

#endif /* __GU_RESOLVER_HPP__ */
