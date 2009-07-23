
#include "gcomm/util.hpp"
#include "gcomm/exception.hpp"
#include "gcomm/logger.hpp"

#include <istream>
#include <cerrno>
#include <arpa/inet.h>
#include <netinet/in.h>

using std::istringstream;

BEGIN_GCOMM_NAMESPACE

bool read_bool(const string& s)
{
    istringstream is(s);
    bool ret;
    if ((is >> ret).fail())
    {
        LOG_FATAL("string '" + s + "' does not contain bool");
        throw FatalException("");
    }
    return ret;
}

int read_int(const string& s)
{
    istringstream is(s);
    int ret;
    if ((is >> ret).fail())
    {
        LOG_FATAL("string '" + s + "' does not contain int");
        throw FatalException("");
    }
    return ret;
}

long read_long(const string& s)
{
    istringstream is(s);
    long ret;
    if ((is >> ret).fail())
    {
        LOG_FATAL("string '" + s + "' does not contain long");
        throw FatalException("");
    }
    return ret;
}

string sockaddr_to_uri(const string& scheme, const sockaddr* sa)
{
    if (sa->sa_family != AF_INET && sa->sa_family != AF_INET6)
    {
        log_fatal << "address family " << sa->sa_family << " not supported";
        throw FatalException("address family not supported");
    }

    char buf[24];
    string ret = scheme + "://";
    
    const sockaddr_in *sin = reinterpret_cast<const sockaddr_in*>(sa);
    
    const char* rb = inet_ntop(sin->sin_family, &sin->sin_addr, buf,
                               sizeof(buf));
    if (rb == 0)
    {
        log_fatal << "address conversion failed: " << strerror(errno);
        throw FatalException("address conversion failed");
    }
    ret += rb;
    ret += ":";
    ret += make_int<int>(ntohs(sin->sin_port)).to_string();
    
    return ret;
}

string sockaddr_host_to_str(const sockaddr* sa)
{
    if (sa->sa_family != AF_INET && sa->sa_family != AF_INET6)
    {
        log_fatal << "address family " << sa->sa_family << " not supported";
        throw FatalException("address family not supported");
    }
    char buf[24];
    const sockaddr_in *sin = reinterpret_cast<const sockaddr_in*>(sa);
    const char* rb = inet_ntop(sin->sin_family, &sin->sin_addr, buf,
                               sizeof(buf));
    if (rb == 0)
    {
        log_fatal << "address conversion failed: " << strerror(errno);
        throw FatalException("address conversion failed");
    }
    return rb;
}

string sockaddr_port_to_str(const sockaddr* sa)
{
    if (sa->sa_family != AF_INET && sa->sa_family != AF_INET6)
    {
        log_fatal << "address family " << sa->sa_family << " not supported";
        throw FatalException("address family not supported");
    }
    const sockaddr_in *sin = reinterpret_cast<const sockaddr_in*>(sa);
    return make_int<unsigned short>(ntohs(sin->sin_port)).to_string();
}

END_GCOMM_NAMESPACE
