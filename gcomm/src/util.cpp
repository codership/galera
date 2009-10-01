
#include "gcomm/util.hpp"
#include "gcomm/exception.hpp"
#include "gcomm/logger.hpp"

#include <istream>
#include <cerrno>
#include <arpa/inet.h>
#include <netinet/in.h>

// @todo: fatal throws in the read_xxx() functions below are really unnecessary,
//        they don't represent unresolvable logic error in the applicaiton
using std::istringstream;
using std::string;

BEGIN_GCOMM_NAMESPACE

bool read_bool(const string& s)
{
    istringstream is(s);
    bool ret;
    if ((is >> ret).fail())
    {
        gcomm_throw_fatal << "String '" << s << "' does not contain bool";
    }
    return ret;
}

int read_int(const string& s)
{
    istringstream is(s);
    int ret;
    if ((is >> ret).fail())
    {
        gcomm_throw_fatal << "String '" << s << "' does not contain int";
    }
    return ret;
}

long read_long(const string& s)
{
    istringstream is(s);
    long ret;
    if ((is >> ret).fail())
    {
        gcomm_throw_fatal << "String '" << s << "' does not contain long";
    }
    return ret;
}

string sockaddr_to_str (const sockaddr* sa) throw (RuntimeException)
{
    if (sa->sa_family != AF_INET && sa->sa_family != AF_INET6)
    {
        gcomm_throw_runtime (EINVAL) << "Address family " << sa->sa_family
                                     << " not supported";
    }

    char   buf[40]; // IPv6 takes 39 digits + terminator
    
    const sockaddr_in *sin = reinterpret_cast<const sockaddr_in*>(sa);
    
    const char* rb = inet_ntop(sin->sin_family, &sin->sin_addr, buf,
                               sizeof(buf));
    if (rb == 0)
    {
        gcomm_throw_runtime (errno) << "Address conversion failed";
    }

    std::ostringstream ret;

    ret << rb << ":" << (ntohs(sin->sin_port));
    
    return ret.str();
}

string sockaddr_to_host (const sockaddr* sa) throw (RuntimeException)
{
    if (sa->sa_family != AF_INET && sa->sa_family != AF_INET6)
    {
        gcomm_throw_runtime (EINVAL) << "Address family " << sa->sa_family
                                     << " not supported";
    }

    char buf[40];
    const sockaddr_in *sin = reinterpret_cast<const sockaddr_in*>(sa);
    const char* rb = inet_ntop(sin->sin_family, &sin->sin_addr, buf,
                               sizeof(buf));
    if (rb == 0)
    {
        gcomm_throw_runtime (errno) << "Address conversion failed";
    }

    return rb;
}

string sockaddr_to_port (const sockaddr* sa) throw (RuntimeException)
{
    if (sa->sa_family != AF_INET && sa->sa_family != AF_INET6)
    {
        gcomm_throw_runtime (EINVAL) << "Address family " << sa->sa_family
                                     << " not supported";
    }

    const sockaddr_in *sin = reinterpret_cast<const sockaddr_in*>(sa);
    return gu::to_string (ntohs(sin->sin_port));
}

END_GCOMM_NAMESPACE
