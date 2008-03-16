/**
 * @file vs_backend.h
 *
 * Backend interface for VS protocol.
 *
 * @author Teemu Ollakka <teemu.ollakka@codership.com>
 *
 * Copyright (C) 2007 Codership Oy
 */
#ifndef VS_BACKEND_H
#define VS_BACKEND_H

#include "gcomm/protolay.h"
#include "gcomm/addr.h"

typedef struct vs_backend_ vs_backend_t;

struct vs_backend_ {
    void *priv;
    protolay_t *pl;
    int (*connect)(vs_backend_t *, const group_id_t);
    void (*close)(vs_backend_t *);
    void (*free)(vs_backend_t *);
    addr_t (*get_self_addr)(const vs_backend_t *);
};

/**
 * Create new backend. 
 *
 * @param conf String containing backend configuration
 * @param poll Pointer to poll struct that is used to handle I/O
 * @param up_ctx Protolay context of upper (VS) layer
 * @param pass_up_cb Callback function that is called for every upgoing 
 *                   event/message
 */
vs_backend_t *vs_backend_new(const char *conf,
			     poll_t *poll,
			     protolay_t *up_ctx,
			     void (*pass_up_cb)(protolay_t *,
						const readbuf_t *,
						const size_t,
						const up_meta_t *));

/**
 * Free backend
 *
 * @param be Backend to be freed
 */
void vs_backend_free(vs_backend_t *be);


/**
 * Connect to backend and start communicating with members of group.
 * 
 *
 */
int vs_backend_connect(vs_backend_t *be, const group_id_t group);

/**
 * Close connection to backend.
 */
void vs_backend_close(vs_backend_t *be);

/**
 *
 */

addr_t vs_backend_get_self_addr(const vs_backend_t *be);


#if 0

#include "gcomm/vs.h"
#include "gcomm/vs_msg.h"
#include "gcomm/poll.h"
#include "gcomm/protolay.h"

typedef struct vs_backend_ vs_backend_t;
typedef void (*vs_backend_cb_f)(void *, const vs_msg_t *);

/**
 * Interface definition of all VS backends.
 */
struct vs_backend_ {
    protolay_t *pl;
    void (*free)(vs_backend_t *);
    int (*connect)(vs_backend_t *be, const group_id_t);
    void (*disconnect)(vs_backend_t *);
    int (*send)(vs_backend_t *, const vs_msg_t *);
    int (*sched)(vs_backend_t *, poll_t *, int);
    
    void (*set_recv_callback)(vs_backend_t *be, void *user_context, 
			      void (*fn)(void *, const vs_msg_t *));
    addr_t (*get_self_addr)(const vs_backend_t *);
    vs_view_id_t (*get_view_id)(const vs_backend_t *);
    int (*get_fd)(const vs_backend_t *);
};

vs_backend_t *vs_backend_new(const char *conf, void *user_context, 
			     void (*send_cb)(void *, const vs_msg_t *));


void vs_backend_free(vs_backend_t *);
int vs_backend_connect(vs_backend_t *be, const group_id_t group);
void vs_backend_disconnect(vs_backend_t *be);

int vs_backend_send(vs_backend_t *be, const vs_msg_t *msg);


void vs_backend_set_recv_cb(vs_backend_t *, void *user_context, 
			    void (*fn)(void *, const vs_msg_t *));

int vs_backend_sched(vs_backend_t *be, poll_t *, int fd);


addr_t vs_backend_get_self_addr(const vs_backend_t *be);
vs_view_id_t vs_backend_get_view_id(const vs_backend_t *be);

#endif /* 0 */

#endif /* VS_BACKEND_H */
