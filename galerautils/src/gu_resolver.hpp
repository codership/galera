/*
 * Copyright (C) 2008-2012 Codership Oy <info@codership.com>
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


// Forward declarations
namespace gu
{
    class URI;
} // namespace gu

// Declarations
namespace gu
{
    namespace net
    {
        /*!
         * @class Sockaddr
         *
         * @brief Class encapsulating struct sockaddr.
         *
         * Class encapsulating struct sockaddr and providing
         * simple interface to access sockaddr fields.
         */
        class Sockaddr;

        /*!
         * @class IMReq
         *
         * @brief Class encapsulating imreq structs.
         */
        class MReq;

        /*!
         * @class Addrinfo
         *
         * @brief Class encapsulating struct addrinfo.
         *
         * Class encapsulating struct addrinfo and providing interface
         * to access addrinfo fields.
         */
        class Addrinfo;

        /*!
         * Resolve address given in @uri
         *
         * @return Addrinfo object representing address
         *
         * @throw gu::Exception in case of failure
         */
        Addrinfo resolve(const gu::URI& uri);
    } // namespace net
} // namespace gu


class gu::net::Sockaddr
{
public:
    /*!
     * Default constuctor.
     *
     * @param sa     Pointer to sockaddr struct
     * @param sa_len Length  of sockaddr struct
     */
    Sockaddr(const sockaddr* sa, socklen_t sa_len);

    /*!
     * Copy constructor.
     *
     * @param sa Reference to Sockaddr
     */
    Sockaddr(const Sockaddr& sa);

    /*!
     * Destructor
     */
    ~Sockaddr();

    /*!
     * Get address family.
     *
     * @return Address family
     */
    sa_family_t get_family() const { return sa_->sa_family; }

    /*!
     * Get port in network byte order. This is applicable only
     * for AF_INET, AF_INET6.
     *
     * @return Port in nework byte order
     */
    unsigned short get_port() const
    {
        switch(sa_->sa_family)
        {
        case AF_INET:
            return reinterpret_cast<const sockaddr_in*>(sa_)->sin_port;
        case AF_INET6:
            return reinterpret_cast<const sockaddr_in6*>(sa_)->sin6_port;
        default:
            gu_throw_fatal;
        }
    }

    /*!
     * Get pointer to address. Return value is pointer to void,
     * user must do casting by himself.
     *
     * @todo: Figure out how this could be done in type safe way.
     *
     * @return Void pointer to address element.
     */
    const void* get_addr() const
    {
        switch(sa_->sa_family)
        {
        case AF_INET:
            return &reinterpret_cast<const sockaddr_in*>(sa_)->sin_addr;
        case AF_INET6:
            return &reinterpret_cast<const sockaddr_in6*>(sa_)->sin6_addr;
        default:
            gu_throw_fatal  << "invalid address family: " << sa_->sa_family;
        }
    }

    socklen_t get_addr_len() const
    {
        switch(sa_->sa_family)
        {
        case AF_INET:
            return sizeof(reinterpret_cast<const sockaddr_in*>(sa_)->sin_addr);
        case AF_INET6:
            return sizeof(reinterpret_cast<const sockaddr_in6*>(sa_)->sin6_addr);
        default:
            gu_throw_fatal;
        }
    }

    /*!
     * Get non-const reference to sockaddr struct.
     *
     * @return Non-const reference to sockaddr struct.
     */
    sockaddr& get_sockaddr() { return *sa_; }

    /*!
     * Get const reference to sockaddr struct.
     *
     * @return Const reference to sockaddr struct.
     */
    const sockaddr& get_sockaddr() const { return *sa_; }

    /*!
     * Get length of sockaddr struct.
     *
     * @return Length of sockaddr struct
     */
    socklen_t get_sockaddr_len() const { return sa_len_; }

    bool is_multicast() const;
    bool is_broadcast() const;
    bool is_anyaddr()   const;

    static Sockaddr get_anyaddr(const Sockaddr& sa)
    {
        Sockaddr ret(sa);
        switch(ret.sa_->sa_family)
        {
        case AF_INET:
            reinterpret_cast<sockaddr_in*>(ret.sa_)->sin_addr.s_addr = 0;
            break;
        case AF_INET6:
            memset(&reinterpret_cast<sockaddr_in6*>(ret.sa_)->sin6_addr,
                   0, sizeof(struct in6_addr));
            break;
        default:
            gu_throw_fatal << "invalid address family: " << ret.sa_->sa_family;
        }
        return ret;
    }

    Sockaddr& operator=(const Sockaddr& sa)
    {
        memcpy(sa_, sa.sa_, sa_len_);
        return *this;
    }

private:

    sockaddr* sa_;
    socklen_t sa_len_;
};


class gu::net::MReq
{
public:

    MReq(const Sockaddr& mcast_addr, const Sockaddr& if_addr);
    ~MReq();

    const void* get_mreq() const        { return mreq_; }
    socklen_t get_mreq_len() const      { return mreq_len_; }
    int get_ipproto() const             { return ipproto_; }
    int get_add_membership_opt() const  { return add_membership_opt_; }
    int get_drop_membership_opt() const { return drop_membership_opt_; }
    int get_multicast_if_opt() const    { return multicast_if_opt_; }
    int get_multicast_loop_opt() const  { return multicast_loop_opt_; }
    int get_multicast_ttl_opt() const   { return multicast_ttl_opt_; }
    const void* get_multicast_if_value() const;
    int get_multicast_if_value_size() const;

private:

    MReq(const MReq&);
    void operator=(const MReq&);

    void* mreq_;
    socklen_t mreq_len_;
    int ipproto_;
    int add_membership_opt_;
    int drop_membership_opt_;
    int multicast_if_opt_;
    int multicast_loop_opt_;
    int multicast_ttl_opt_;
};


class gu::net::Addrinfo
{
public:
    /*!
     * Default constructor.
     *
     * @param ai Const reference to addrinfo struct
     */
    Addrinfo(const addrinfo& ai);

    /*!
     * Copy costructor.
     *
     * @param ai Const reference to Addrinfo object to copy
     */
    Addrinfo(const Addrinfo& ai);

    /*!
     * Copy constructor that replaces @ai sockaddr struct.
     *
     * @param ai Const reference to Addrinfo object to copy
     * @param sa Const reference to Sockaddr struct that replaces
     *           @ai sockaddr data
     */
    Addrinfo(const Addrinfo& ai, const Sockaddr& sa);

    /*!
     * Destructor.
     */
    ~Addrinfo();

    /*!
     * Get address family, AF_INET, AF_INET6 etc.
     *
     * @return Address family
     */
    int get_family() const { return ai_.ai_family; }

    /*!
     * Get socket type, SOCK_STREAM, SOCK_DGRAM etc
     *
     * @return Socket type
     */
    int get_socktype() const { return ai_.ai_socktype; }

    /*!
     * Get protocol.
     *
     * @return Protocol
     */
    int get_protocol() const { return ai_.ai_protocol; }

    /*!
     * Get length of associated sockaddr struct
     *
     * @return Length of associated sockaddr struct
     */
    socklen_t get_addrlen() const { return ai_.ai_addrlen; }

    /*!
     * Get associated Sockaddr object.
     *
     * @return Associated Sockaddr object
     */

    Sockaddr get_addr() const
    { return Sockaddr(ai_.ai_addr, ai_.ai_addrlen); }

    /*!
     * Get string representation of the addrinfo.
     *
     * @return String representation of the addrinfo
     */
    std::string to_string() const;

private:

    addrinfo ai_;
};


#endif /* __GU_RESOLVER_HPP__ */
