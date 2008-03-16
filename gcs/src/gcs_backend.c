// Copyright (C) 2007 Codership Oy <info@codership.com>
/*****************************************/
/*                                       */
/*****************************************/

#include <errno.h>
#include "gcs_backend.h"

#include "gcs_dummy.h"
#ifdef    GCS_USE_SPREAD
#include "gcs_spread.h"
#endif /* GCS_USE_SPREAD */
#ifdef GCS_USE_VS
#include "gcs_vs.h"
#endif /* GCS_USE_VS */

long
gcs_backend_init (gcs_backend_t*     const bk,
		  const char*        const channel,
		  const char*        const sock_addr,
		  gcs_backend_type_t const backend)
{
    switch (backend)
    {
#ifdef GCS_USE_VS
    case GCS_BACKEND_VS:
	return gcs_vs_open(bk, channel, sock_addr);
#endif /* GCS_USE_VS */
#ifdef    GCS_USE_SPREAD
    case GCS_BACKEND_SPREAD:
	return gcs_spread_open (bk, channel, sock_addr);
#endif /* GCS_USE_SPREAD */
    case GCS_BACKEND_DUMMY:
	return gcs_dummy_open (bk, channel, sock_addr);
    default:
	return -ESOCKTNOSUPPORT;
    }

    return 0;
}

