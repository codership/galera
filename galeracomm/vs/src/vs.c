#include "gcomm/vs.h"
#include "vs_backend.h"
#include <glib.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>



typedef struct vs_memb_ {
    addr_t addr;
    seq_t recv_seq;
    vs_msg_t *state_msg;
} vs_memb_t;

typedef struct vs_memb_set_ vs_memb_set_t;
typedef struct vs_memb_set_iter_ vs_memb_set_iter_t;

struct vs_ {
    protolay_t *pl;
    seq_t send_seq;
    vs_backend_t *be;
    void *user_context;
    void (*recv_cb)(void *, const vs_msg_t *);
    vs_view_t *view;
    vs_view_t *trans_view;
    vs_view_t *proposed_view;
    GList *trans_queue;
    vs_memb_set_t *memb;
};


static void print_view(FILE *fp, const vs_view_t *view)
{
    const addr_set_iter_t *i;
    fprintf(fp, "type = %s id = %i memb = ", 
	    vs_view_get_type(view) == VS_VIEW_TRANS ? "TRANS" : "REG", 
	    vs_view_get_id(view));
    for (i = addr_set_first(vs_view_get_addr(view)); i; 
	 i = addr_set_next(i))
	fprintf(fp, "%i ", addr_cast(i));
    fprintf(fp, "\n");
}

/*******************************************************************
 * Memb set
 *******************************************************************/

static int memb_cmp(const void *a, const void *b)
{
    if (((vs_memb_t*)a)->addr < ((vs_memb_t*)b)->addr)
	return -1;
    if (((vs_memb_t*)a)->addr > ((vs_memb_t*)b)->addr)
	return -1;
    return 0;
}

static int memb_data_cmp(const void *a, const void *b, void *data)
{
    return memb_cmp(a, b);
}


static vs_memb_t *vs_memb_new(const addr_t addr)
{
    vs_memb_t *ret;
    ret = g_malloc(sizeof(vs_memb_t));
    ret->addr = addr;
    ret->recv_seq = 0;
    ret->state_msg = NULL;
    return ret;
}

static void vs_memb_free(vs_memb_t *memb)
{
    if (memb) {
	vs_msg_free(memb->state_msg);
	g_free(memb);
    }
}

static vs_memb_set_t *vs_memb_set_new(void)
{
    return (vs_memb_set_t *) g_queue_new();
}

static void vs_memb_set_free(vs_memb_set_t *set)
{
    GList *i;
    if (set) {
	for (i = g_queue_peek_head_link((GQueue *)set); i; 
	     i = g_list_next(i))
	    vs_memb_free(i->data);
	g_queue_free((GQueue*) set);
    }
}

static vs_memb_set_iter_t *vs_memb_set_find(const vs_memb_set_t *set, const vs_memb_t *memb)
{

    return (vs_memb_set_iter_t *) 
	g_queue_find_custom((GQueue *) set, (gpointer)memb, &memb_cmp);
}

static vs_memb_set_iter_t *vs_memb_set_insert(vs_memb_set_t *set, vs_memb_t *memb)
{
    if (vs_memb_set_find(set, memb))
	return NULL;
    g_queue_insert_sorted((GQueue*)set, memb, &memb_data_cmp, NULL);
    return vs_memb_set_find(set, memb);
}


static void vs_memb_set_erase(vs_memb_set_t *set, vs_memb_t *memb)
{
    vs_memb_set_iter_t *i;
     
    if ((i = vs_memb_set_find(set, memb)))
	g_queue_delete_link((GQueue *)set, (GList *)i);     
}

static vs_memb_t *vs_memb_cast(const vs_memb_set_iter_t *i)
{
    return (vs_memb_t *) ((GList *) i)->data;
}

static vs_memb_t *vs_memb_set_find_by_addr(const vs_memb_set_t *set, const addr_t addr)
{
    vs_memb_t *ret = NULL;
    vs_memb_t dummy;
    vs_memb_set_iter_t *i;
    dummy.addr = addr;
    dummy.recv_seq = 0;
     
    i = vs_memb_set_find(set, &dummy);
    if (i) {
	ret = vs_memb_cast(i);
    }
    return ret;
}

#ifdef VS_UNUSED
static size_t vs_memb_set_size(const vs_memb_set_t *set)
{
    return g_queue_get_length((GQueue*)set);
}
#endif

static const vs_memb_set_iter_t *vs_memb_set_first(const vs_memb_set_t *set)
{
    return set ? (const vs_memb_set_iter_t *)g_queue_peek_head_link((GQueue*)set) : NULL;
}

static const vs_memb_set_iter_t *vs_memb_set_next(const vs_memb_set_iter_t *i)
{
    return i ? (const vs_memb_set_iter_t *)g_list_next((GList*) i) : NULL;
}

/*******************************************************************
 * VS state
 *******************************************************************/ 


addr_t vs_get_self_addr(const vs_t *vs)
{
    g_assert(vs);
    g_assert(vs->be);
    return vs_backend_get_self_addr(vs->be);
}



void vs_free(vs_t *vs)
{
    if (vs) {
	vs_backend_free(vs->be);
	vs_view_free(vs->view);
	vs_view_free(vs->trans_view);
	vs_view_free(vs->proposed_view);
	vs_memb_set_free(vs->memb);
	g_list_free(vs->trans_queue);
	g_free(vs);
    }
}

int vs_open(vs_t *vs, const group_id_t group)
{
    int ret;
    if (!(vs && vs->be))
	return EINVAL;
     
    if ((ret = vs_backend_connect(vs->be, group))) {
	vs_backend_free(vs->be);
	vs->be = NULL;
	return ret;
    }
    return 0;
}

void vs_close(vs_t *vs)
{
    vs_backend_close(vs->be);
}


#if 0
int vs_send(vs_t *vs, msg_t *msgbuf, const vs_msg_safety_e safety)
{
    int ret;
    vs_memb_t *self;
    vs_msg_t *msg;
     
    if (!vs->view || vs->trans_view)
	return EAGAIN;
    self = vs_memb_set_find_by_addr(vs->memb, vs_backend_get_self_addr(vs->be));
    /* Poor man's flow control... count bytes as well */
    if (self->recv_seq + 64 < vs->send_seq)
	return EAGAIN;
     
    msg = vs_msg_new(VS_MSG_DATA, 
		     vs_backend_get_self_addr(vs->be),
		     vs_view_get_id(vs->view), 
		     vs->send_seq + 1, 
		     safety, 
		     msgbuf, NULL);
    ret = vs_backend_send(vs->be, msg);
    if (ret == 0)
	vs->send_seq++;
    vs_msg_free(msg);
    /* TODO: Log error */
    return ret;
}
#endif /* 0 */

static void vs_deliver_data(vs_t *vs, const readbuf_t *rb, const vs_msg_t *msg)
{
    vs_memb_t *memb;
    memb = vs_memb_set_find_by_addr(vs->memb, vs_msg_get_source(msg));
    assert(memb);
    assert(memb->recv_seq + 1 == vs_msg_get_seq(msg));
    assert(vs->view && vs_view_find_addr(vs->view, vs_msg_get_source(msg)));
    memb->recv_seq = vs_msg_get_seq(msg);
    protolay_pass_up(vs->pl, rb, 
		     vs_msg_get_read_offset(msg) + vs_msg_get_hdrlen(msg), 
		     NULL);
}

static void vs_deliver_view(vs_t *vs, const vs_view_t *view)
{
    char *msgbuf;
    size_t msgbuflen;
    readbuf_t *rb;
    vs_msg_t *view_msg;
    
    assert(view);

    fprintf(stderr, "VS(%i) deliver view: ", vs_backend_get_self_addr(vs->be));
    print_view(stderr, view);
    
    view_msg = vs_msg_new(vs_view_get_type(view) == VS_VIEW_TRANS ? 
			  VS_MSG_TRANS_CONF : VS_MSG_REG_CONF, 0, 0, 0,
			  VS_MSG_SAFETY_SAFE);
    msgbuflen = vs_msg_get_hdrlen(view_msg) + vs_view_get_len(view);
    msgbuf = g_malloc(msgbuflen);
    memcpy(msgbuf, vs_msg_get_hdr(view_msg), vs_msg_get_hdrlen(view_msg));
    if (!vs_view_write(view, msgbuf, msgbuflen, vs_msg_get_hdrlen(view_msg))) {
	abort();
    }
    rb = readbuf_new(msgbuf, msgbuflen);
    protolay_pass_up(vs->pl, rb, 0, NULL);
    vs_msg_free(view_msg);
    readbuf_free(rb);
    free(msgbuf);
}


static void vs_memb_states_clean(vs_t *vs) 
{
    const vs_memb_set_iter_t *i;
    for (i = vs_memb_set_first(vs->memb); i; i = vs_memb_set_next(i)) {
	vs_msg_free(vs_memb_cast(i)->state_msg);
	vs_memb_cast(i)->state_msg = NULL;
    }
}

static void vs_memb_states_reset(vs_t *vs) 
{
    vs_memb_t *memb;
    const vs_memb_set_iter_t *i;
    for (i = vs_memb_set_first(vs->memb); i; i = vs_memb_set_next(i)) {
	memb = vs_memb_cast(i);
	memb->recv_seq = vs_msg_get_seq(memb->state_msg);
    }
}

static int vs_send_state(vs_t *vs)
{
    int ret;
    vs_msg_t *msg;
    writebuf_t *wb;
    char *view_buf;
    size_t view_buf_len;
    
    fprintf(stderr, "VS(%i) send state: ", vs_backend_get_self_addr(vs->be));
    print_view(stderr, vs->proposed_view);
    view_buf_len = vs_view_get_len(vs->proposed_view);
    view_buf = malloc(view_buf_len);
    
    if (!vs_view_write(vs->proposed_view, view_buf, view_buf_len, 0)) {
	abort();
	return EPROTO;
    }
    
    msg = vs_msg_new(VS_MSG_STATE, 
		     vs_backend_get_self_addr(vs->be), 
		     vs_view_get_id(vs->proposed_view),
		     vs->send_seq,
		     VS_MSG_SAFETY_SAFE);
    wb = writebuf_new(view_buf, view_buf_len);
    writebuf_prepend_hdr(wb, vs_msg_get_hdr(msg), vs_msg_get_hdrlen(msg));
    
    ret = protolay_pass_down(vs->pl, wb, NULL);

    vs_msg_free(msg);
    writebuf_free(wb);
    free(view_buf);
    
    return ret;
}


static int vs_handle_view_change(vs_t *vs, const readbuf_t *rb, 
				 const vs_msg_t *msg)
{
    int ret = 0;
    vs_view_t *view;
    vs_view_t *reg_view;
    vs_view_t *trans_view;
    vs_view_t *curr_view;
    addr_set_t *addr_set;
    const addr_set_iter_t *i;

    if (!vs_view_read(vs_msg_get_payload(msg), 
		      vs_msg_get_payload_len(msg), 0, &view)) {
	g_assert(0);
	return EPROTO;
    }
     
    fprintf(stderr, "VS(%i) view change: ", vs_backend_get_self_addr(vs->be));
    print_view(stderr, view);
    
    if (vs->view == NULL && vs->trans_view == NULL) {
	g_assert(vs_view_get_type(view) == VS_VIEW_TRANS);
	/* No views or attempts yet */
	vs->trans_view = vs_view_copy(view);
	/* */
	g_assert(vs->proposed_view == NULL);
    } else if (vs_view_get_type(view) == VS_VIEW_TRANS) {
	/* Invalidate proposed view */
	vs_view_free(vs->proposed_view);
	vs->proposed_view = NULL;
	vs_memb_states_clean(vs);
	/* Current view is always transitional if regular view 
	 * has not been reached */
	curr_view = vs->trans_view ? vs->trans_view : vs->view;
	  
	trans_view = vs_view_new(VS_VIEW_TRANS, vs->view ? vs_view_get_id(vs->view) : 0);
	  
	/* Generate intersection of previous view addr and current addr 
	 * and set it to trans view */
	if (!(addr_set = addr_set_intersection(vs_view_get_addr(curr_view),
					       vs_view_get_addr(view)))) {
	    g_assert(0);
	    return ENOMEM; 
	}

	/* Validate transitional configuration */
	if (!addr_set_is_subset(addr_set, vs_view_get_addr(curr_view))) {
	    g_assert(0);
	    return EPROTO;
	}
	  
	vs_view_free(vs->trans_view);
	vs->trans_view = trans_view;
	for (i = addr_set_first(addr_set); i; i = addr_set_next(i)) {
	    if (!vs_view_addr_insert(vs->trans_view, addr_cast(i))) {
		g_assert(0);
		return EPROTO;
	    }
	}
	addr_set_free(addr_set);
	  
	/* Validate transitional configuration */
	if (!addr_set_is_subset(vs_view_get_addr(vs->trans_view), 
				vs_view_get_addr(view))) {
	    g_assert(0);
	    return EPROTO;
	}

	/* Validate transitional configuration */
	if (vs->view && !addr_set_is_subset(vs_view_get_addr(vs->trans_view), 
					    vs_view_get_addr(vs->view))) {
	    g_assert(0);
	    return EPROTO;
	}
    } else {
	if (!vs->trans_view) {
	    g_assert(0);
	    return EPROTO;
	}
	  
	reg_view = vs_view_new(VS_VIEW_REG, vs_view_get_id(view));
	g_assert(vs->proposed_view == NULL);
	vs->proposed_view = reg_view;

	for (i = addr_set_first(vs_view_get_addr(view)); i; 
	     i = addr_set_next(i)) 
	    if (!vs_view_addr_insert(vs->proposed_view, addr_cast(i))) {
		g_assert(0);
		return EPROTO;
	    }
	ret = vs_send_state(vs);
    }
    vs_view_free(view);
    return ret;
}

static bool vs_state_msg_validate(const vs_t *vs, const vs_memb_t *memb, 
				  const vs_msg_t *msg)
{
    vs_view_t *view = NULL;
     
    if (!vs_view_read(vs_msg_get_payload(msg),
		      vs_msg_get_payload_len(msg), 0, &view)) {
	fprintf(stderr, "Corrupt message?\n");
	goto fail;
    }
    if (vs_view_get_id(view) != vs_view_get_id(vs->proposed_view)) {
	fprintf(stderr, "Invalid view id\n");
	goto fail;
    }

    if (!addr_set_equal(vs_view_get_addr(view), 
			vs_view_get_addr(vs->proposed_view))) {
	fprintf(stderr, "Addr sets not equal\n");
	goto fail;
    }
    vs_view_free(view);
    return true;
fail:
    vs_view_free(view);
    return false;
}

static int vs_handle_state(vs_t *vs, const readbuf_t *rb, const vs_msg_t *msg)
{
    vs_memb_t *memb;
    const vs_memb_set_iter_t *i, *i_next;
    GList *trans_msg;
    size_t n_state_msgs;
    vs_view_t *view;

    if (!vs->proposed_view ||
	vs_view_get_id(vs->proposed_view) != vs_msg_get_source_view_id(msg)) {
	if (vs->trans_view) {
	    fprintf(stderr, "Drop state message");
	    return 0;
	} else {
	    g_assert(0);
	    return EPROTO;
	}
    }
     
    memb = vs_memb_set_find_by_addr(vs->memb, vs_msg_get_source(msg));
    if (!memb) {
	memb = vs_memb_new(vs_msg_get_source(msg));
	vs_memb_set_insert(vs->memb, memb);
    }
     
     
    if (memb->state_msg) {
	fprintf(stderr, "Memb %i state msg %p\n",  memb->addr, 
		(void*)memb->state_msg);
	g_assert(0);
	return EPROTO;
    }

    if (!vs_state_msg_validate(vs, memb, msg)) {
	g_assert(0);
	return EPROTO;
    }

    if (!vs_view_read(vs_msg_get_payload(msg),
		      vs_msg_get_payload_len(msg), 0, &view)) {
	g_assert(0);
	return EPROTO;
    }

    fprintf(stderr, "VS(%i) handle state from %i: ", 
	    vs_backend_get_self_addr(vs->be), memb->addr);
    print_view(stderr, view);

    memb->state_msg = vs_msg_copy(msg);
     
    for (n_state_msgs = 0, i = vs_memb_set_first(vs->memb); i; 
	 i = vs_memb_set_next(i)) {
	if (vs_memb_cast(i)->state_msg) {
	    n_state_msgs++;
	}
    }
     
    if (n_state_msgs == addr_set_size(vs_view_get_addr(vs->proposed_view))) {
	g_assert(vs->trans_queue == NULL || vs->view);
	  
	vs_deliver_view(vs, vs->trans_view);
	  
	for (trans_msg = g_list_first(vs->trans_queue); trans_msg; 
	     trans_msg = g_list_next(trans_msg)) {
	    g_assert(vs_view_get_id(vs->trans_view) == 
		     vs_msg_get_source_view_id(trans_msg->data));
	    vs_deliver_data(vs, vs_msg_get_readbuf(trans_msg->data),
			    trans_msg->data);
	    vs_msg_free(trans_msg->data);
	}
	  
	g_list_free(vs->trans_queue);
	vs->trans_queue = NULL;
	  
	vs_view_free(vs->trans_view);
	vs->trans_view = NULL;
	  
	vs_view_free(vs->view);
	for (i = vs_memb_set_first(vs->memb); i; i = i_next) {
	    i_next = vs_memb_set_next(i);
	    if (!vs_view_find_addr(vs->proposed_view, 
				   vs_memb_cast(i)->addr))
		vs_memb_set_erase(vs->memb, vs_memb_cast(i));
	}
	  
	vs->view = vs->proposed_view;
	vs->proposed_view = NULL;
	  
	vs_deliver_view(vs, vs->view);
	  
	/* FIXME: This does not cover case when group partitions 
	 * and remerges between previous reg view delivery and 
	 * this point. It should be covered somehow... */
	  
	/* Reset reiceve seqno's etc... */
	vs_memb_states_reset(vs);
	/* Clean up state messages */
	vs_memb_states_clean(vs);
    }
     
    vs_view_free(view);
     
    return 0;
}

static int vs_handle_data(vs_t *vs, const readbuf_t *rb, const vs_msg_t *msg)
{
    int ret;
    vs_msg_t *msg_copy;
     
    if (vs->view && !vs_view_find_addr(vs->view, vs_msg_get_source(msg))) {
	if (vs->trans_view) {
	    /* This may happed after backend split/merge, 
	     * messages originated from previously split view 
	     * are delivered in merged reg view before state 
	     * messages. See also comment below... */
	    return 0;
	} else {
	    g_assert(0);
	    return EPROTO;
	}
    } else if (!vs->view) {
	/* Note: This is valid case if underlying protocol is EVS. 
	 * Some messages may be delivered in reg view before state 
	 * messages. Those messages were sent moment before 
	 * view change and didn't get past EVS send queue before
	 * view change, so were sent in new reg view. 
	 *
	 * I made this note because I was constantly making assumption
	 * that without view you don't receive data messages...
	 */
	return 0; /* Silently drop */
    }
     
    if (vs->trans_view) {
	g_assert(vs_msg_get_source_view_id(msg) == 
		 vs_view_get_id(vs->trans_view));
	msg_copy = vs_msg_copy(msg);
	vs->trans_queue = g_list_append(vs->trans_queue, msg_copy);
	ret = 0;
    } else {
	g_assert(vs_msg_get_source_view_id(msg) == vs_view_get_id(vs->view));
	vs_deliver_data(vs, rb, msg);
	ret = 0;
    }
    return ret;
}


static void vs_pass_up_cb(protolay_t *pl, const readbuf_t *rb, 
			  const size_t roff, const up_meta_t *up_meta)
{
    vs_msg_t *vs_msg;
    vs_t *vs;

    vs = protolay_get_priv(pl);
    vs_msg = vs_msg_read(rb, roff);
    
    switch (vs_msg_get_type(vs_msg)) {
    case VS_MSG_TRANS_CONF:
    case VS_MSG_REG_CONF:
	if (vs_handle_view_change(vs, rb, vs_msg))
	    protolay_fail(pl);
	break;
    case VS_MSG_STATE:
	if (vs_handle_state(vs, rb, vs_msg))
	    protolay_fail(pl);
	break;
    case VS_MSG_DATA:
	if (vs_handle_data(vs, rb, vs_msg))
	    protolay_fail(pl);
	break;
    default:
	protolay_fail(pl);
    }
}

static int vs_pass_down_cb(protolay_t *pl, writebuf_t *wb, 
			   const down_meta_t *down_meta)
{
    int ret;
    vs_memb_t *self;
    vs_msg_t *msg;
    vs_t *vs;

    vs = protolay_get_priv(pl);
    if (!vs->view || vs->trans_view)
	return EAGAIN;

    self = vs_memb_set_find_by_addr(vs->memb, vs_backend_get_self_addr(vs->be));

    /* Poor man's flow control... should count bytes as well */
    if (self->recv_seq + 64 < vs->send_seq)
	return EAGAIN;
    
    msg = vs_msg_new(VS_MSG_DATA, 
		     vs_backend_get_self_addr(vs->be),
		     vs_view_get_id(vs->view), 
		     vs->send_seq + 1, 
		     VS_MSG_SAFETY_SAFE);
    writebuf_prepend_hdr(wb, vs_msg_get_hdr(msg), vs_msg_get_hdrlen(msg));
    ret = protolay_pass_down(pl, wb, NULL);
    writebuf_rollback_hdr(wb, vs_msg_get_hdrlen(msg));
    if (ret == 0)
	vs->send_seq++;
    vs_msg_free(msg);
    /* TODO: Log error */
    return ret;  
}

vs_t *vs_new(const char *backend_url, poll_t *poll,
	     protolay_t *up_ctx,
	     void (*pass_up_cb)(protolay_t *, 
				const readbuf_t *,
				const size_t,
				const up_meta_t *))
{
    vs_t *vs;
    if (!(vs = g_malloc(sizeof(vs_t)))) {
	return NULL;
    }
    vs->pl = protolay_new(vs, NULL);
    protolay_set_up(vs->pl, up_ctx, pass_up_cb);
    protolay_set_down(up_ctx, vs->pl, &vs_pass_down_cb);
    vs->send_seq = 0;
    vs->user_context = NULL;
    vs->recv_cb = NULL;
    vs->view = NULL;
    vs->trans_view = NULL;
    vs->proposed_view = NULL;
    vs->memb = vs_memb_set_new();
    vs->trans_queue = NULL;
    vs->be = vs_backend_new(backend_url, poll, vs->pl, &vs_pass_up_cb);
    return vs;
}
