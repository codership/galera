#ifndef _GCOMM_UTIL_HPP_
#define _GCOMM_UTIL_HPP_

#include <sstream>
#include <cstring>

#include <gcomm/common.hpp>
#include <gcomm/string.hpp>
#include <gcomm/exception.hpp>
#include <gcomm/types.hpp>

using std::ostringstream;

BEGIN_GCOMM_NAMESPACE


static inline size_t read_string(const void* from, const size_t fromlen,
                                 const size_t from_offset, char** to)
{
    if (to == 0)
    {
        gcomm_throw_runtime (EINVAL) << "0 destination pointer";
    }
    /* Scan for termination character */
    size_t i;
    const char* from_str = reinterpret_cast<const char*>(from);

    for (i = from_offset; i < fromlen && *(from_str + i) != '\0'; ++i) {}
 
    /* Buffer didn't contain '\0' */
    if (i == fromlen) gcomm_throw_runtime (EMSGSIZE);

    /* Safe to strdup now */
    *to = strdup(from_str + from_offset);
    return i + 1;
}

static inline size_t read_bytes(const byte_t* from, const size_t fromlen,
                                const size_t from_offset, 
                                byte_t* to, const size_t tolen)
{
    if (fromlen < from_offset + tolen) gcomm_throw_runtime (EMSGSIZE);

    memcpy(to, from + from_offset, tolen);

    return from_offset + tolen;
}

static inline size_t write_string(const char* from, byte_t* to, 
                                  const size_t tolen, const size_t to_offset)
{
    size_t strl = strlen(from);

    if (tolen < strl + 1 + to_offset) gcomm_throw_runtime (EMSGSIZE);

    /* Copy string including '\0' */
    memcpy(to + to_offset, from, strl + 1);

    return to_offset + strl + 1;
}

static inline size_t write_bytes(const byte_t* from, 
                                 const size_t fromlen,
                                 byte_t* to, 
                                 const size_t tolen, const size_t to_offset)
{
    if (tolen < fromlen + to_offset) gcomm_throw_runtime (EMSGSIZE);

    memcpy(to + to_offset, from, fromlen);

    return to_offset + fromlen;
}

/**
 * Read boolean value from string. String must contain one of
 * 0, false, 1, true
 *
 * \param s Input string
 * \return Boolean value
 */
bool read_bool(const string& s);

/**
 * Read integer value from string. 
 *
 * \param s Input string
 * \return Integer value
 */
int read_int(const string& s);

/**
 * Read long value from string.
 *
 * \param s Input string
 * \return Long value
 */
long read_long(const string& s);

/*! 
 * Convert sockaddr struct to uri
 */
std::string sockaddr_to_uri(const std::string& scheme, const sockaddr* sa);

std::string sockaddr_host_to_str(const sockaddr* sa);
std::string sockaddr_port_to_str(const sockaddr* sa);
bool is_anyaddr(const std::string& url);

// @todo: wrong function - port is not mandatory by RFC, will also fail for IPv6
inline std::string _parse_host(const std::string& str)
{
    size_t sep = str.find(':');
    if (sep == std::string::npos)
    {
        gcomm_throw_runtime (EINVAL) << "Invalid auth str";
    }
    return str.substr(0, sep);
}

// @todo: wrong function - port is not mandatory by RFC, will also fail for IPv6
inline std::string _parse_port(const std::string& str)
{
    size_t sep = str.find(':');
    if (sep == std::string::npos)
    {
        gcomm_throw_runtime (EINVAL) << "Invalid auth str";
    }
    return str.substr(sep + 1);
}

inline bool host_undefined (const string& host)
{   
    return (host.length() == 0 || host == "0.0.0.0" ||
            host.find ("::/128") <= 1);
}

END_GCOMM_NAMESPACE

#endif // _GCOMM_UTIL_HPP_
