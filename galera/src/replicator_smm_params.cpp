/* Copyright (C) 2012 Codership Oy <info@codersip.com> */

#include "replicator_smm.hpp"
#include "gcs.hpp"
#include "galera_common.hpp"

#include <gu_uri.hpp>

static const std::string common_prefix = "replicator.";

const std::string galera::ReplicatorSMM::Param::commit_order =
    common_prefix + "commit_order";
const std::string galera::ReplicatorSMM::Param::causal_read_timeout =
    common_prefix + "causal_read_timeout";

galera::ReplicatorSMM::Defaults::Defaults() : map_()
{
    map_.insert(Default(Param::commit_order, "3"));
    map_.insert(Default(Param::causal_read_timeout, "PT30S"));
    map_.insert(Default(Certification::Param::log_conflicts,
                        Certification::Defaults::log_conflicts));
}

const galera::ReplicatorSMM::Defaults galera::ReplicatorSMM::defaults;

galera::ReplicatorSMM::SetDefaults::SetDefaults(gu::Config&     conf,
                                                const Defaults& def,
                                                const char* const node_address)
{
    std::map<std::string, std::string>::const_iterator i;

    for (i = def.map_.begin(); i != def.map_.end(); ++i)
    {
        if (!conf.has(i->first)) conf.set(i->first, i->second);
    }

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
        catch (gu::NotSet&) {}

        try
        {
            conf.set(BASE_PORT_KEY, na.get_port());
        }
        catch (gu::NotSet&) {}
    }
}

void
galera::ReplicatorSMM::set_param (const std::string& key,
                                  const std::string& value)
    throw (gu::Exception)
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
    else if (key == Certification::Param::log_conflicts)
    {
        cert_.set_log_conflicts(value);
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
    throw (gu::Exception, gu::NotFound)
{
    try
    {
        if (config_.get(key) == value) return;
    }
    catch (gu::NotFound&) {}

    bool found(false);

    if (defaults.map_.find(key) != defaults.map_.end()) // is my key?
    {
        found = true;
        set_param (key, value);
        config_.set (key, value);
    }

    if (0 != key.find(common_prefix)) // this key might be for another module
    {
        try
        {
            gcs_.param_set (key, value);
            found = true;
        }
        catch (gu::NotFound&) {}
    }

    if (!found) throw gu::NotFound();
}

std::string
galera::ReplicatorSMM::param_get (const std::string& key) const
    throw (gu::Exception, gu::NotFound)
{
    return config_.get(key);
}

