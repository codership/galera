// Copyright (C) 2010 Codership Oy <info@codership.com>
 
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
gu::Config::parse (param_map_t& pmap, const std::string& params)
    throw (gu::Exception)
{
    if (0 == params[0]) return;

    std::vector<std::string> pv = gu::tokenize (params, PARAM_SEP, ESCAPE);

    for (size_t i = 0; i < pv.size(); ++i)
    {
        std::vector<std::string> kvv =
            gu::tokenize (pv[i], KEY_VALUE_SEP, ESCAPE, true);

        gu::trim(kvv[0]);
        const std::string& key = kvv[0];

        if (!key.empty())
        {
            if (kvv.size() > 2)
            {
                gu_throw_error(EINVAL) <<"More than one value for key '" << key
                                       <<"' at '"
                                       << pv[i] << "' in parameter list.";
            }

            gu::trim(kvv[1]);
            std::string& value = kvv[1];

            param_map_t::iterator pi = pmap.find(key);

            if (pi != pmap.end())
            {
                log_warn << "Element " << pv[i] << " overwrites previous value"
                         << " '" << pi->second << "' of '" << key << "' by '"
                         << value << "'";
            }

            log_debug << "Found param '" << key << "' = '" << value << "'";
            pmap[key] = value;
        }
        else if (kvv.size() > 1)
        {
            gu_throw_error(EINVAL) << "Empty key at '" << pv[i]
                                << "' in parameter list.";
        }
    }
}

gu::Config::Config() throw () : params_() {}

gu::Config::Config (const std::string& params) throw (gu::Exception)
    : params_()
{
    Config::parse (params_, params);
}

gu_config_t*
gu_config_create (const char* params)
{
    try
    {
        const std::string& ps(params ? params : "");
        return (reinterpret_cast<gu_config_t*>(new gu::Config(ps)));
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

static long
config_check_set_args (gu_config_t* cnf, const char* key, const char* func)
{
    if (cnf && key && key[0] != '\0') return 0;

    if (!cnf) { log_fatal << "Null configuration object in " << func; }
    if (!key) { log_fatal << "Null key in " << func; }
    else if (key[0] == '\0') { log_fatal << "Empty key in " << func; }

    assert (0);

    return -EINVAL;
}

static long
config_check_get_args (gu_config_t* cnf, const char* key, const void* val_ptr,\
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

long
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

long
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
    catch (gu::Exception& e)
    {
        log_error << "Failed to parse parameter '" << key << "': " << e.what();
        return -e.get_errno();
    }
}

long
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
    catch (gu::Exception& e)
    {
        log_error << "Failed to parse parameter '" << key << "': " << e.what();
        return -e.get_errno();
    }
}

long
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
    catch (gu::Exception& e)
    {
        log_error << "Failed to parse parameter '" << key << "': " << e.what();
        return -e.get_errno();
    }
}

long
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
        return -ENOENT;
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

    conf->set (key, gu::to_string<int64_t>(val));
}

void
gu_config_set_double (gu_config_t* cnf, const char* key, double val)
{
    if (config_check_set_args (cnf, key, __FUNCTION__)) abort();

    gu::Config* conf = reinterpret_cast<gu::Config*>(cnf);

    conf->set (key, gu::to_string<double>(val));
}

void
gu_config_set_ptr    (gu_config_t* cnf, const char* key, void* val)
{
    if (config_check_set_args (cnf, key, __FUNCTION__)) abort();

    gu::Config* conf = reinterpret_cast<gu::Config*>(cnf);

    conf->set (key, gu::to_string<void*>(val));
}

void
gu_config_set_ptr    (gu_config_t* cnf, const char* key, bool val)
{
    if (config_check_set_args (cnf, key, __FUNCTION__)) abort();

    gu::Config* conf = reinterpret_cast<gu::Config*>(cnf);

    conf->set (key, gu::to_string<bool>(val));
}
