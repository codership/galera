/*
 * Copyright (C) 2008-2015 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gcs_msg_type.hpp"

const char* gcs_msg_type_string[GCS_MSG_MAX] = {
    "ERROR",
    "ACTION",
    "LAST",
    "COMPONENT",
    "STATE_UUID",
    "STATE_MSG",
    "JOIN",
    "SYNC",
    "FLOW",
    "VOTE",
    "CAUSAL"
};
