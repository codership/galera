/*
 * Copyright (C) 2011 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _gcs_gcache_h_
#define _gcs_gcache_h_

#ifndef GCS_FOR_GARB
    #include <gcache.h>
#else
    #ifndef gcache_t
        struct gcache_st;
        typedef struct gcache_st gcache_t;
    #endif
#endif

#include <gu_macros.h>

#include <cstdlib>

static inline void*
gcs_gcache_malloc (gcache_t* gcache, size_t size)
{
#ifndef GCS_FOR_GARB
    if (gu_likely(gcache != NULL))
        return gcache_malloc (gcache, size);
    else
#endif
        return ::malloc (size);
}

static inline void
gcs_gcache_free (gcache_t* gcache, const void* buf)
{
#ifndef GCS_FOR_GARB
    if (gu_likely (gcache != NULL))
        gcache_free (gcache, buf);
    else
#endif
        ::free (const_cast<void*>(buf));
}

#endif /* _gcs_gcache_h_ */
