/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*
 * Proposal for gcs error definitions (probably is obsolete)
 */

#include <string.h>
#include <assert.h>
#include "gcs.h"

/*! Error codes specific to GCS (moved here from gcs.h in preparation to
 *  get rid of entirely)*/
#define GCS_ERR_OK 0
#define GCS_ERR_BASE 0x100
enum
{
    _GCS_ERR_OTHER = GCS_ERR_BASE,
    _GCS_ERR_INTERNAL,
    _GCS_ERR_CHANNEL,
    _GCS_ERR_SOCKET,
    _GCS_ERR_BACKEND,
    _GCS_ERR_COULD_NOT_CONNECT,
    _GCS_ERR_CONNECTION_CLOSED,
    _GCS_ERR_NOT_CONNECTED,
    _GCS_ERR_NON_PRIMARY,
    _GCS_ERR_ABORTED,
    GCS_ERR_MAX
};

static char *err_string[GCS_ERR_MAX - GCS_ERR_BASE] =
{
    "GCS: unspecified error",
    "GCS: internal library error",
    "GCS: illegal channel name",
    "GCS: illegal socket",
    "GCS: unsupported backend",
    "GCS: could not connect.",
    "GCS: not connected to group.",
    "GCS: connection closed.",
    "GCS: non-primary configuration"
};

char *gcs_strerror (int err)
{
    if (err > 0) err = 0; /* positive values are successfull error codes */
    err = -err;
    if (err < GCS_ERR_BASE)
	return strerror (err);
    else if (err < GCS_ERR_MAX)
	return err_string [err - GCS_ERR_BASE];
    else
	return "GCS: Unknown Error";
}

