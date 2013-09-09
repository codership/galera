// Copyright (C) 2009-2013 Codership Oy <info@codership.com>

#include "gu_resolver.hpp"
#include "gu_logger.hpp"
#include "gu_utils.hpp"
#include "gu_throw.hpp"
#include "gu_uri.hpp"

#include <cerrno>
#include <cstdlib>
#include <unistd.h> // for close()
#include <netdb.h>
#include <arpa/inet.h>
#include <net/if.h>
#define BSD_COMP /* For SIOCGIFCONF et al on Solaris */
#include <sys/ioctl.h>
#include <map>
#include <stdexcept>

#if defined(__APPLE__) || defined(__FreeBSD__)
# include <ifaddrs.h>
# define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
# define IPV6_DROP_MEMBERSHIP IPV6_LEAVE_GROUP
#else /* !__APPLE__ && !__FreeBSD__ */
extern "C" /* old style cast */
{
static int const GU_SIOCGIFCONF  = SIOCGIFCONF;
static int const GU_SIOCGIFINDEX = SIOCGIFINDEX;
}
#endif /* !__APPLE__ && !__FreeBSD__ */

//using namespace std;
using std::make_pair;

// Map from scheme string to addrinfo
class SchemeMap
{
public:

    typedef std::map<std::string, addrinfo> Map;
    typedef Map::const_iterator const_iterator;

    SchemeMap() : ai_map()
    {

        ai_map.insert(make_pair("tcp",
                                get_addrinfo(0, AF_UNSPEC, SOCK_STREAM, 0)));
        ai_map.insert(make_pair("ssl",
                                get_addrinfo(0, AF_UNSPEC, SOCK_STREAM, 0)));
        ai_map.insert(make_pair("udp",
                                get_addrinfo(0, AF_UNSPEC, SOCK_DGRAM,  0)));
        // TODO:
    }

    const_iterator find(const std::string& key) const
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
#if defined(__FreeBSD__)
	    0, // FreeBSD gives ENOMEM error with non-zero value
#else
            sizeof(struct sockaddr),
#endif
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

bool gu::net::Sockaddr::is_multicast() const
{
    switch (sa_->sa_family)
    {
    case AF_INET:
        return IN_MULTICAST(ntohl(reinterpret_cast<const sockaddr_in*>(sa_)->sin_addr.s_addr));
    case AF_INET6:
        return IN6_IS_ADDR_MULTICAST(&reinterpret_cast<const sockaddr_in6*>(sa_)->sin6_addr);
    default:
        gu_throw_fatal;
    }
}


bool gu::net::Sockaddr::is_anyaddr() const
{
    switch (sa_->sa_family)
    {
    case AF_INET:
        return (ntohl(reinterpret_cast<const sockaddr_in*>(sa_)->sin_addr.s_addr) == INADDR_ANY);
    case AF_INET6:
        return IN6_IS_ADDR_UNSPECIFIED(&reinterpret_cast<const sockaddr_in6*>(sa_)->sin6_addr);
    default:
        gu_throw_fatal;
    }
}


gu::net::Sockaddr::Sockaddr(const sockaddr* sa, socklen_t sa_len) :
    sa_    (0     ),
    sa_len_(sa_len)
{
    if ((sa_ = reinterpret_cast<sockaddr*>(malloc(sa_len_))) == 0) { gu_throw_fatal; }
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
//                     MReq implementation
/////////////////////////////////////////////////////////////////////////

static unsigned int get_ifindex_by_addr(const gu::net::Sockaddr& addr)
{

    if (addr.is_anyaddr() == true)
    {
        return 0;
    }

    unsigned int idx(-1);
    int err(0);
#if defined(__APPLE__) || defined(__FreeBSD__)
    struct ifaddrs *if_addrs = NULL;
    struct ifaddrs *if_addr = NULL;

    if (getifaddrs (&if_addrs) != 0)
    {
        err = errno;
        goto out;
    }
    for (if_addr = if_addrs; if_addr != NULL; if_addr = if_addr->ifa_next)
    {
        try
        {
            gu::net::Sockaddr sa (if_addr->ifa_addr, sizeof (struct sockaddr));
            if (sa.get_family () == addr.get_family () &&
                memcmp (sa.get_addr (), addr.get_addr (), addr.get_addr_len ()) == 0)
            {
                idx = if_nametoindex (if_addr->ifa_name);
                goto out;
            }
        }
        catch (gu::Exception& e)
        {
        }
    }

out:
# else /* !__APPLE__ && !__FreeBSD__ */
    struct ifconf ifc;
    memset(&ifc, 0, sizeof(struct ifconf));
    ifc.ifc_len = 16*sizeof(struct ifreq);
    std::vector<struct ifreq> ifr(16);
    ifc.ifc_req = &ifr[0];

    int fd(socket(AF_INET, SOCK_DGRAM, 0));
    if (fd == -1)
    {
        err = errno;
        gu_throw_error(err) << "could not create socket";
    }
    if ((err = ioctl(fd, GU_SIOCGIFCONF, &ifc)) == -1)
    {
        err = errno;
        goto out;
    }
    
    log_debug << "read: " << ifc.ifc_len;
    
    for (size_t i(0); i < ifc.ifc_len/sizeof(struct ifreq); ++i)
    {
        struct ifreq* ifrp(&ifr[i]);
        try
        {
            log_debug << "read: " << ifrp->ifr_name;
            gu::net::Sockaddr sa(&ifrp->ifr_addr, sizeof(struct sockaddr));
            if (sa.get_family() == addr.get_family() &&
                memcmp(sa.get_addr(), addr.get_addr(), addr.get_addr_len()) == 0)
            {
                if ((err = ioctl(fd, GU_SIOCGIFINDEX, ifrp, sizeof(struct ifreq))) == -1)
                {
                    err = errno;
                }
#if defined(__linux__)
                idx = ifrp->ifr_ifindex;
#elif defined(__sun__)
                idx = ifrp->ifr_index;
#else
# error "Unsupported ifreq structure"
#endif
                goto out;
            }
        }
        catch (gu::Exception& e)
        {
        }
    }
    
out:
    close(fd);
#endif /* !__APPLE__ && !__FreeBSD__ */
    if (err != 0)
    {
        gu_throw_error(err) << "failed to get interface index";
    }
    else
    {
        log_debug << "returning ifindex: " << idx;
    }
    return idx;
}


gu::net::MReq::MReq(const Sockaddr& mcast_addr, const Sockaddr& if_addr)
    :
    mreq_               ( 0),
    mreq_len_           ( 0),
    ipproto_            ( 0),
    add_membership_opt_ (-1),
    drop_membership_opt_(-1),
    multicast_if_opt_   (-1),
    multicast_loop_opt_ (-1),
    multicast_ttl_opt_  (-1)
{
    log_debug << mcast_addr.get_family() << " " << if_addr.get_family();
    if (mcast_addr.get_family() != if_addr.get_family())
    {
        gu_throw_fatal << "address families do not match: " 
                       << mcast_addr.get_family() << ", "
                       << if_addr.get_family();
    }
    
    if (mcast_addr.get_family() != AF_INET &&
        mcast_addr.get_family() != AF_INET6)
    {
        gu_throw_fatal << "Mreq: address family " << mcast_addr.get_family()
                       << " not supported";
    }
    
    get_ifindex_by_addr(if_addr);
    
    mreq_len_ = (mcast_addr.get_family() == AF_INET ? 
                 sizeof(struct ip_mreq)             : 
                 sizeof(struct ipv6_mreq));
    if ((mreq_ = malloc(mreq_len_)) == 0)
    {
        gu_throw_fatal << "could not allocate memory";
    }
    memset(mreq_, 0, mreq_len_);

    switch (mcast_addr.get_family())
    {
    case AF_INET:
    {
        struct ip_mreq* mr(reinterpret_cast<struct ip_mreq*>(mreq_));
        mr->imr_multiaddr.s_addr = *reinterpret_cast<const in_addr_t*>(mcast_addr.get_addr());

        mr->imr_interface.s_addr = *reinterpret_cast<const in_addr_t*>(if_addr.get_addr());
        ipproto_             = IPPROTO_IP;
        add_membership_opt_  = IP_ADD_MEMBERSHIP;
        drop_membership_opt_ = IP_DROP_MEMBERSHIP;
        multicast_if_opt_    = IP_MULTICAST_IF;
        multicast_loop_opt_  = IP_MULTICAST_LOOP;
        multicast_ttl_opt_   = IP_MULTICAST_TTL;
        break;
    }
    case AF_INET6:
    {
        struct ipv6_mreq* mr(reinterpret_cast<struct ipv6_mreq*>(mreq_));
        mr->ipv6mr_multiaddr = *reinterpret_cast<const struct in6_addr*>(mcast_addr.get_addr());
        mr->ipv6mr_interface = get_ifindex_by_addr(if_addr);
        ipproto_             = IPPROTO_IPV6;
        add_membership_opt_  = IPV6_ADD_MEMBERSHIP;
        drop_membership_opt_ = IPV6_DROP_MEMBERSHIP;
        multicast_loop_opt_  = IPV6_MULTICAST_LOOP;
        multicast_ttl_opt_   = IPV6_MULTICAST_HOPS;
        break;
    }
    }
}

gu::net::MReq::~MReq()
{
    free(mreq_);
}

const void* gu::net::MReq::get_multicast_if_value() const
{
    switch (ipproto_)
    {
    case IPPROTO_IP:
        return &reinterpret_cast<const struct ip_mreq*>(mreq_)->imr_interface;
    case IPPROTO_IPV6:
        return &reinterpret_cast<const struct ipv6_mreq*>(mreq_)->ipv6mr_interface;
    default:
        gu_throw_fatal << "get_multicast_if_value() not implemented for: "
                       << ipproto_;
    }
}

int gu::net::MReq::get_multicast_if_value_size() const
{
    switch (ipproto_)
    {
    case IPPROTO_IP:
        return sizeof(reinterpret_cast<const struct ip_mreq*>(mreq_)->imr_interface);
    case IPPROTO_IPV6:
        return sizeof(reinterpret_cast<const struct ipv6_mreq*>(mreq_)->ipv6mr_interface);
    default:
        gu_throw_fatal << "get_multicast_if_value_size() not implemented for: "
                       << ipproto_;
    }
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


std::string gu::net::Addrinfo::to_string() const
{
    static const size_t max_addr_str_len = (6 /* tcp|udp:// */ +
                                            INET6_ADDRSTRLEN + 2 /* [] */ +
                                            6 /* :portt */);
    std::string ret;

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

    if (inet_ntop(get_family(), addr.get_addr(), dst, sizeof(dst)) == 0)
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
        std::string host(uri.get_host());
        // remove [] if this is IPV6 address
        size_t pos(host.find_first_of('['));
        if (pos != std::string::npos)
        {
            host.erase(pos, pos + 1);
            pos = host.find_first_of(']');
            if (pos == std::string::npos)
            {
                gu_throw_error(EINVAL) << "invalid host: " << uri.get_host();

            }
            host.erase(pos, pos + 1);
        }

        int err;
        addrinfo* ai(0);
        try
        {
            err = getaddrinfo(host.c_str(), uri.get_port().c_str(),
                              SchemeMap::get_addrinfo(i), &ai);
        }
        catch (NotSet&)
        {
            err = getaddrinfo(host.c_str(), NULL,
                              SchemeMap::get_addrinfo(i), &ai);
        }

        if (err != 0)
        {
            // Use EHOSTUNREACH as generic error number in case errno
            // is zero. Real error should be apparent from exception message
            gu_throw_error(errno == 0 ? EHOSTUNREACH : errno)
                << "getaddrinfo failed with error '"
                << gai_strerror(err) << "' ("
                << err << ") for " << uri.to_string();
        }

        // Assume that the first entry is ok
        Addrinfo ret(*ai);
        freeaddrinfo(ai);
        return ret;
    }
    catch (NotFound& nf)
    {
        gu_throw_error(EINVAL) << "invalid URI: " << uri.to_string();
    }
}
