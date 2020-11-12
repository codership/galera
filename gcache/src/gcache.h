/*
 * Copyright (C) 2010-2014 Codership Oy <info@codership.com>
 */
/*!
 * @file C-interface to GCache.
 */

#ifndef _gcache_h_
#define _gcache_h_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "gu_config.h"

typedef struct gcache_st gcache_t;

extern gcache_t* gcache_create  (gu_config_t* conf, const char* data_dir);
extern void      gcache_destroy (gcache_t* gc);

extern void* gcache_malloc      (gcache_t* gc, int size);
extern void  gcache_free        (gcache_t* gc, const void* ptr);
extern void* gcache_realloc     (gcache_t* gc, void* ptr, int size);

extern int64_t gcache_seqno_min (gcache_t* gc);

#ifdef __cplusplus
}
#endif

#endif /* _gcache_h_ */
