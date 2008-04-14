/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*********************************************************/
/*  This unit initializes the backend given backend URI  */
/*********************************************************/

#include <errno.h>
#include <string.h>
#include <galerautils.h>
#include "gcs_backend.h"

#include "gcs_dummy.h"
#ifdef    GCS_USE_SPREAD
#include "gcs_spread.h"
#endif /* GCS_USE_SPREAD */
#ifdef    GCS_USE_VS
#include "gcs_vs.h"
#endif /* GCS_USE_VS */

/* Static array describing backend ID - open() pairs */
static
struct {
    const char* id;
    gcs_backend_open_t open;
}
    const backend[] =
    {
#ifdef    GCS_USE_VS
        { "gcomm", gcs_vs_open },
#endif /* GCS_USE_VS */
#ifdef    GCS_USE_SPREAD
        { "spread", gcs_spread_open },
#endif /* GCS_USE_SPREAD */
        { "dummy", gcs_dummy_open },
        { NULL, NULL } // terminating pair
    };

static const char backend_sep[] = "://";

/* Returns 1 is backend matches, 0 otherwise */
static long
backend_type_is (const char* uri, const char* type, const size_t len)
{
    if (len == strlen(type)) {
        if (!strncmp (uri, type, len)) return 1;
    }
    return 0;
}

long
gcs_backend_init (gcs_backend_t* const bk,
		  const char*    const channel,
		  const char*    const uri)
{
    char* sep;

    assert (NULL != bk);
    assert (NULL != uri);

    sep = strstr (uri, backend_sep);

    if (NULL != sep) {
        size_t type_len  = sep - uri;
        const char* addr = sep + strlen(backend_sep);
        long i;
        /* try to match any of specified backends */
        for (i = 0; backend[i].id != NULL; i++) {
            if (backend_type_is (uri, backend[i].id, type_len))
                return backend[i].open(bk, channel, addr);
        }

        /* no backends matched */
        return -ESOCKTNOSUPPORT;
    }
    return -EINVAL;
}

