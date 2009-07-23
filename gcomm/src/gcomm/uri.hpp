#ifndef _GCOMM_URI_HPP_
#define _GCOMM_URI_HPP_

#include <gu_url.hpp>

namespace gcomm
{
    typedef gu::URL URI;
    typedef gu::URLQueryList URIQueryList;
    inline const std::string& get_query_key(URIQueryList::const_iterator& i)
    {
        return i->first;
    }

    inline const std::string& get_query_value(URIQueryList::const_iterator& i)
    {
        return i->second;
    }
}

#endif // _GCOMM_URI_HPP_
