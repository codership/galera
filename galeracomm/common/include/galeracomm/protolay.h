/**
 * @file protolay.h
 *
 * Common protocol interface definition. All layers should implement
 * this and provide it for upper layers. This is used in combination
 * of read and write buffers.
 *
 * @author Teemu Ollakka <teemu.ollakka@codership.com>
 *
 * Copyright (C) 2007 Codership Oy
 *
 */

#ifndef PROTOLAY_H
#define PROTOLAY_H

#include <gcomm/types.h>
#include <gcomm/readbuf.h>
#include <gcomm/writebuf.h>
#include <gcomm/poll.h>


typedef struct protolay_ protolay_t;
typedef struct up_meta_ up_meta_t;
typedef struct down_meta_ down_meta_t;

/*
 * Example: 
 *
 * typedef struct vs_ev_ {
 *     up_meta_t *meta;
 * } vs_ev_t; 
 * static inline up_meta_t *vs_ev_meta(const vs_ev_t *ev) 
 * {
 *     return ev->meta;
 * }
 * vs_ev_t ev = {(up_meta_t*)&ev, ...};
 *
 * protolay_pass_up(pl, pl->up_ctx, rb, vs_ev_meta(&ev));
 *
 */

typedef enum {
    /* Operation has been interrupted. All system calls should be 
     * retried on EINTR unless this flag is set. */
    PROTOLAY_F_INTERRUPTED = 0x1,
    /* Some of the underlying layers has failed, but some are still 
     * operational so that connectivity to outside world may still exist */
    PROTOLAY_F_SUSPECT = 0x2, 
    /* Underlying protocol layers have failed so that there is no 
     * connectivity to outside world */
    PROTOLAY_F_FAILED = 0x4
} protolay_flags_e;



/**
 *
 */
protolay_t *protolay_new(void *priv, void (*free_cb)(void *));

/**
 *
 */
void protolay_free(protolay_t *);

/**
 *
 */

void protolay_fail(protolay_t *);

/**
 *
 */
void protolay_set_up(protolay_t *,
		     protolay_t *up_ctx,
		     void (*pass_up_cb)(protolay_t *, 
					const readbuf_t *, const size_t, 
					const up_meta_t *));
/**
 *
 */
void protolay_set_down(protolay_t *,
		       protolay_t *down_ctx,
		       int (*pass_down_cb)(protolay_t *, writebuf_t *wb,
					   const down_meta_t *));

/**
 *
 */
void protolay_set_poll(protolay_t *,
		       void (*set_poll_up_cb)(protolay_t *, poll_t *, bool),
		       void (*set_poll_down_cb)(protolay_t *, poll_t *, bool),
		       int (*poll_up_cb)(protolay_t *));


/**
 *
 */
void *protolay_get_priv(const protolay_t *p);


/**
 *
 */
void protolay_pass_up(protolay_t *p, const readbuf_t *rb,
		      const size_t read_offset, const up_meta_t *);

/**
 *
 */
int protolay_pass_down(protolay_t *p, writebuf_t *wb, 
			const down_meta_t *dm);


/**
 *
 */
bool protolay_get_down_atomic(const protolay_t *);

/**
 *
 */
bool protolay_get_up_atomic(const protolay_t *);

/**
 *
 */
int protolay_poll_up(protolay_t *);



#endif /* PROTOLAY_H */
