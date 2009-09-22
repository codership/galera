/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*! @todo: scheme and host parts should be normalized to lower case except for
 *         %-encodings, which should go upper-case */

#include <sys/types.h>

#include <cerrno>
#include <vector>

#include "gu_throw.hpp"
#include "gu_logger.hpp"
#include "gu_string.hpp"

#include "gu_uri.hpp"

using std::string;
using std::vector;
using std::multimap;

static void parse_authority (const string&     auth,
                             gu::RegEx::Match& user,
                             gu::RegEx::Match& host,
                             gu::RegEx::Match& port) throw (gu::Exception)
{
    size_t pos1, pos2;

    pos1 = auth.find_first_of ('@');

    if (pos1 != string::npos)
    {
        user = gu::RegEx::Match (auth.substr(0, pos1));
        pos1 += 1;
        // pos1 now points past the first occurence of @,
        // which may be past the end of the string.
    }
    else
    {
        pos1 = 0;
    }

    pos2 = auth.find_last_of (':');

    if (pos2 != string::npos && pos2 >= pos1)
    {
        host = gu::RegEx::Match (auth.substr (pos1, pos2 - pos1));

        // according to RFC 3986 empty port (nothing after :) should be trated
        // as unspecified, so make sure that it is not 0-length.
        if ((pos2 + 1) < auth.length())
        {
            port = gu::RegEx::Match (auth.substr (pos2 + 1));

            if ((port.string().find_first_not_of ("0123456789") != string::npos)
                ||
                // @todo: possible port range is not determined in RFC 3986
                (65535 < gu::from_string<long> (port.string())))
            {
                gu_throw_error (EINVAL) << "Can't parse port number from '"
                                        << port.string() << "'";
            }
        }
    }
    else
    {
        host = gu::RegEx::Match (auth.substr (pos1));
    }
}

static gu::URIQueryList extract_query_list(const string& s,
                                           const string& query)
{
    gu::URIQueryList ret;

    // scan all key=value pairs
    vector<string> qlist = gu::strsplit(query, '&');

    for (vector<string>::iterator i = qlist.begin(); i != qlist.end(); ++i)
    {
        vector<string> kvlist = gu::strsplit(*i, '=');

        if (kvlist.size() != 2)
        {
            gu_throw_error (EINVAL) << "Invalid URI query part: '" << *i << "'";
        }

        ret.insert(make_pair(kvlist[0], kvlist[1]));
    }

    return ret;
}

gu::URI::URI(const string& uri_str) throw (gu::Exception)
    :
    modified   (true), // recompose to normalize on the first call to_string()
    str        (uri_str),
    scheme     (),
    user       (),
    host       (),
    port       (),
    path       (),
    fragment   (),
    query_list ()
{
    parse(uri_str);
}

void gu::URI::parse (const string& uri_str) throw (gu::Exception)
{
    log_debug << "URI: " << uri_str;

    /*! regexp suggested by RFC 3986 to parse URI into 5 canonical parts */
    static const char* const uri_regex =
        "^(([^:/?#]+):)?(//([^/?#]*))?([^?#]*)(\\?([^#]*))?(#(.*))?";
    /*    12            3  4          5       6  7        8 9            */

    /*! positions of URI components as matched by the above pattern */
    enum
    {
        SCHEME     = 2,
        AUTHORITY  = 4,
        PATH       = 5,
        QUERY      = 7,
        FRAGMENT   = 9,
        NUM_PARTS
    };

    static const RegEx regex (uri_regex);

    vector<RegEx::Match> parts = regex.match (uri_str, NUM_PARTS);

    scheme = parts[SCHEME];

    if (!scheme.is_set() || !scheme.string().length())
    {
        gu_throw_error (EINVAL) << "URI '" << uri_str << "' has empty scheme"; 
    }

    try
    {
        parse_authority (parts[AUTHORITY].string(), user, host, port);
    }
    catch (NotSet&) {}

    path = parts[PATH];

    if (!parts[AUTHORITY].is_set() && !path.is_set())
    {
        gu_throw_error (EINVAL) << "URI '" << uri_str
                                << "' has no hierarchical part";
    }

    try
    {
        query_list = extract_query_list(str, parts[QUERY].string());
    }
    catch (NotSet&) {}

    fragment   = parts[FRAGMENT];

#if 0
    try
    {
        log_debug << "Base URI: " << scheme.string() << "://"
                  << get_authority();
    }
    catch (NotSet&) {}
#endif
}

string gu::URI::get_authority() const throw (NotSet)
{
    if (!user.is_set() && !host.is_set()) throw NotSet();

    size_t auth_len = 0;

    if (user.is_set()) auth_len += user.string().length() + 1;

    if (host.is_set())
    {
        auth_len += host.string().length();

        if (port.is_set()) auth_len += port.string().length() + 1;
    }

    string auth;

    auth.reserve (auth_len);

    if (user.is_set()) { auth += user.string(); auth += '@'; }

    if (host.is_set())
    {
        auth += host.string();

        if (port.is_set()) { auth += ':'; auth += port.string(); }
    }

    return auth;
}

void gu::URI::recompose() const
{
    size_t l = str.length();
    str.clear ();
    str.reserve (l); // resulting string length will be close to this

    if (scheme.is_set())
    {
        str += scheme.string();
        str += ':';
    }

    try
    {
        string auth = get_authority();
        str += "//";
        str += auth;
    }
    catch (NotSet&) {}

    if (path.is_set()) str += path.string();

    if (query_list.size() > 0)
    {
        str += '?';
    }
    
    URIQueryList::const_iterator i = query_list.begin();

    while (i != query_list.end())
    {
        str += i->first + '=' + i->second;

        URIQueryList::const_iterator i_next = i;

        ++i_next;

        if (i_next != query_list.end())
        {
            str += '&';
        }

        i = i_next;
    }

    if (fragment.is_set())
    {
        str += '#';
        str += fragment.string();
    }
}

void gu::URI::_set_scheme(const std::string& s)
{
    scheme = RegEx::Match(s);
    modified = true;
}

void gu::URI::_set_authority(const std::string& s)
{
    parse_authority (s, user, host, port);
    modified = true;
}

void gu::URI::set_query_param(const string& key, const string& val)
{
    query_list.insert(make_pair(key, val));
    modified = true;
}

const std::string& gu::URI::get_option (const std::string& name) const
    throw (gu::NotFound)
{
    gu::URIQueryList::const_iterator i = query_list.find(name);

    if (i == query_list.end()) throw NotFound();

    return i->second;
}
