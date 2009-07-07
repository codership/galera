
#include "gcomm/uri.hpp"
#include "gcomm/logger.hpp"
#include "gcomm/string.hpp"

#include <sys/types.h>
#include <regex.h>

BEGIN_GCOMM_NAMESPACE

static void logerr(int rc, const regex_t* preg, const string& msg = "")
{
    char buf[128];
    regerror(rc, preg, buf, sizeof(buf));
    LOG_ERROR(string(buf) + " (" + msg + ")");
}


static string extract_str(const string& s, const regmatch_t& rm)
{
    if (rm.rm_so == -1)
    {
        throw URISubexpNotFoundException();
    }
    return s.substr(rm.rm_so, rm.rm_eo - rm.rm_so);
}


static multimap<const string, string> extract_query_list(
    const string& s,
    const regmatch_t& rm)
{
    string query = extract_str(s, rm);    
    multimap<const string, string> ret;
    // scan all key=value pairs
    vector<string> qlist = strsplit(query, '&');
    for (vector<string>::iterator i = qlist.begin(); i != qlist.end(); ++i)
    {
        vector<string> kvlist = strsplit(*i, '=');
        if (kvlist.size() != 2)
        {
            throw URIParseException();
        }
        ret.insert(make_pair(kvlist[0], kvlist[1]));
    }
    return ret;
}


URI::URI() :
    str(),
    scheme(),
    authority(),
    path(),
    query_list()
{

}

URI::URI(const string& str_) : 
    str(str_),
    scheme(),
    authority(),
    path(),
    query_list()
{
    parse();
}

const string& URI::to_string() const
{
    return str;
}

void URI::set_scheme(const string& scheme)
{
    this->scheme = scheme;
    recompose();
}

const string& URI::get_scheme() const
{
    return scheme;
}

const string& URI::get_authority() const
{
    return authority;
}
    
const string& URI::get_path() const
{
    return path;
}
    

void URI::set_query_param(const string& key, const string& val)
{
    query_list.insert(make_pair(key, val));
    recompose();
}

const URIQueryList& URI::get_query_list() const
{
    return query_list;
}

/*
 * Parse URI according to RFC 2396 appendix B. Regexp is not exactly the 
 * same as in RFC due to need to escape '?' in query part.
 *
 * The following parts are assumed to be part of URI string:
 * - Scheme (http, ftp, ...)
 * - Authority ([username:password@]example.com)
 * Non-obligatory parts:
 * - Path (/path/to/object)
 * - Query (key1=val1[&key2=val2[...]])
 * Ignored parts:
 * - Parameter
 * - Fragment
 */
void URI::parse()
{
    const char* const uri_regex = "^(([^:/?#]+):)?(//([^/?#]*))?([^?#]*)(\\?([^#]*))?(#(.*))?";
    regex_t reg;
    int rc;

    LOG_DEBUG("uri: " + str);
    
    if ((rc = regcomp(&reg, uri_regex, REG_EXTENDED)) != 0)
    {
        logerr(rc, &reg);
        throw URIParseException();
    }
    
    /* Possibly 9 matches + beginning of line */
    const size_t nmatch = 10;
    regmatch_t pmatch[nmatch];
    
    if ((rc = regexec(&reg, str.c_str(), nmatch, pmatch, 0)) != 0)
    {
        logerr(rc, &reg, str);
        regfree(&reg);
        throw URIParseException();
    }
    regfree(&reg);    

    scheme = extract_str(str, pmatch[2]);
    authority = extract_str(str, pmatch[4]);
    
    try
    {
        path = extract_str(str, pmatch[5]);
    }
    catch (URISubexpNotFoundException e)
    {
        LOG_DEBUG("no path subexp in uri");
    }
    
    try
    {
        query_list = extract_query_list(str, pmatch[7]);
    }
    catch (URISubexpNotFoundException e)
    {
        LOG_DEBUG("no query part in uri");
    }
}


void URI::recompose()
{
    str.clear();
    str += scheme;
    str += "://";
    str += authority;
    str += path;

    if (query_list.size() > 0)
    {
        str += "?";
    }
    
    URIQueryList::iterator i = query_list.begin();
    while (i != query_list.end())
    {
        str += i->first + "=" + i->second;
        URIQueryList::iterator i_next = i;
        ++i_next;
        if (i_next != query_list.end())
        {
            str += "&";
        }
        i = i_next;
    }
}



END_GCOMM_NAMESPACE
