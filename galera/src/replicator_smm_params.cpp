/* Copyright (C) 2010 Codership Oy <info@codersip.com> */

#include "replicator_smm.hpp"
#include "gcs.hpp"

static bool
my_key (const std::string& key)
{
    if (key == "replicator.co_mode") return true;
    return false;
}

static void
set_param (const std::string& key, const std::string& value)
    throw (gu::Exception)
{
    if (key == "replicator.co_mode")
    {
        gu_throw_error(EPERM)
            << "setting replicator.co_mode during runtime not allowed";
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

    if (my_key(key))
    {
        found = true;
        set_param (key, value);
        config_.set (key, value);
    }

    if (0 != key.find("replicator.")) // this key might be for another module
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

