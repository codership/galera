// Copyright (C) 2007 Codership Oy <info@codership.com>
/*
 *  Function and macro definitions internal to GCS
 */

#ifndef _gcs_defs_h_
#define _gcs_defs_h_

#include "galerautils.h"

/** We will hardly ever meet Big Endian machines now, so why waste cycles
 *  on converting LE data to classic BE network byte order and back?
 *  Let's use LE throughout. */
#define gcs_htons(x) gu_le16(x)
#define gcs_ntohs(x) gcs_htons(x)
#define gcs_htonl(x) gu_le32(x)
#define gcs_ntohl(x) gcs_htonl(x)

#endif /* _gcs_defs_h_ */
