//
// Copyright (C) 2010-2014 Codership Oy <info@codership.com>
//

#include "wsrep_params.hpp"
#include "gu_dbug.h"
#include "gu_debug_sync.hpp"

void
wsrep_set_params (galera::Replicator& repl, const char* params)
{
    if (!params) return;

    std::vector<std::pair<std::string, std::string> > pv;
    gu::Config::parse (pv, params);

    for (size_t i(0); i < pv.size(); ++i)
    {
        const std::string& key(pv[i].first);
        const std::string& value(pv[i].second);

        try
        {
            if (key == galera::Replicator::Param::debug_log)
            {
                bool val(gu::from_string<bool>(value));
                if (val == true)
                {
                    gu_conf_debug_on();
                }
                else
                {
                    gu_conf_debug_off();
                }
            }
#ifdef GU_DBUG_ON
            else if (key == galera::Replicator::Param::dbug)
            {
                if (value.empty())
                {
                    GU_DBUG_POP();
                }
                else
                {
                    GU_DBUG_PUSH(value.c_str());
                }
            }
            else if (key == galera::Replicator::Param::signal)
            {
                gu_debug_sync_signal(value);
            }
#endif /* GU_DBUG_ON */
            else
            {
                log_debug << "Setting param '"
                          << key << "' = '" << value << '\'';
                repl.param_set(key, value);
            }
        }
        catch (gu::NotFound&)
        {
            log_warn << "Unknown parameter '" << key << "'";
            gu_throw_error(EINVAL) << "Unknown parameter' " << key << "'";
        }
        catch (gu::Exception& e)
        {
            log_warn << "Setting parameter '" << key << "' to '"
                     << value << "' failed: " << e.what();
            throw;
        }
    }
}

char* wsrep_get_params(const galera::Replicator& repl)
{
    std::ostringstream os;
    os << repl.params();
    return strdup(os.str().c_str());
}
