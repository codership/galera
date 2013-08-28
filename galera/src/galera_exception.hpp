//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#ifndef GALERA_EXCEPTION_HPP
#define GALERA_EXCEPTION_HPP

#include "galerautils.hpp"
#include "wsrep_api.h"

namespace galera
{
/*!
 * An exception to handle applier errors and avoid confusing wsrep error codes
 * with the standard ones
 */
class ApplyException : public gu::Exception
{
public:
    ApplyException (const std::string& msg, int err)
        : gu::Exception (msg, err)
    {
        if (err < 0) // sanity check
        {
            log_fatal
                << "Attempt to throw exception with a " << err << " code";
            abort();
        }
    }

    /* this is just int because we must handle any positive value */
    int status () { return get_errno(); }
};

static inline const char* wsrep_status_str(wsrep_status_t& status)
{
    switch (status)
    {
    case WSREP_OK:          return "WSREP_OK";
    case WSREP_WARNING:     return "WSREP_WARNING";
    case WSREP_TRX_MISSING: return "WSREP_TRX_MISSING";
    case WSREP_TRX_FAIL:    return "WSREP_TRX_FAIL";
    case WSREP_BF_ABORT:    return "WSREP_BF_ABORT";
    case WSREP_CONN_FAIL:   return "WSREP_CONN_FAIL";
    case WSREP_NODE_FAIL:   return "WSREP_NODE_FAIL";
    case WSREP_FATAL:       return "WSREP_FATAL";
    case WSREP_NOT_IMPLEMENTED: return "WSREP_NOT_IMPLEMENTED";
    default: return "(unknown code)";
    }
}

/*!
 * And exception to handle replication errors
 */
class ReplException : public gu::Exception
{
public:
    ReplException (const std::string& msg, int err)
        : gu::Exception (msg, err)
    {}
};

}
#endif /* GALERA_EXCEPTION_HPP */
