// Copyright (C) 2007 Codership Oy <info@codership.com>
/**
 * @file
 * Declarations of memory allocation functions and macros
 *
 * $Id$
 */

#ifndef _gu_mem_h_
#define _gu_mem_h_

#include <stdlib.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** @name Functions to help with dynamic allocation debugging.
 *  Take additional __FILE__ and __LINE__ arguments. Should be
 *  used as part of macros defined below */
/*@{*/
void* gu_malloc_dbg  (size_t size,
		      const char* file, unsigned int line);
void* gu_calloc_dbg  (size_t nmemb, size_t size,
		      const char* file, unsigned int line);
void* gu_realloc_dbg (void* ptr, size_t size,
		      const char* file, unsigned int line);
void  gu_free_dbg    (void* ptr,
		      const char* file, unsigned int line);
/*@}*/

/** Reports statistics on the current amount of allocated memory
 *  total number of allocations and deallocations */
void gu_mem_stats (ssize_t* total, ssize_t* allocs, ssize_t* reallocs,
                   ssize_t* deallocs);

/** @name Applications should use the following macros */
/*@{*/
#ifdef DEBUG_MALLOC

#define gu_malloc(S)    gu_malloc_dbg  ((S), __FILE__, __LINE__)
#define gu_calloc(N,S)  gu_calloc_dbg  ((N), (S), __FILE__, __LINE__)
#define gu_realloc(P,S) gu_realloc_dbg ((P), (S), __FILE__, __LINE__)
#define gu_free(P)      gu_free_dbg    ((P), __FILE__, __LINE__)

#else  /* !DEBUG_MALLOC - use standard allocation routines */

#define gu_malloc(S)    malloc  ((S))
#define gu_calloc(N,S)  calloc  ((N), (S))
#define gu_realloc(P,S) realloc ((P), (S))
#define gu_free(P)      free    ((P))

#endif /* DEBUG_MALLOC */

/** Convenience macros - to avoid code clutter */
#define GU_MALLOC(type)      (type*) gu_malloc (sizeof(type))
#define GU_MALLOCN(N,type)   (type*) gu_malloc ((N) * sizeof(type))
#define GU_CALLOC(N,type)    (type*) gu_calloc ((N), sizeof(type))
#define GU_REALLOC(P,N,type) (type*) gu_realloc((P), (N) * sizeof(type))

/*@}*/

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _gu_mem_h_ */
