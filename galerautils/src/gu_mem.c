// Copyright (C) 2007 Codership Oy <info@codership.com>

/**
 * Debugging versions of memmory functions
 *
 * $Id$
 */

#include <stdint.h>
#include <limits.h>
#include <assert.h>

#include "gu_mem.h"
#include "gu_log.h"

/* Some global counters - can be inspected by gdb */
static volatile ssize_t gu_mem_total    = 0;
static volatile ssize_t gu_mem_allocs   = 0;
static volatile ssize_t gu_mem_reallocs = 0;
static volatile ssize_t gu_mem_frees    = 0;

typedef struct mem_head
{
    const char*  file;
    unsigned int line;
    size_t       used;
    size_t       allocated;
    uint32_t     signature;
}
mem_head_t;

#define MEM_SIGNATURE 0x13578642                   /**< Our special marker */

// Returns pointer to the first byte after the head structure
#define TAIL(head) ((void*)((mem_head_t*)(head) + 1))
// Returns pointer to the head preceding tail
#define HEAD(tail) ((mem_head_t*)(tail) - 1)

void* gu_malloc_dbg  (size_t size,
                      const char* file, unsigned int line)
{
    if (size) {
        size_t const total_size = size + sizeof(mem_head_t);
        mem_head_t* const ret = (mem_head_t*) malloc (total_size);
        if (ret) {
	    gu_mem_total  += total_size;
	    gu_mem_allocs++;
	    ret->signature = MEM_SIGNATURE;
	    ret->allocated = total_size;
	    ret->used      = size;
	    ret->file      = file;
	    ret->line      = line;
	    // cppcheck-suppress memleak
	    return TAIL(ret);
	}
    }
    return NULL;
}

void* gu_calloc_dbg  (size_t nmemb, size_t size,
                      const char* file, unsigned int line)
{
    if (size != 0 && nmemb != 0) {
        size_t const total_size = size*nmemb + sizeof(mem_head_t);
        mem_head_t* const ret = (mem_head_t*) calloc (total_size, 1);
        if (ret) {
            size_t const total_size = size*nmemb + sizeof(mem_head_t);
	    gu_mem_total  += total_size;
	    gu_mem_allocs++;
	    ret->signature = MEM_SIGNATURE;
	    ret->allocated = total_size;
	    ret->used      = size;
	    ret->file      = file;
	    ret->line      = line;
	    return TAIL(ret);
	}
    }
    return NULL;
}

void* gu_realloc_dbg (void* ptr, size_t size,
                      const char* file, unsigned int line)
{
    if (ptr) {
        if (size > 0) {
            mem_head_t* const old = HEAD(ptr);

            if (MEM_SIGNATURE != old->signature) {
                gu_error ("Attempt to realloc uninitialized pointer at "
                          "file: %s, line: %d", file, line);
                assert (0);
            }

            size_t const total_size = size + sizeof(mem_head_t);
            mem_head_t* const ret = (mem_head_t*) realloc (old, total_size);
            if (ret) {
		gu_mem_reallocs++;
		gu_mem_total  -= ret->allocated; // old size
		ret->allocated = total_size;
		gu_mem_total  += ret->allocated; // new size
		ret->used      = size;
		ret->file      = file;
		ret->line      = line;
		return TAIL(ret);
	    }
	    else { // realloc failed
		return NULL;
	    }
	}
	else {
	    gu_free_dbg (ptr, file, line);
	    return NULL;
	}
    }
    else {
	return gu_malloc_dbg (size, file, line);
    }
    return NULL;
}

void  gu_free_dbg    (void* ptr,
		      const char* file, unsigned int line)
{
    mem_head_t* head;
    
    if (NULL == ptr) {
	gu_debug ("Attempt to free NULL pointer at file: %s, line: %d",
		  file, line);
	return; /* As per specification - no operation is performed */
    }
    
    head = HEAD(ptr);

    if (MEM_SIGNATURE != head->signature) {
	gu_error ("Attempt to free uninitialized pointer "
		  "at file: %s, line: %d", file, line);
	assert (0);
    }

    if (0 == head->used) {
	gu_error ("Attempt to free pointer the second time at "
		  "file: %s, line: %d. "
		  "Was allocated at file: %s, line: %d.",
		  file, line, head->file, head->line);
	assert (0);
    }

    gu_mem_total   -= head->allocated;
    gu_mem_frees++;
    head->allocated = 0;
    head->used      = 0;
    free (head);
}

void gu_mem_stats (ssize_t* total, ssize_t* allocs, ssize_t* reallocs, ssize_t* deallocs)
{
    *total    = gu_mem_total;
    *allocs   = gu_mem_allocs;
    *reallocs = gu_mem_reallocs;
    *deallocs = gu_mem_frees;
}
