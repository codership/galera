

/**
 * @file vs.h
 *
 * View synchrony interface.
 *
 * Author: Teemu Ollakka <teemu.ollakka@codership.com>
 *
 * Copyright (C) 2007 Codership Oy <info@codership.com>
 */

#ifndef VS_H
#define VS_H


#include <gcomm/addr.h>
#include <gcomm/seq.h>
#include <gcomm/protolay.h>
#include <gcomm/vs_view.h>
#include <gcomm/vs_msg.h>

typedef struct vs_ vs_t;

/**
 * Create new vs. 
 *
 * @param backend_url URL to connect backend ("type:addr")
 */
vs_t *vs_new(const char *backend_url, 
	     poll_t *poll, 
	     protolay_t *up_ctx,
	     void (*pass_up_cb)(protolay_t *, 
				const readbuf_t *,
				const size_t,
				const up_meta_t *));

void vs_free(vs_t *vs);


/**
 * Open VS connection and join to group
 *
 * @param group To join into
 */
int vs_open(vs_t *vs, const group_id_t group);

void vs_close(vs_t *vs);

int vs_join(vs_t *vs);

void vs_leave(vs_t *vs);

addr_t vs_get_self_addr(const vs_t *vs);

#endif /* VS_H */
