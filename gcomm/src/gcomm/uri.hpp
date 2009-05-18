#ifndef URI_HPP
#define URI_HPP

#include <gcomm/common.hpp>
#include <gcomm/exception.hpp>
#include <gcomm/string.hpp>

#include <map>
using std::multimap;

 
BEGIN_GCOMM_NAMESPACE

class URIException : public Exception
{
    
};

class URIParseException : public URIException
{
    
};

class URISubexpNotFoundException : public URIException
{
    
};


typedef multimap<const string, string> URIQueryList;

static inline const string& get_query_key(const URIQueryList::const_iterator& i)
{
    return i->first;
}

static inline const string& get_query_value(const URIQueryList::const_iterator& i)
{
    return i->second;
}

class URI
{
private:
    string str;
    string scheme;
    string authority;
    string path;
    
    URIQueryList query_list;
    void parse();
    void recompose();
public:
    
    URI();
    URI(const string&);
    
    const string& to_string() const;
    void set_scheme(const string&);
    const string& get_scheme() const;
    const string& get_authority() const;
    const string& get_path() const;
    void set_query_param(const string&, const string&);
    const URIQueryList& get_query_list() const;
};


END_GCOMM_NAMESPACE

#endif // URI_HPP
