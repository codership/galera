/* Copyright (C) 2010 Codership Oy <info@codersip.com> */

#include "replicator_smm.hpp"
#include "gcs.hpp"

static const std::string common_prefix = "replicator.";

const std::string galera::ReplicatorSMM::Param::commit_order =
    common_prefix + "commit_order";

galera::ReplicatorSMM::Defaults::Defaults() : map_()
{
    map_.insert(Default(Param::commit_order, "3"));
}

const galera::ReplicatorSMM::Defaults galera::ReplicatorSMM::defaults;

galera::ReplicatorSMM::SetDefaults::SetDefaults(gu::Config&     conf,
                                                const Defaults& def)
{
    std::map<std::string, std::string>::const_iterator i;

    for (i = def.map_.begin(); i != def.map_.end(); ++i)
    {
        if (!conf.has(i->first)) conf.set(i->first, i->second);
    }
}

void
galera::ReplicatorSMM::set_param (const std::string& key,
                                  const std::string& value)
    throw (gu::Exception)
{
    if (key == Param::commit_order)
    {
        gu_throw_error(EPERM)
            << "setting '" << key << "' during runtime not allowed";
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

