
#include "vs_backend.h"
#include "vs_backend_shm.h"
#include <assert.h>


vs_backend_t *vs_backend_new(const char *conf,
			     poll_t *poll,
			     protolay_t *up_ctx,
			     void (*pass_up_cb)(protolay_t *,
						const readbuf_t *,
						const size_t,
						const up_meta_t *))
{
    vs_backend_t *be = NULL;
    if (strncmp(conf, "shm:", 4) == 0)
	be = vs_backend_shm_new(conf + 4, poll, up_ctx, pass_up_cb);

    if (be) {
	assert(be->connect != NULL);
	assert(be->close != NULL);
	assert(be->free != NULL);
	assert(be->get_self_addr != NULL);
    }
    return be;
}

void vs_backend_free(vs_backend_t *be)
{
    if (be != NULL)
	be->free(be);
}

int vs_backend_connect(vs_backend_t *be, const group_id_t group)
{
    assert(be != NULL);
    assert(be->connect != NULL);
    return be->connect(be, group);
}

void vs_backend_close(vs_backend_t *be)
{
    assert(be != NULL);
    assert(be->close != NULL);
    be->close(be);
}

addr_t vs_backend_get_self_addr(const vs_backend_t *be)
{
    assert(be != NULL);
    assert(be->get_self_addr != NULL);
    return be->get_self_addr(be);
}

#if 0
vs_backend_t *vs_backend_new(const char *conf, 
			     void *user_context, 
			     void (*send_cb)(void *, const vs_msg_t *))
{
     if (strncmp(conf, "shm:", 4) == 0)
	  return vs_backend_shm_new(conf + 4, user_context, send_cb);
     if (strncmp(conf, "evs:", 4) == 0)
	  return vs_backend_evs_new(conf + 4, user_context, send_cb);
     return NULL;
}

void vs_backend_free(vs_backend_t *be)
{
     assert(be);
     assert(be->free);
     be->free(be);
}

void vs_backend_set_recv_cb(vs_backend_t *be, void *user_context, 
			    void (*fn)(void *, const vs_msg_t *))
{
     assert(be);
     assert(be->set_recv_callback);
     be->set_recv_callback(be, user_context, fn);
}

int vs_backend_connect(vs_backend_t *be, const group_id_t group)
{
     assert(be);
     assert(be->connect);
     return be->connect(be, group);
}

addr_t vs_backend_get_self_addr(const vs_backend_t *be)
{
     assert(be);
     assert(be->get_self_addr);
     return be->get_self_addr(be);
}

vs_view_id_t vs_backend_get_view_id(const vs_backend_t *be)
{
     assert(be);
     assert(be->get_view_id);
     return be->get_view_id(be);
}

void vs_backend_disconnect(vs_backend_t *be)
{
     assert(be);
     assert(be->disconnect);
     be->disconnect(be);
}

int vs_backend_send(vs_backend_t *be, const vs_msg_t *msg)
{
     assert(be);
     assert(be->send);
     return be->send(be, msg);
}


/*
 * Now becomes the intresting part: How to decouple process
 * workflow from protocol stack?
 *
 * Conventions: 
 * If backend is connected via socket, vs_backend_fd() returns the 
 * file descriptor corresponding to socket that must (should?) be passed to 
 * vs_backend_sched(). 
 */
int vs_backend_sched(vs_backend_t *be, poll_t *poll, int fd)
{
     assert(be);
     assert(be->sched);
     return be->sched(be, poll, fd);
}

int vs_backend_get_fd(const vs_backend_t *be)
{
     assert(be);
     assert(be->get_fd);
     return be->get_fd(be);
}

#endif /* 0 */
