/**
 * @file protolay.c
 *
 *
 * @author Teemu Ollakka <teemu.ollakka@codership.com>
 *
 * Copyright (C) 2007 Codership Oy
 */
#include "gcomm/protolay.h"

#include <stdlib.h>
#include <assert.h>

struct protolay_ {
    protolay_t *up_ctx;
    protolay_t *down_ctx;
    void *priv;
    protolay_flags_e flags;        
    
    /* Pass message buffer upwards on protocol layer */
    void (*pass_up)(protolay_t *up_ctx, const readbuf_t *rb, 
		    const size_t read_offset, const up_meta_t *);
    /* Pass message buffer downwards on protocol layer */
    int (*pass_down)(protolay_t *down_ctx, writebuf_t *wb, 
		     const down_meta_t *);
    
    /* Return true/false whether pass_down() is atomic operation
     * in sense that if call returns success, message has been 
     * completely processed. If pass_down() is not atomic, poll_t
     * must be used to enforce retry on POLL_OUT event. 
     * This is highly dependent on process workflow, so all 
     * layers must exhibit the same behaviour */
    bool (*get_down_atomic)(const protolay_t *);
    
    /* Return true/false whether poll_up() is atomic in the sense
     * that once poll_up() returns successfully, it has received 
     * and processed complete message. Note that this may not 
     * necessarily mean that message has been passed to the top 
     * of layer stack */
    bool (*get_up_atomic)(const protolay_t *);
    
    /* Poll for up events. If poll is atomic, it returns success after it has
     * received and processed one complete message. However, this does not 
     * necessarily mean that message has been passed up to topmost layer */
    int (*poll_up)(protolay_t *);
    
    /* Build or tear down poll set for up events */
    void (*set_poll_up)(protolay_t *, poll_t *, bool);
    
    /* Build or tear down poll set for down events */
    void (*set_poll_down)(protolay_t *, poll_t *, bool);
    
    /* Free protocol layer recursively */
    void (*free)(void *);
};

protolay_t *protolay_new(void *priv, void (*free_cb)(void *))
{
    protolay_t *p;

    p = malloc(sizeof(protolay_t));
    memset(p, 0, sizeof(protolay_t));
    p->priv = priv;
    p->free = free_cb;
    return p;
}

void protolay_free(protolay_t *p)
{
    if (p) {
	protolay_free(p->down_ctx);
	if (p->free)
	    p->free(p->priv);
	free(p);
    }
}

void protolay_fail(protolay_t *p)
{
    assert(p != NULL);
    p->flags |= PROTOLAY_F_FAILED;
}


void protolay_set_up(protolay_t *p, protolay_t *up,
		     void (*pass_up_cb)(protolay_t *,
					const readbuf_t *, const size_t, 
					const up_meta_t *)) 
{
    p->up_ctx = up;
    p->pass_up = pass_up_cb;
}

void protolay_set_down(protolay_t *p,
		       protolay_t *down,
		       int (*pass_down_cb)(protolay_t *, writebuf_t *,
					   const down_meta_t *))
{
    p->down_ctx = down;
    p->pass_down = pass_down_cb;
}

void protolay_set_poll(protolay_t *p,
		       void (*set_poll_up_cb)(protolay_t *, poll_t *, bool),
		       void (*set_poll_down_cb)(protolay_t *, poll_t *, bool),
		       int (*poll_up_cb)(protolay_t *))
{
    assert(p);
    assert(!p->poll_up);
    assert(!p->set_poll_up);
    assert(!p->set_poll_down);
    p->set_poll_up = set_poll_up_cb;
    p->set_poll_down = set_poll_down_cb;
    p->poll_up = poll_up_cb;
}

void *protolay_get_priv(const protolay_t *p)
{
    return p->priv;
}

void protolay_pass_up(protolay_t *p, const readbuf_t *rb,
		      const size_t read_offset, const up_meta_t *um)
{
    assert(p);
    assert(p->up_ctx);
    p->pass_up(p->up_ctx, rb, read_offset, um);
}

int protolay_pass_down(protolay_t *p, writebuf_t *wb, 
			const down_meta_t *dm)
{
    assert(p);
    assert(p->down_ctx);
    return p->pass_down(p->down_ctx, wb, dm);
}

bool protolay_get_down_atomic(const protolay_t *p)
{
    assert(p);
    return false;
}

bool protolay_get_up_atomic(const protolay_t *p)
{
    assert(p);
    return false;
}

void protolay_set_poll_up(protolay_t *p, poll_t *pl, bool b)
{
    assert(p);
    assert(p->set_poll_up);
    p->set_poll_up(p, pl, b);
}

void protolay_set_poll_down(protolay_t *p, poll_t *pl, bool b)
{
    assert(p);
    assert(p->set_poll_down);
    p->set_poll_down(p, pl, b);
}

int protolay_poll_up(protolay_t *p)
{
    assert(p);
    if (!p->poll_up && p->down_ctx) {
	return protolay_poll_up(p->down_ctx);
    } else {
	assert(p->poll_up);
	return p->poll_up(p);
    } 
}

