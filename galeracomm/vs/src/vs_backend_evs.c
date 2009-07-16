#define _GNU_SOURCE
#include "vs_backend_evs.h"
#include "vs_backend.h"

#include "galeracomm/transport.h"

#include <string.h>
#include <errno.h>
#include <stdlib.h>

typedef struct vs_backend_evs_ {
     vs_backend_t be;
     transport_t *tp;
     char *url;
     void (*recv_cb)(void *, const vs_msg_t *);
     void *user_context;
     bool blocking;
     vs_view_t *view;
     addr_t self_addr;
} vs_backend_evs_t;


static int vs_backend_evs_connect(vs_backend_t *be, const group_id_t grp)
{
     vs_backend_evs_t *evs = (vs_backend_evs_t*) be;
     return transport_open(evs->tp, evs->url);
}

static void vs_backend_evs_disconnect(vs_backend_t *be)
{
     vs_backend_evs_t *evs = (vs_backend_evs_t*) be;
     transport_close(evs->tp);
}

static int vs_backend_evs_send(vs_backend_t *be, const vs_msg_t *vs_msg)
{
     vs_backend_evs_t *evs = (vs_backend_evs_t*) be;
     return transport_send(evs->tp, vs_msg_get_writebuf(vs_msg));
}

static void vs_backend_evs_set_recv_callback(vs_backend_t *be, 
					     void *user_context, 
					     void (*fn)(void *, 
							const vs_msg_t *))
{
     vs_backend_evs_t *evs = (vs_backend_evs_t*) be;
     evs->recv_cb = fn;
     evs->user_context = user_context;
}

static int vs_backend_evs_sched(vs_backend_t *be, poll_t *poll, int fd)
{
     int ret = 0;
     const msg_t *msg = NULL;
     vs_msg_t *vs_msg = NULL;
     vs_backend_evs_t *evs = (vs_backend_evs_t*) be;
     ret = transport_send(evs->tp, NULL);
     if (ret == 0) {
	  if (poll)
	       poll_unset(poll, transport_fd(evs->tp), POLL_OUT);
     } else if (ret == EBUSY) {
	  if (poll)
	       poll_set(poll, transport_fd(evs->tp), POLL_OUT);
     } else {
	  vs_msg = vs_msg_new(VS_MSG_ERR, 0, 0, 0, 0, NULL, NULL);
	  evs->recv_cb(evs->user_context, vs_msg);
	  vs_msg_free(vs_msg);
	  return 0;
     }
     ret = transport_recv(evs->tp, &msg);
     if (ret == 0) {
	  vs_msg = vs_msg_read(msg);
	  if (evs->view == NULL && vs_msg_get_type(vs_msg) == VS_MSG_TRANS_CONF) {
	       evs->self_addr = vs_msg_get_source(vs_msg);
	  }
	  evs->recv_cb(evs->user_context, vs_msg);
	  vs_msg_free(vs_msg);
	  ret = 1;
     } else if (ret != EBUSY) {
	  vs_msg = vs_msg_new(VS_MSG_ERR, 0, 0, 0, 0, NULL, NULL);
	  evs->recv_cb(evs->user_context, vs_msg);
	  vs_msg_free(vs_msg);
	  return 0;
     }
     return ret;
}

static addr_t vs_backend_evs_get_self_addr(const vs_backend_t *be)
{
     const vs_backend_evs_t *evs = (vs_backend_evs_t*) be;
     return evs->self_addr;
}

static int vs_backend_evs_get_fd(const vs_backend_t *be)
{
     vs_backend_evs_t *evs = (vs_backend_evs_t*) be;
     return transport_fd(evs->tp);
}

static void vs_backend_evs_free(vs_backend_t *be)
{
     vs_backend_evs_t *evs = (vs_backend_evs_t*) be;
     if (evs) {
	  transport_free(evs->tp);
	  free(evs->url);
	  free(evs);
     }
}
vs_backend_t *vs_backend_evs_new(const char *url, void *user_context,
				 void (*send_cb)(void *, const vs_msg_t *))
{
     vs_backend_evs_t *be;
     
     be = malloc(sizeof(vs_backend_evs_t));
     be->tp = transport_new(TRANSPORT_TCP);
     be->url = strdup(url);
     be->recv_cb = NULL;
     be->user_context = NULL;
     be->view = NULL;
     be->self_addr = ADDR_INVALID;

     be->be.free = &vs_backend_evs_free;
     be->be.connect = &vs_backend_evs_connect;
     be->be.disconnect = &vs_backend_evs_disconnect;
     be->be.send = &vs_backend_evs_send;
     be->be.sched = &vs_backend_evs_sched;
     be->be.set_recv_callback = &vs_backend_evs_set_recv_callback;
     be->be.get_self_addr = &vs_backend_evs_get_self_addr;
     be->be.get_fd = &vs_backend_evs_get_fd;

     return (vs_backend_t *)be;
}

