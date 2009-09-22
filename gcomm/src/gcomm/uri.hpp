#ifndef _GCOMM_URI_HPP_
#define _GCOMM_URI_HPP_

#include <gu_uri.hpp>

namespace gcomm
{
    typedef gu::URI URI;
    typedef gu::URIQueryList URIQueryList;

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
