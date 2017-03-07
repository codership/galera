// Copyright (C) 2010-2014 Codership Oy <info@codership.com>

/**
 * @file
 * Configuration management class
 *
 * $Id$
 */

#ifndef _gu_config_hpp_
#define _gu_config_hpp_

#include "gu_string_utils.hpp"
#include "gu_exception.hpp"
#include "gu_utils.hpp"
#include "gu_throw.hpp"
#include "gu_logger.hpp"
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

    Config ();

    bool
    has (const std::string& key) const
    {
        return (params_.find(key) != params_.end());
    }

    bool
    is_set (const std::string& key) const
    {
        param_map_t::const_iterator const i(params_.find(key));

        if (i != params_.end())
        {
            return i->second.is_set();
        }
        else
        {
            throw NotFound();
        }
    }

    /* adds parameter to the known parameter list */
    void
    add (const std::string& key)
    {
        gu_trace(key_check(key));
        if (!has(key)) { params_[key] = Parameter(); }
    }

    /* adds parameter to the known parameter list and sets its value */
    void
    add (const std::string& key, const std::string& value)
    {
        gu_trace(key_check(key));
        if (!has(key)) { params_[key] = Parameter(value); }
    }

    /* sets a known parameter to some value, otherwise throws NotFound */
    void
    set (const std::string& key, const std::string& value)
    {
        param_map_t::iterator const i(params_.find(key));

        if (i != params_.end())
        {
            i->second.set(value);
        }
        else
        {
#ifndef NDEBUG
            log_debug << "Key '" << key << "' not recognized.";
#endif
            throw NotFound();
        }
    }

    void
    set (const std::string& key, const char* value)
    {
        set(key, std::string(value));
    }

    /* Parse a string of semicolumn separated key=value pairs into a vector.
     * Throws Exception in case of parsing error. */
    static void
    parse (std::vector<std::pair<std::string, std::string> >& params_vector,
           const std::string& params_string);

    /* Parse a string of semicolumn separated key=value pairs and
     * set the values.
     * Throws NotFound if key was not explicitly added before. */
    void
    parse (const std::string& params_string);

    /* General template for integer types */
    template <typename T> void
    set (const std::string& key, T val)
    {
        set_longlong (key, val);
    }

    /*! @throws NotSet, NotFound */
    const std::string&
    get (const std::string& key) const
    {
        param_map_t::const_iterator const i(params_.find(key));
        if (i == params_.end())
        {
            log_debug << "key '" << key << "' not found.";
            throw NotFound();
        }
        if (i->second.is_set()) return i->second.value();
        log_debug << "key '" << key << "' not set.";
        throw NotSet();
    }

    const std::string&
    get (const std::string& key, const std::string& def) const
    {
        try             { return get(key); }
        catch (NotSet&) { return def     ; }
    }

    /*! @throws NotFound */
    template <typename T> inline T
    get (const std::string& key) const
    {
        return from_config <T> (get(key));
    }

    template <typename T> inline T
    get(const std::string& key, const T& def) const
    {
        try { return get<T>(key); }
        catch (NotSet&) { return def; }
    }

    void print (std::ostream& os, bool include_not_set = false) const;
    /*! Convert string configuration values to other types.
     *  General template for integers, specialized templates follow below.
     *  @throw gu::Exception in case conversion failed */
    template <typename T> static inline T
    from_config (const std::string& value)
    {
        const char* str    = value.c_str();
        long long   ret;
        errno = 0; // this is needed to detect overflow
        const char* endptr = gu_str2ll (str, &ret);

        check_conversion (str, endptr, "integer", ERANGE == errno);

        switch (sizeof(T))
        {
        case 1: return overflow_char  (ret);
        case 2: return overflow_short (ret);
        case 4: return overflow_int   (ret);
        default: return ret;
        }
    }

    /* iterator stuff */

    class Parameter
    {
    public:

        explicit
        Parameter(const std::string& value) : value_(value), set_(true)  {}
        Parameter()                         : value_(),      set_(false) {}

        const std::string& value()  const { return value_; }
        bool               is_set() const { return set_  ; }

        void set(const std::string& value)
        {
            value_ = value;
            set_   = true;
        }

    private:

        std::string value_;
        bool        set_;
    };

    typedef std::map <std::string, Parameter> param_map_t;
    typedef param_map_t::const_iterator const_iterator;

    const_iterator begin() const { return params_.begin(); }
    const_iterator end()   const { return params_.end();   }

private:

    static void
    key_check (const std::string& key);

    static void
    check_conversion (const char* ptr, const char* endptr, const char* type,
                      bool range_error = false);

    static char
    overflow_char(long long ret);

    static short
    overflow_short(long long ret);

    static int
    overflow_int(long long ret);

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
    Config::from_config (const std::string& value)
    {
        const char* str    = value.c_str();
        double      ret;
        errno = 0; // this is needed to detect over/underflow
        const char* endptr = gu_str2dbl (str, &ret);

        check_conversion (str, endptr, "double", ERANGE == errno);

        return ret;
    }

    template <> inline bool
    Config::from_config (const std::string& value)
    {
        const char* str    = value.c_str();
        bool        ret;
        const char* endptr = gu_str2bool (str, &ret);

        check_conversion (str, endptr, "boolean");

        return ret;
    }

    template <> inline void*
    Config::from_config (const std::string& value)
    {
        const char* str    = value.c_str();
        void*       ret;
        const char* endptr = gu_str2ptr (str, &ret);

        check_conversion (str, endptr, "pointer");

        return ret;
    }

    template <> inline void
    Config::set (const std::string& key, const void* value)
    {
        set (key, to_string<const void*>(value));
    }

    template <> inline void
    Config::set (const std::string& key, double val)
    {
        set (key, to_string<double>(val));
    }

    template <> inline void
    Config::set (const std::string& key, bool val)
    {
        const char* val_str(val ? "YES" : "NO"); // YES/NO is most generic
        set (key, val_str);
    }
}
#endif /* _gu_config_hpp_ */

