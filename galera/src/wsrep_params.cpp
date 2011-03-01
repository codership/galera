//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#include "wsrep_params.hpp"

void
wsrep_set_params (galera::Replicator& repl, const char* params)
    throw (gu::Exception)
{
    if (!params) return;

    gu::Config::param_map_t pm;
    gu::Config::parse (pm, params);

    for (gu::Config::param_map_t::const_iterator i = pm.begin();
         i != pm.end(); ++i)
    {
        try
        {
            log_debug << "Setting param '" << i->first << "' = '" << i->second
                      << "'";
            repl.param_set(i->first, i->second);
        }
        catch (gu::NotFound&)
        {
            log_warn << "Unknown parameter '" << i->first << "'";
        }
        catch (gu::Exception& e)
        {
            log_warn << "Setting parameter '" << i->first << "' to '"
                     << i->second << "' failed: " << e.what();
        }
    }
}

char* wsrep_get_params(const galera::Replicator& repl)
{
    std::ostringstream os;
    os << repl.params();
    return strdup(os.str().c_str());
}
