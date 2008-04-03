/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*
 * Proposal for gcs error definitions     
 */

#include <string.h>
#include <assert.h>
#include "gcs.h"

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

