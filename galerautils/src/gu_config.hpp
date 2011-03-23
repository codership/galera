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

#include <climits>

namespace gu
{
    class Config;
}

extern "C" const char* gu_str2ll (const char* str, long long* ll);

class gu::Config
{
public:

    static const char PARAM_SEP;     // parameter separator
    static const char KEY_VALUE_SEP; // key-value separator
    static const char ESCAPE;        // escape symbol

    typedef std::map <std::string, std::string> param_map_t;

    static void
    parse (param_map_t& list, const std::string& params) throw (Exception);

    /*! Convert string configuration values to other types.
     *  General template for integers, specialized templates follow below. */
    template <typename T> static inline T
    from_config (const std::string& value) throw (Exception)
    {
        const char* str    = value.c_str();
        long long   ret;
        const char* endptr = gu_str2ll (str, &ret);

        check_conversion (str, endptr, "integer");

        switch (sizeof(T))
        {
        case 1: return overflow_char  (ret);
        case 2: return overflow_short (ret);
        case 4: return overflow_int   (ret);
        }

        return ret;
    }

    Config () throw();
    Config (const std::string& params) throw (Exception);

    bool
    has (const std::string& key) const throw ()
    {
        return (params_.find(key) != params_.end());
    }

    void
    set (const std::string& key, const std::string& value) throw ()
    {
        params_[key] = value;
    }

    void
    set (const std::string& key, const char* value) throw ()
    {
        params_[key] = value;
    }

    /* General template for integer types */
    template <typename T> void
    set (const std::string& key, T val) throw ()
    {
        set_longlong (key, val);
    }

    const std::string&
    get (const std::string& key) const throw (NotFound)
    {
        param_map_t::const_iterator i = params_.find(key);
        if (i == params_.end()) throw NotFound();
        return i->second;
    }

    const std::string&
    get(const std::string& key, const std::string& def) const throw (Exception)
    {
        try               { return get(key); }
        catch (NotFound&) { return def     ; }
    }

    template <typename T> inline T
    get (const std::string& key) const throw (NotFound, Exception)
    {
        return from_config <T> (get(key));
    }

    template <typename T> inline T
    get(const std::string& key, const T& def) const throw (Exception)
    {
        try { return get<T>(key); }
        catch (NotFound&) { return def; }
    }

    const param_map_t& params () const throw() { return params_; }

private:

    static void
    check_conversion (const char* ptr, const char* endptr, const char* type)
        throw (Exception);

    static char
    overflow_char(long long ret) throw (Exception);

    static short
    overflow_short(long long ret) throw (Exception);

    static int
    overflow_int(long long ret) throw (Exception);

    void set_longlong (const std::string& key, long long value);

    param_map_t params_;
};


extern "C" const char* gu_str2dbl  (const char* str, double* dbl);
extern "C" const char* gu_str2bool (const char* str, bool*   bl);
extern "C" const char* gu_str2ptr  (const char* str, void**  ptr);

namespace gu
{
    std::ostream& operator<<(std::ostream&, const gu::Config&);
    /*! Specialized templates for "funny" types */

    template <> inline double
    Config::from_config (const std::string& value) throw (Exception)
    {
        const char* str    = value.c_str();
        double      ret;
        const char* endptr = gu_str2dbl (str, &ret);

        check_conversion (str, endptr, "double");

        return ret;
    }

    template <> inline bool
    Config::from_config (const std::string& value) throw (Exception)
    {
        const char* str    = value.c_str();
        bool        ret;
        const char* endptr = gu_str2bool (str, &ret);

        check_conversion (str, endptr, "boolean");

        return ret;
    }

    template <> inline void*
    Config::from_config (const std::string& value) throw (Exception)
    {
        const char* str    = value.c_str();
        void*       ret;
        const char* endptr = gu_str2ptr (str, &ret);

        check_conversion (str, endptr, "pointer");

        return ret;
    }

    template <> inline void
    Config::set (const std::string& key, const void* value) throw ()
    {
        set (key, to_string<const void*>(value));
    }

    template <> inline void
    Config::set (const std::string& key, double val) throw ()
    {
        set (key, to_string<double>(val));
    }

    template <> inline void
    Config::set (const std::string& key, bool val) throw ()
    {
        const char* val_str(val ? "YES" : "NO"); // YES/NO is most generic
        set (key, val_str);
    }
}
#endif /* _gu_config_hpp_ */

