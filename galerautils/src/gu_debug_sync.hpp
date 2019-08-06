//
// Copyright (C) 2014 Codership Oy <info@codership.com>
//

//
// Define -DGU_DBUG_ON to enable GU_DBUG macros
//
// Usage:
//
// GU_DBUG_SYNC_WAIT("sync_point_identifier")
//
// The macro above will block whenever "dbug=d,sync_point_identifier"
// parameter has been passed to provider.
//
// Blocking waiters can be signalled by setting "signal=sync_point_identifier"
// option.
//
// List of waiters can be monitored from wsrep debug_sync_waiters status
// variable.
//

#ifndef GU_DEBUG_SYNC_HPP
#define GU_DEBUG_SYNC_HPP

#ifdef GU_DBUG_ON

#include <string>
#include "gu_dbug.h"

#define GU_DBUG_SYNC_WAIT(_c)                   \
    GU_DBUG_EXECUTE(_c, gu_debug_sync_wait(_c);)

#define GU_DBUG_SYNC_EXECUTE(_c,_cmd)           \
    GU_DBUG_EXECUTE(_c, _cmd);

// Wait for sync signal identified by sync string
void gu_debug_sync_wait(const std::string& sync);

// Signal waiter identified by sync string
void gu_debug_sync_signal(const std::string& sync);

// Get list of active sync waiters
std::string gu_debug_sync_waiters();

#else

#define GU_DBUG_SYNC_WAIT(_c)
#define GU_DBUG_SYNC_EXECUTE(_c,_cmd)

#endif // GU_DBUG_ON

#endif // GU_DEBUG_SYNC_HPP
