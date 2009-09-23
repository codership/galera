
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

string sockaddr_to_uri(const string& scheme, const sockaddr* sa)
{
    if (sa->sa_family != AF_INET && sa->sa_family != AF_INET6)
    {
        gcomm_throw_fatal << "Address family " << sa->sa_family
                          << " not supported";
    }

    char buf[24];
    string ret = scheme + "://";
    
    const sockaddr_in *sin = reinterpret_cast<const sockaddr_in*>(sa);
    
    const char* rb = inet_ntop(sin->sin_family, &sin->sin_addr, buf,
                               sizeof(buf));
    if (rb == 0)
    {
        gcomm_throw_fatal << "Address conversion failed: " << strerror(errno);
    }
    ret += rb;
    ret += ":";
    ret += make_int<unsigned short>(ntohs(sin->sin_port)).to_string();
    
    return ret;
}

string sockaddr_host_to_str(const sockaddr* sa)
{
    if (sa->sa_family != AF_INET && sa->sa_family != AF_INET6)
    {
        gcomm_throw_fatal << "Address family " << sa->sa_family
                          << " not supported";
    }
    char buf[24];
    const sockaddr_in *sin = reinterpret_cast<const sockaddr_in*>(sa);
    const char* rb = inet_ntop(sin->sin_family, &sin->sin_addr, buf,
                               sizeof(buf));
    if (rb == 0)
    {
        gcomm_throw_fatal << "Address conversion failed: " << strerror(errno);
    }
    return rb;
}

string sockaddr_port_to_str(const sockaddr* sa)
{
    if (sa->sa_family != AF_INET && sa->sa_family != AF_INET6)
    {
        gcomm_throw_fatal << "Address family " << sa->sa_family
                          << " not supported";
    }

    const sockaddr_in *sin = reinterpret_cast<const sockaddr_in*>(sa);
    return make_int<unsigned short>(ntohs(sin->sin_port)).to_string();
}

END_GCOMM_NAMESPACE
