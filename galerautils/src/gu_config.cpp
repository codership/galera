// Copyright (C) 2010-2014 Codership Oy <info@codership.com>

/**
 * @file
 * Configuration management implementation
 *
 * $Id$
 */

#include "gu_config.h"
#include "gu_config.hpp"

#include "gu_logger.hpp"
#include "gu_assert.hpp"

const char gu::Config::PARAM_SEP     = ';';  // parameter separator
const char gu::Config::KEY_VALUE_SEP = '=';  // key-value separator
const char gu::Config::ESCAPE        = '\\'; // escape symbol

void
gu::Config::parse (
    std::vector<std::pair<std::string, std::string> >& params_vector,
    const std::string& param_list)
{
    assert(params_vector.empty()); // we probably want a clean list

    if (param_list.empty()) return;

    std::vector<std::string> pv = gu::tokenize (param_list, PARAM_SEP, ESCAPE);

    for (size_t i = 0; i < pv.size(); ++i)
    {
        std::vector<std::string> kvv =
            gu::tokenize (pv[i], KEY_VALUE_SEP, ESCAPE, true);

        assert(kvv.size() > 0);

        gu::trim(kvv[0]);
        const std::string& key = kvv[0];

        if (!key.empty())
        {
            if (kvv.size() == 1)
            {
                gu_throw_error(EINVAL) <<"Key without value: '" << key
                                       <<"' at position '"
                                       << i << "' in parameter list.";
            }

            if (kvv.size() > 2)
            {
                gu_throw_error(EINVAL) <<"More than one value for key '" << key
                                       <<"' at '"
                                       << pv[i] << "' in parameter list.";
            }

            gu::trim(kvv[1]);
            std::string& value = kvv[1];

            params_vector.push_back(std::make_pair(key, value));
        }
        else if (kvv.size() > 1)
        {
            gu_throw_error(EINVAL) << "Empty key at '" << pv[i]
                                   << "' in parameter list.";
        }
    }
}

void
gu::Config::parse (const std::string& param_list)
{
    if (param_list.empty()) return;

    std::vector<std::pair<std::string, std::string> > pv;

    parse (pv, param_list);

    bool not_found(false);

    for (size_t i = 0; i < pv.size(); ++i)
    {
        const std::string& key  (pv[i].first);
        const std::string& value(pv[i].second);

        try
        {
            set(key, value);
        }
        catch (NotFound& e)
        {
            log_error << "Unrecognized parameter '" << key << '\'';
            /* Throw later so that all invalid parameters get logged.*/
            not_found = true;
        }

        log_debug << "Set parameter '" << key << "' = '" << value << "'";
    }

    if (not_found) throw gu::NotFound();
}

gu::Config::Config() : params_() {}

void
gu::Config::set_longlong (const std::string& key, long long val)
{
    const char* num_mod = "";

    /* Shift preserves sign! */
    if (val != 0)
    {
        if (!(val & ((1LL << 40) - 1)))
        {
            val >>= 40;
            num_mod = "T";
        }
        else if (!(val & ((1 << 30) - 1)))
        {
            val >>= 30;
            num_mod = "G";
        }
        else if (!(val & ((1 << 20) - 1)))
        {
            val >>= 20;
            num_mod = "M";
        }
        else if (!(val & ((1 << 10) - 1)))
        {
            val >>= 10;
            num_mod = "K";
        }
    }

    std::ostringstream ost;
    ost << val << num_mod;
    set (key, ost.str());
}

void
gu::Config::key_check (const std::string& key)
{
    if (key.size() == 0) { gu_throw_error(EINVAL) << "Empty key."; }
}

void
gu::Config::check_conversion (const char* str,
                              const char* endptr,
                              const char* type,
                              bool range_error)
{
    if (endptr == str || endptr[0] != '\0' || range_error)
    {
        gu_throw_error(EINVAL) << "Invalid value '" << str << "' for " << type
                               << " type.";
    }
}

char
gu::Config::overflow_char(long long ret)
{
    if (ret >= CHAR_MIN && ret <= CHAR_MAX) return ret;

    gu_throw_error(EOVERFLOW) << "Value " << ret
                              << " too large for requested type (char).";
}

short
gu::Config::overflow_short(long long ret)
{
    if (ret >= SHRT_MIN && ret <= SHRT_MAX) return ret;

    gu_throw_error(EOVERFLOW) << "Value " << ret
                              << " too large for requested type (short).";
}

int
gu::Config::overflow_int(long long ret)
{
    if (ret >= INT_MIN && ret <= INT_MAX) return ret;

    gu_throw_error(EOVERFLOW) << "Value " << ret
                              << " too large for requested type (int).";
}

void
gu::Config::print (std::ostream& os, bool const notset) const
{
    struct _print_param
    {
        void operator() (std::ostream& os, bool const notset,
                         param_map_t::const_iterator& pi)
        {
            const Parameter& p(pi->second);

            if (p.is_set() || notset)
            {
                os << pi->first << " = " << p.value() << "; ";
            }
        }
    }
    print_param;

    for (param_map_t::const_iterator pi(params_.begin());
         pi != params_.end(); ++pi)
    {
        print_param(os, notset, pi);
    }
}

std::ostream& gu::operator<<(std::ostream& ost, const gu::Config& c)
{
    c.print(ost);
    return ost;
}

gu_config_t*
gu_config_create (void)
{
    try
    {
        return (reinterpret_cast<gu_config_t*>(new gu::Config()));
    }
    catch (gu::Exception& e)
    {
        log_error << "Failed to create configuration object: " << e.what();
        return 0;
    }
}

void
gu_config_destroy (gu_config_t* cnf)
{
    if (cnf)
    {
        gu::Config* conf = reinterpret_cast<gu::Config*>(cnf);
        delete conf;
    }
    else
    {
        log_error << "Null configuration object in " << __FUNCTION__;
        assert (0);
    }
}

static int
config_check_set_args (gu_config_t* cnf, const char* key, const char* func)
{
    if (cnf && key && key[0] != '\0') return 0;

    if (!cnf) { log_fatal << "Null configuration object in " << func; }
    if (!key) { log_fatal << "Null key in " << func; }
    else if (key[0] == '\0') { log_fatal << "Empty key in " << func; }

    assert (0);

    return -EINVAL;
}

static int
config_check_get_args (gu_config_t* cnf, const char* key, const void* val_ptr,
                       const char* func)
{
    if (cnf && key && key[0] != '\0' && val_ptr) return 0;

    if (!cnf) { log_error << "Null configuration object in " << func; }
    if (!key) { log_error << "Null key in " << func; }
    else if (key[0] == '\0') { log_error << "Empty key in " << func; }
    if (!val_ptr) { log_error << "Null value pointer in " << func; }

    assert (0);

    return -EINVAL;
}

bool
gu_config_has (gu_config_t* cnf, const char* key)
{
    if (config_check_set_args (cnf, key, __FUNCTION__)) return false;

    gu::Config* conf = reinterpret_cast<gu::Config*>(cnf);

    return (conf->has (key));
}

bool
gu_config_is_set (gu_config_t* cnf, const char* key)
{
    if (config_check_set_args (cnf, key, __FUNCTION__)) return false;

    gu::Config* conf = reinterpret_cast<gu::Config*>(cnf);

    return (conf->is_set (key));
}

int
gu_config_add (gu_config_t* cnf, const char* key, const char* const val)
{
    if (config_check_set_args (cnf, key, __FUNCTION__)) return -EINVAL;

    gu::Config* conf = reinterpret_cast<gu::Config*>(cnf);

    try
    {
        if (val != NULL)
            conf->add (key, val);
        else
            conf->add (key);

        return 0;
    }
    catch (std::exception& e)
    {
        log_error << "Error adding parameter '" << key << "': " << e.what();
        return -1;
    }
    catch (...)
    {
        log_error << "Unknown exception adding parameter '" << key << "'";
        return -1;
    }
}

int
gu_config_get_string (gu_config_t* cnf, const char* key, const char** val)
{
    if (config_check_get_args (cnf, key, val, __FUNCTION__)) return -EINVAL;

    gu::Config* conf = reinterpret_cast<gu::Config*>(cnf);

    try
    {
        *val = conf->get(key).c_str();
        return 0;
    }
    catch (gu::NotFound&)
    {
        return 1;
    }
}

int
gu_config_get_int64  (gu_config_t* cnf, const char* key, int64_t* val)
{
    if (config_check_get_args (cnf, key, val, __FUNCTION__)) return -EINVAL;

    gu::Config* conf = reinterpret_cast<gu::Config*>(cnf);

    try
    {
        *val = conf->get<int64_t>(key);
        return 0;
    }
    catch (gu::NotFound&)
    {
        return 1;
    }
    catch (gu::NotSet&)
    {
        return 1;
    }
    catch (gu::Exception& e)
    {
        log_error << "Failed to parse parameter '" << key << "': " << e.what();
        return -e.get_errno();
    }
}

int
gu_config_get_double (gu_config_t* cnf, const char* key, double* val)
{
    if (config_check_get_args (cnf, key, val, __FUNCTION__)) return -EINVAL;

    gu::Config* conf = reinterpret_cast<gu::Config*>(cnf);

    try
    {
        *val = conf->get<double>(key);
        return 0;
    }
    catch (gu::NotFound&)
    {
        return 1;
    }
    catch (gu::NotSet&)
    {
        return 1;
    }
    catch (gu::Exception& e)
    {
        log_error << "Failed to parse parameter '" << key << "': " << e.what();
        return -e.get_errno();
    }
}

int
gu_config_get_ptr    (gu_config_t* cnf, const char* key, void** val)
{
    if (config_check_get_args (cnf, key, val, __FUNCTION__)) return -EINVAL;

    gu::Config* conf = reinterpret_cast<gu::Config*>(cnf);

    try
    {
        *val = conf->get<void*>(key);
        return 0;
    }
    catch (gu::NotFound&)
    {
        return 1;
    }
    catch (gu::NotSet&)
    {
        return 1;
    }
    catch (gu::Exception& e)
    {
        log_error << "Failed to parse parameter '" << key << "': " << e.what();
        return -e.get_errno();
    }
}

int
gu_config_get_bool   (gu_config_t* cnf, const char* key, bool* val)
{
    if (config_check_get_args (cnf, key, val, __FUNCTION__)) return -EINVAL;

    gu::Config* conf = reinterpret_cast<gu::Config*>(cnf);

    try
    {
        *val = conf->get<bool>(key);
        return 0;
    }
    catch (gu::NotFound&)
    {
        return 1;
    }
    catch (gu::NotSet&)
    {
        return 1;
    }
    catch (gu::Exception& e)
    {
        log_error << "Failed to parse parameter '" << key << "': " << e.what();
        return -e.get_errno();
    }
}

#include <cstdlib>

void
gu_config_set_string (gu_config_t* cnf, const char* key, const char* val)
{
    if (config_check_set_args (cnf, key, __FUNCTION__)) abort();
    assert (cnf);
    gu::Config* conf = reinterpret_cast<gu::Config*>(cnf);

    conf->set (key, val);
}

void
gu_config_set_int64  (gu_config_t* cnf, const char* key, int64_t val)
{
    if (config_check_set_args (cnf, key, __FUNCTION__)) abort();

    gu::Config* conf = reinterpret_cast<gu::Config*>(cnf);

    conf->set (key, val);
}

void
gu_config_set_double (gu_config_t* cnf, const char* key, double val)
{
    if (config_check_set_args (cnf, key, __FUNCTION__)) abort();

    gu::Config* conf = reinterpret_cast<gu::Config*>(cnf);

    conf->set(key, val);
}

void
gu_config_set_ptr    (gu_config_t* cnf, const char* key, const void* val)
{
    if (config_check_set_args (cnf, key, __FUNCTION__)) abort();

    gu::Config* conf = reinterpret_cast<gu::Config*>(cnf);

    conf->set<const void*>(key, val);
}

void
gu_config_set_bool  (gu_config_t* cnf, const char* key, bool val)
{
    if (config_check_set_args (cnf, key, __FUNCTION__)) abort();

    gu::Config* conf = reinterpret_cast<gu::Config*>(cnf);

    conf->set<bool>(key, val);
}

ssize_t
gu_config_print (gu_config_t* cnf, char* buf, ssize_t buf_len)
{
    std::ostringstream os;

    os << *(reinterpret_cast<gu::Config*>(cnf));

    const std::string& str = os.str();

    strncpy (buf, str.c_str(), buf_len - 1);
    buf[buf_len - 1] = '\0';

    return str.length();
}
