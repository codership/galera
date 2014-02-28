/* Copyright (C) 2012-2014 Codership Oy <info@codersip.com> */

#include "replicator_smm.hpp"
#include "gcs.hpp"
#include "galera_common.hpp"

#include "gu_uri.hpp"

const std::string galera::ReplicatorSMM::Param::base_host = "base_host";
const std::string galera::ReplicatorSMM::Param::base_port = "base_port";

static const std::string common_prefix = "repl.";

const std::string galera::ReplicatorSMM::Param::commit_order =
    common_prefix + "commit_order";
const std::string galera::ReplicatorSMM::Param::causal_read_timeout =
    common_prefix + "causal_read_timeout";

galera::ReplicatorSMM::Defaults::Defaults() : map_()
{
    map_.insert(Default(Param::base_port, BASE_PORT_DEFAULT));
    map_.insert(Default(Param::commit_order, "3"));
    map_.insert(Default(Param::causal_read_timeout, "PT30S"));
}

const galera::ReplicatorSMM::Defaults galera::ReplicatorSMM::defaults;


galera::ReplicatorSMM::InitConfig::InitConfig(gu::Config&       conf,
                                              const char* const node_address)
{
    Replicator::register_params(conf);

    std::map<std::string, std::string>::const_iterator i;

    for (i = defaults.map_.begin(); i != defaults.map_.end(); ++i)
    {
        if (i->second.empty())
            conf.add(i->first);
        else
            conf.add(i->first, i->second);
    }

    conf.add(COMMON_BASE_HOST_KEY);
    conf.add(COMMON_BASE_PORT_KEY);

    if (node_address && strlen(node_address) > 0)
    {
        gu::URI na(node_address, false);

        try
        {
            std::string const host = na.get_host();

            if (host == "0.0.0.0" || host == "0:0:0:0:0:0:0:0" || host == "::")
            {
                gu_throw_error(EINVAL) << "Bad value for 'node_address': '"
                                       << host << '\'';
            }

            conf.set(BASE_HOST_KEY, host);
        }
        catch (gu::NotSet& e) {}

        try
        {
            conf.set(BASE_PORT_KEY, na.get_port());
        }
        catch (gu::NotSet& e) {}
    }

    /* register variables and defaults from other modules */
    gcache::GCache::register_params(conf);
    gcs_register_params(reinterpret_cast<gu_config_t*>(&conf));
    Certification::register_params(conf);
    ist::register_params(conf);
}


galera::ReplicatorSMM::ParseOptions::ParseOptions(gu::Config&       conf,
                                                  const char* const opts)
{
    conf.parse(opts);
}


void
galera::ReplicatorSMM::set_param (const std::string& key,
                                  const std::string& value)
{
    if (key == Param::commit_order)
    {
        log_error << "setting '" << key << "' during runtime not allowed";
        gu_throw_error(EPERM)
            << "setting '" << key << "' during runtime not allowed";
    }
    else if (key == Param::causal_read_timeout)
    {
        causal_read_timeout_ = gu::datetime::Period(value);
    }
    else if (key == Param::base_host ||
             key == Param::base_port)
    {
        // nothing to do here, these params take effect only at
        // provider (re)start
    }
    else
    {
        log_warn << "parameter '" << "' not found";
        assert(0);
        throw gu::NotFound();
    }
}

void
galera::ReplicatorSMM::param_set (const std::string& key,
                                  const std::string& value)
{
    try
    {
        if (config_.get(key) == value) return;
    }
    catch (gu::NotFound&) {}

    bool found(false);

    // Note: base_host is treated separately here as it cannot have
    // default value known at compile time.
    if (defaults.map_.find(key) != defaults.map_.end() ||
        key                     == Param::base_host) // is my key?
    {
        found = true;
        set_param (key, value);
        config_.set (key, value);
    }

    if (key == Certification::PARAM_LOG_CONFLICTS)
    {
        cert_.set_log_conflicts(value);
    }
    // this key might be for another module
    else if (0 != key.find(common_prefix))
    {
        try
        {
            gcs_.param_set (key, value);
            found = true;
        }
        catch (gu::NotFound&) {}

        try
        {
            gcache_.param_set (key, value);
            found = true;
        }
        catch (gu::NotFound&) {}
    }

    if (!found) throw gu::NotFound();
}

std::string
galera::ReplicatorSMM::param_get (const std::string& key) const
{
    return config_.get(key);
}

