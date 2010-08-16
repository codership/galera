// Copyright (C) 2010 Codership Oy <info@codership.com>
 
/**
 * @file
 * Configuration management class
 *
 * $Id$
 */

#ifndef _gu_config_hpp_
#define _gu_config_hpp_

#include "gu_string.hpp"
#include "gu_exception.hpp"
#include "gu_utils.hpp"
#include "gu_throw.hpp"
#include <map>

namespace gu
{
    class Config;
}

class gu::Config
{
public:

    static const char PARAM_SEP;     // parameter separator
    static const char KEY_VALUE_SEP; // key-value separator
    static const char ESCAPE;        // escape symbol

    typedef std::map <std::string, std::string> param_map_t;

    static void parse (param_map_t& list, const std::string& params)
        throw (gu::Exception);

    Config () throw();
    Config (const std::string& params) throw (gu::Exception);

    bool has (const std::string& key) const throw ()
    {
        return (params_.find(key) != params_.end());
    }

    void set (const std::string& key, const std::string& value) throw ()
    {
        params_[key] = value;
    }

    template <typename T> inline T
    get (const std::string& key) const throw (NotFound, Exception)
    {
        const std::string& value = get_param (key);

        try
        {
            return from_string<T>(value);
        }
        catch (NotFound&)
        {
            gu_throw_error(EINVAL) << "Invalid value '" << value << "' for '"
                                   << key << '\'';
            throw;
        }
    }

    const std::string&
    get (const std::string& key) const throw (NotFound)
    {
        return get_param (key);
    }

private:

    const std::string&
    get_param (const std::string& key) const throw (gu::NotFound)
    {
        param_map_t::const_iterator i = params_.find(key);
        if (i == params_.end()) throw NotFound();
        return i->second;
    }

    param_map_t params_;
};

#endif /* _gu_config_hpp_ */

