
#include "vs_backend_shm.h"
#include "galeracomm/vs_view.h"
#include "galeracomm/vs_msg.h"
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>

typedef struct shm_be_ {
    int refcnt;
    addr_t last_addr;
    vs_view_id_t view_id;
    GList *views;
    GQueue *msg_queue;
    GQueue *conf_queue;
    GList  *memb;
} shm_be_t;



typedef struct shm_be_msg_ {
    void *buf;
    size_t buflen;
} shm_be_msg_t;


typedef enum {
    CLOSED,
    CONNECTING,
    CONNECTED,
    CLOSING
} shm_be_memb_state_e;

typedef struct shm_be_memb_ {
    addr_t addr;
    shm_be_memb_state_e state;
} shm_be_memb_t;



typedef struct shm_be_view_ {
    vs_view_id_t view_id;
    vs_view_t *reg_view;
    vs_view_t *trans_view;
} shm_be_view_t;

static shm_be_t *shm_be = NULL;



static void print_view(FILE *fp, const vs_view_t *view)
{
    const addr_set_iter_t *i;
    fprintf(fp, "type = %s id = %i memb = ", 
	    vs_view_get_type(view) == VS_VIEW_TRANS ? "TRANS" : "REG", 
	    vs_view_get_id(view));
    for (i = addr_set_first(vs_view_get_addr(view)); i; 
	 i = addr_set_next(i))
	fprintf(fp, "%i ", addr_cast(i));
}


static shm_be_view_t *shm_be_view_new(const vs_view_id_t view_id)
{
    shm_be_view_t *view;
    
    view = malloc(sizeof(shm_be_view_t));
    view->view_id = view_id;
    view->reg_view = NULL;
    view->trans_view = NULL;
    return view;
}

static void shm_be_view_free(shm_be_view_t *view)
{
    if (view) {
	vs_view_free(view->reg_view);
	vs_view_free(view->trans_view);
	free(view);
    }
}

static shm_be_view_t *shm_be_get_preceeding_be_view(const vs_view_t *view)
{
    shm_be_view_t *ret = NULL;
    shm_be_view_t *view_data;
    GList *i;
    
    for (i = g_list_last(shm_be->views); i && ret == NULL; 
	 i = g_list_previous(i)) {
	view_data = i->data;
	if (vs_view_get_type(view) == VS_VIEW_TRANS &&
	    vs_view_get_id(view) == vs_view_get_id(view_data->reg_view)) {
	    assert(vs_view_get_type(view_data->reg_view) == VS_VIEW_REG);
	    assert(addr_set_is_subset(
			 vs_view_get_addr(view), 
			 vs_view_get_addr(view_data->reg_view)));
	    ret = view_data;
	} else if (vs_view_get_type(view) == VS_VIEW_REG && 
		   view_data->trans_view) {
	    assert(vs_view_get_type(view_data->trans_view) == VS_VIEW_TRANS);
	    
	    if (addr_set_is_subset(vs_view_get_addr(view_data->trans_view),
				   vs_view_get_addr(view)) &&
		vs_view_get_size(view_data->trans_view) > 0) {
		ret = view_data;
	    }
	}
    }
    return ret;
}

static shm_be_view_t *shm_be_get_current_be_view_by_addr(const addr_t addr)
{
    GList *i;
    shm_be_view_t *view;
    shm_be_view_t *ret = NULL;

    for (i = g_list_last(shm_be->views); i && ret == NULL; 
	 i = g_list_previous(i)) {
	view = i->data;
	if (view->trans_view && vs_view_find_addr(view->trans_view, addr)) {
	    ret = view;
	}
    }
    for (i = g_list_last(shm_be->views); i; 
	 i = g_list_previous(i)) {
	view = i->data;
	if (vs_view_find_addr(view->reg_view, addr)) {
	    if (!ret || vs_view_get_id(ret->reg_view) < vs_view_get_id(view->reg_view))
		ret = view;
	}
    }     
    
    return ret;
}

static vs_msg_t *shm_be_get_last_view_msg_by_addr(const addr_t addr)
{
    vs_view_t *view;
    readbuf_t *msgbuf;
    vs_msg_t *msg;
    shm_be_msg_t *shm_msg;
    GList *i;
    vs_msg_t *ret = NULL;

    for (i = g_queue_peek_tail_link(shm_be->conf_queue); i && ret == NULL; 
	 i = g_list_previous(i)) {
	shm_msg = i->data;
	msgbuf = readbuf_new(shm_msg->buf, shm_msg->buflen);
	msg = vs_msg_read(msgbuf, 0);
	
	if (vs_msg_get_type(msg) == VS_MSG_REG_CONF) {
	    if (!vs_view_read(vs_msg_get_payload(msg), 
			      vs_msg_get_payload_len(msg), 0, &view)) {
		g_assert(!"Corrupted message?");
	    }
	    if (vs_view_find_addr(view, addr))
		ret = vs_msg_copy(msg);
	    vs_view_free(view);
	}
	vs_msg_free(msg);
	readbuf_free(msgbuf);
    }
    return ret;
}

static vs_view_t *shm_be_get_last_view_by_addr(const addr_t addr)
{
    vs_msg_t *view_msg;
    shm_be_view_t *be_view;
    vs_view_t *view;
    vs_view_t *ret = NULL;
    if ((view_msg = shm_be_get_last_view_msg_by_addr(addr))) {
	if (!vs_view_read(vs_msg_get_payload(view_msg),
			  vs_msg_get_payload_len(view_msg), 
			  0, &view)) {
	    g_assert(!"View read failed");
	}
	g_assert(vs_view_find_addr(view, addr));
	g_assert(vs_view_get_type(view) == VS_VIEW_REG);
	vs_msg_free(view_msg);
	ret = view;
    } else if ((be_view = shm_be_get_current_be_view_by_addr(addr))) {
	g_assert(be_view->reg_view);
	g_assert(vs_view_find_addr(be_view->reg_view, addr));
	g_assert(vs_view_get_type(be_view->reg_view) == VS_VIEW_REG);
	ret = vs_view_copy(be_view->reg_view);
    } 
    return ret;
}



static void shm_be_queue_conf(const shm_be_memb_t *memb, const vs_view_t *view)
{
    shm_be_msg_t *msg;
    char *msgbuf;
    size_t msgbuflen;
    vs_msg_t *vs_msg;

    vs_msg = vs_msg_new(vs_view_get_type(view) == VS_VIEW_TRANS ? 
			VS_MSG_TRANS_CONF : 
			VS_MSG_REG_CONF, 
			memb->addr, 0, 0, 
			VS_MSG_SAFETY_SAFE);
    
    msgbuflen = vs_msg_get_hdrlen(vs_msg) + vs_view_get_len(view);
    msgbuf = malloc(msgbuflen);
    
    memcpy(msgbuf, vs_msg_get_hdr(vs_msg), vs_msg_get_hdrlen(vs_msg));
    if (!vs_view_write(view, msgbuf, msgbuflen, vs_msg_get_hdrlen(vs_msg))) {
	fprintf(stderr, "Fatal programming error\n");
	abort();
    }
    
    msg = malloc(sizeof(shm_be_msg_t));
    msg->buf = msgbuf;
    msg->buflen = msgbuflen;
    g_queue_push_tail(shm_be->conf_queue, msg);
    vs_msg_free(vs_msg);
}


static int view_cmp(const void *a, const void *b)
{
    const vs_view_t *va = (const vs_view_t *)a;
    const vs_view_t *vb = (const vs_view_t *)b;
     
    if (vs_view_get_id(va) < vs_view_get_id(vb))
	return -1;
    else if (vs_view_get_id(va) > vs_view_get_id(vb))
	return 1;
     
    g_assert(addr_set_equal(vs_view_get_addr(va), 
			    vs_view_get_addr(vb)));
    return 0;
}

void vs_backend_shm_split()
{
    GList *i, *j;
    GList *new_views = NULL;
    GList *old_views = NULL;
    GList *split_views = NULL;
    int n_new_views;
    int n_old_views;
    int n, split, s, rn;
    addr_set_t *targets, *tmp_set, *set_intr;
    addr_t addr;
    vs_backend_t *be;
    shm_be_memb_t *memb;
    const addr_set_iter_t *addr_i;
    vs_view_t *view, *split_view1, *split_view2;
    size_t view_size;


    fprintf(stderr, "Split!!!\n");
     
    targets = addr_set_new();
     
    for (i = g_list_first(shm_be->memb); i; i = g_list_next(i)) {
	be = i->data;
	memb = be->priv;
	/* Others will be gone at the moment of delivery */
	if (memb->state == CONNECTING || memb->state == CONNECTED)
	    addr_set_insert(targets, memb->addr);
    }
     
    for (addr_i = addr_set_first(targets); addr_i; 
	 addr_i = addr_set_next(addr_i)) {
	if (!(view = shm_be_get_last_view_by_addr(addr_cast(addr_i)))) {
	    g_assert(!"Not possible");
	}
	if (!g_list_find_custom(old_views, view, &view_cmp))
	    old_views = g_list_insert_sorted(old_views, view, &view_cmp);
	else
	    vs_view_free(view);
    }
     
    fprintf(stderr, "Old views:\n");
    for (i = g_list_first(old_views); i; i = g_list_next(i)) {
	print_view(stderr, i->data);
	fprintf(stderr, "\n");
    }

    /* Now we should have complete set of future views... 
     * TODO: Verify that union of views covers addr_set */
     
    n_old_views = g_list_length(old_views);
    n_new_views = n_old_views + 1 + rand()%3;
     
    for (n = n_old_views; n < n_new_views && old_views; n++) {
	i = g_list_nth(old_views, rand()%g_list_length(old_views));
	view = i->data;
	tmp_set = addr_set_copy(vs_view_get_addr(view));
	if ((view_size = addr_set_size(tmp_set)) > 2) {
	    old_views = g_list_remove(old_views, view);
	    split = 1 + rand()%(view_size - 1);
	    split_view1 = vs_view_new(VS_VIEW_REG, vs_view_get_id(view));
	    split_view2 = vs_view_new(VS_VIEW_REG, vs_view_get_id(view));
	    fprintf(stderr, "split = %i\n", split);
	    for (s = view_size; s > split; s--) {
		rn = rand()%addr_set_size(tmp_set);
		addr = addr_cast(addr_set_nth(tmp_set, rn));
		vs_view_addr_insert(split_view1, addr);
		addr_set_erase(tmp_set, addr);
	    }
	    for (addr_i = addr_set_first(tmp_set); addr_i; 
		 addr_i = addr_set_next(addr_i))
		vs_view_addr_insert(split_view2, addr_cast(addr_i));
	       
	    new_views = g_list_append(new_views, split_view1);
	    new_views = g_list_append(new_views, split_view2);
	    split_views = g_list_append(split_views, view);
	} 
	addr_set_free(tmp_set);
    }
     

    fprintf(stderr, "Split views:\n");
    for (i = g_list_first(split_views); i; i = g_list_next(i)) {
	print_view(stderr, i->data);
	fprintf(stderr, "\n");
	for (addr_i = addr_set_first(vs_view_get_addr(i->data));
	     addr_i; addr_i = addr_set_next(addr_i)) {
	    for (j = g_list_first(new_views); j; j = g_list_next(j)) {
		if (vs_view_find_addr(j->data, addr_cast(addr_i)))
		    break;
	    }
	    g_assert(j);
	}
    }

    fprintf(stderr, "New views:\n");
     
    for (i = g_list_first(new_views); i; i = g_list_next(i)) {
	print_view(stderr, i->data);
	fprintf(stderr, "\n");
	g_assert(vs_view_get_size(i->data) > 0);
	for (j = i->next; j; j = g_list_next(j)) {
	    set_intr = addr_set_intersection(vs_view_get_addr(i->data), 
					     vs_view_get_addr(j->data));
	    g_assert(addr_set_size(set_intr) == 0);
	    addr_set_free(set_intr);
	}
    }
     
    i = g_list_first(shm_be->memb);
    memb = i ? ((vs_backend_t *)i->data)->priv : NULL;
    g_assert(memb || (!new_views && !split_views && !old_views));

    for (i = g_list_first(new_views); i; i = g_list_next(i)) {
	for (j = g_list_first(split_views); j; j = g_list_next(j)) {
	    if (addr_set_is_subset(vs_view_get_addr(i->data), 
				   vs_view_get_addr(j->data)))
		break;
	}
	g_assert(j);
	view = vs_view_new(VS_VIEW_TRANS, vs_view_get_id(j->data));
	for (addr_i = addr_set_first(vs_view_get_addr(i->data)); addr_i;
	     addr_i = addr_set_next(addr_i)) {
	    vs_view_addr_insert(view, addr_cast(addr_i));
	}
	shm_be_queue_conf(memb, view);
	vs_view_free(view);
    }

    for (i = g_list_first(new_views); i; i = g_list_next(i)) {
	for (j = g_list_first(split_views); j; j = g_list_next(j)) {
	    if (addr_set_is_subset(vs_view_get_addr(i->data), 
				   vs_view_get_addr(j->data)))
		break;
	}
	g_assert(j);
	view = vs_view_new(VS_VIEW_REG, ++shm_be->view_id);
	for (addr_i = addr_set_first(vs_view_get_addr(i->data)); addr_i;
	     addr_i = addr_set_next(addr_i)) {
	    vs_view_addr_insert(view, addr_cast(addr_i));
	}
	shm_be_queue_conf(memb, view);
	vs_view_free(view);
    }
    
    
    for (i = g_list_first(old_views); i; i = g_list_next(i))
	vs_view_free(i->data);
    for (i = g_list_first(split_views); i; i = g_list_next(i))
	vs_view_free(i->data);
    for (i = g_list_first(new_views); i; i = g_list_next(i))
	vs_view_free(i->data);
    g_list_free(old_views);
    g_list_free(split_views);
    g_list_free(new_views);
    addr_set_free(targets);
}


void vs_backend_shm_merge()
{
    GList *old_views = NULL;
    GList *merge_views = NULL;
    addr_set_t *targets;
    const addr_set_iter_t *addr_i;
    GList *i;
    int n, n_merge;
    vs_view_t *view;
    vs_view_t *reg_view, *trans_view = NULL;
    shm_be_memb_t *memb;
    vs_backend_t *be;
    targets = addr_set_new();
     
    for (i = g_list_first(shm_be->memb); i; i = g_list_next(i)) {
	be = i->data;
	memb = be->priv;
	/* Others will be gone at the moment of delivery */
	if (memb->state == CONNECTING || memb->state == CONNECTED)
	    addr_set_insert(targets, memb->addr);
    }
    for (addr_i = addr_set_first(targets); addr_i; 
	 addr_i = addr_set_next(addr_i)) {
	if (!(view = shm_be_get_last_view_by_addr(addr_cast(addr_i)))) {
	    g_assert(!"Not possible");
	}
	if (!g_list_find_custom(old_views, view, &view_cmp))
	    old_views = g_list_insert_sorted(old_views, view, &view_cmp);
	else
	    vs_view_free(view);
    }
     
    fprintf(stderr, "Old views:\n");
    for (i = g_list_first(old_views); i; i = g_list_next(i)) {
	print_view(stderr, i->data);
	fprintf(stderr, "\n");
    }

    if (g_list_length(old_views) > 1) {
	n_merge = 1 + rand()%g_list_length(old_views);
	for (n = 0; n < n_merge; n++) {
	    view = g_list_nth_data(old_views, rand()%g_list_length(old_views));
	    old_views = g_list_remove(old_views, view);
	    merge_views = g_list_append(merge_views, view);
	}
    }
     
    fprintf(stderr, "Merge views:\n");
    for (i = g_list_first(merge_views); i; i = g_list_next(i)) {
	print_view(stderr, i->data);
	fprintf(stderr, "\n");
    }

    i = g_list_first(shm_be->memb);     
    memb = i ? ((vs_backend_t *)i->data)->priv : NULL;
     
    for (i = g_list_first(merge_views); i; i = g_list_next(i)) {
	view = i->data;
	trans_view = vs_view_new(VS_VIEW_TRANS, vs_view_get_id(view));
	for (addr_i = addr_set_first(vs_view_get_addr(view)); addr_i;
	     addr_i = addr_set_next(addr_i)) {
	    vs_view_addr_insert(trans_view, addr_cast(addr_i));
	}
	shm_be_queue_conf(memb, trans_view);
	vs_view_free(trans_view);
    }
     
    if (trans_view) {
	reg_view = vs_view_new(VS_VIEW_REG, ++shm_be->view_id);
	for (i = g_list_first(merge_views); i; i = g_list_next(i)) {
	    view = i->data;
	    for (addr_i = addr_set_first(vs_view_get_addr(view)); addr_i;
		 addr_i = addr_set_next(addr_i)) {
		vs_view_addr_insert(reg_view, addr_cast(addr_i));
	    }
	}
	shm_be_queue_conf(memb, reg_view);
	fprintf(stderr, "Merged view: ");
	print_view(stderr, reg_view);
	fprintf(stderr, "\n");
	vs_view_free(reg_view);
    }     
    
    for (i = g_list_first(old_views); i; i = g_list_next(i))
	vs_view_free(i->data);
    for (i = g_list_first(merge_views); i; i = g_list_next(i))
	vs_view_free(i->data);
    g_list_free(old_views);
    g_list_free(merge_views);
    addr_set_free(targets);
    
}





/***********************************************************************/

static int shm_be_connect(vs_backend_t *be, const group_id_t group)
{
    int ret = 0;
    GList *i;
    const addr_set_iter_t *addr_i;
    shm_be_memb_t *memb = be->priv;
    vs_backend_t *be_p;
    shm_be_memb_t *memb_p;
    vs_view_t *view, *trans_view, *reg_view;

    if (shm_be->memb && g_list_find(shm_be->memb, be))
	return EPERM;
    memb->addr = ++shm_be->last_addr;
    shm_be->memb = g_list_append(shm_be->memb, be);

    view = NULL;
    for (i = g_list_first(shm_be->memb); i && view == NULL;
	 i = g_list_next(i) ) {
	be_p = i->data;
	memb_p = be_p->priv;
	if ((memb_p->state == CONNECTING || memb_p->state == CONNECTED)) {
	    view = shm_be_get_last_view_by_addr(memb_p->addr);
	    assert(view);
	    print_view(stderr, view);
	}
    }

    trans_view = vs_view_new(VS_VIEW_TRANS, view ? vs_view_get_id(view) : 0);
    
    for (addr_i = addr_set_first(vs_view_get_addr(view));
	 addr_i; addr_i = addr_set_next(addr_i)) {
	vs_view_addr_insert(trans_view, addr_cast(addr_i));
    }
    
    reg_view = vs_view_new(VS_VIEW_REG, ++shm_be->view_id);
    vs_view_joined_addr_insert(reg_view, memb->addr);
    vs_view_addr_insert(reg_view, memb->addr);
    for (addr_i = addr_set_first(vs_view_get_addr(trans_view));
	 addr_i; addr_i = addr_set_next(addr_i)) {
	vs_view_addr_insert(reg_view, addr_cast(addr_i));
    }

    shm_be_queue_conf(memb, trans_view);
    shm_be_queue_conf(memb, reg_view);

    vs_view_free(trans_view);
    vs_view_free(reg_view);
    vs_view_free(view);

    memb->state = CONNECTING;

    return ret;
}

static void shm_be_close(vs_backend_t *be)
{
    const addr_set_iter_t *addr_i;
    shm_be_memb_t *memb;
    vs_view_t *view, *trans_view, *reg_view;

    if (!g_list_find(shm_be->memb, be))
	return;

    memb = be->priv;
    
    view = shm_be_get_last_view_by_addr(memb->addr);
    assert(view);

    trans_view = vs_view_new(VS_VIEW_TRANS, vs_view_get_id(view));
    reg_view = vs_view_new(VS_VIEW_REG, ++shm_be->view_id);

    vs_view_left_addr_insert(trans_view, memb->addr);
    for (addr_i = addr_set_first(vs_view_get_addr(view)); addr_i;
	 addr_i = addr_set_next(addr_i)) {
	if (addr_cast(addr_i) != memb->addr) {
	    vs_view_addr_insert(trans_view, addr_cast(addr_i));
	    vs_view_addr_insert(reg_view, addr_cast(addr_i));
	}
    }
    
    shm_be_queue_conf(memb, trans_view);
    if (vs_view_get_size(reg_view) > 0) {
	shm_be_queue_conf(memb, reg_view);
    }

    vs_view_free(trans_view);
    vs_view_free(reg_view);
    vs_view_free(view);

    memb->state = CLOSING;

}

static void shm_be_free(vs_backend_t *be)
{

    GList *i;
    shm_be_msg_t *msg;
    

    shm_be->memb = g_list_remove(shm_be->memb, be);
    if (shm_be->memb == NULL) {
	for (i = g_queue_peek_head_link(shm_be->msg_queue); i;
	     i = g_list_next(i)) {
	    msg = i->data;
	    free(msg->buf);
	    free(msg);
	}

	for (i = g_queue_peek_head_link(shm_be->conf_queue); i;
	     i = g_list_next(i)) {
	    msg = i->data;
	    free(msg->buf);
	    free(msg);
	}

	g_queue_free(shm_be->msg_queue);
	g_queue_free(shm_be->conf_queue);

	for (i = g_list_first(shm_be->views); i; i = g_list_next(i)) {
	    shm_be_view_free(i->data);
	}
	g_list_free(shm_be->views);
	free(shm_be);
	shm_be = NULL;
    }

    free(be->priv);
    free(be);
}

static int shm_be_pass_down_cb(protolay_t *pl, writebuf_t *wb, 
			       const down_meta_t *dm)
{
    vs_backend_t *be = (vs_backend_t *) protolay_get_priv(pl);
    shm_be_memb_t *memb = be->priv;
    shm_be_msg_t *msg;
    char *buf;
    
    if (memb->state != CONNECTED)
	return ENOTCONN;
    
    buf = malloc(writebuf_get_totlen(wb));
    memcpy(buf, writebuf_get_hdr(wb), writebuf_get_hdrlen(wb));
    memcpy(buf + writebuf_get_hdrlen(wb), writebuf_get_payload(wb),
	   writebuf_get_payloadlen(wb));
    
    msg = malloc(sizeof(shm_be_msg_t));
    msg->buf = buf;
    msg->buflen = writebuf_get_totlen(wb);

    g_queue_push_tail(shm_be->msg_queue, msg);
    return 0;
}

static void shm_be_deliver_empty_trans_view(vs_backend_t *be,
					    const vs_view_t *view,
					    const vs_msg_t *vs_msg)
{
    char *buf;
    size_t buflen;
    vs_msg_t *trans_msg;
    vs_view_t *trans_view;
    readbuf_t *rb;
    
    trans_msg = vs_msg_new(VS_MSG_TRANS_CONF, vs_msg_get_source(vs_msg),
			   view ? vs_view_get_id(view) : 0, 0, VS_MSG_SAFETY_SAFE);
    
    trans_view = vs_view_new(VS_VIEW_TRANS, view ? vs_view_get_id(view) : 0);
    buflen = vs_msg_get_hdrlen(trans_msg) + vs_view_get_len(trans_view);
    buf = malloc(buflen);
    memcpy(buf, vs_msg_get_hdr(trans_msg), vs_msg_get_hdrlen(trans_msg));
    vs_view_write(trans_view, buf, buflen, vs_msg_get_hdrlen(trans_msg));
    rb = readbuf_new(buf, buflen);
    
    protolay_pass_up(be->pl, rb, 0, NULL);
    
    readbuf_free(rb);
    vs_view_free(trans_view);
    vs_msg_free(trans_msg);
    free(buf);
}

static int shm_be_handle_conf(const readbuf_t *rb, const vs_msg_t *vs_msg)
{
    GList *i, *i_next;
    vs_view_t *view;
    shm_be_view_t *be_view;
    shm_be_view_t *new_be_view;
    vs_backend_t *be;
    shm_be_memb_t *memb;
    
    if (!vs_view_read(vs_msg_get_payload(vs_msg),
		      vs_msg_get_payload_len(vs_msg), 0, &view))
	return EPROTO;
    
    if (!(be_view = shm_be_get_preceeding_be_view(view))) {
	/* Initiating empty transitional view, this is not delivered 
	 * anywhere */
	if (vs_view_get_type(view) == VS_VIEW_TRANS) {
	    if (vs_view_get_size(view) > 0) {
		print_view(stderr, view);
		fprintf(stderr, "\n");
	    }
	    assert(vs_view_get_size(view) == 0);
	    vs_view_free(view);
	    return 0;
	} else {
	    be_view = shm_be_view_new(vs_view_get_id(view));
	    be_view->reg_view = view;
	    shm_be->views = g_list_append(shm_be->views, be_view);
	}
    } else if (vs_view_get_type(view) == VS_VIEW_TRANS) {
	if (be_view->trans_view) {
	    new_be_view = shm_be_view_new(vs_view_get_id(view));
	    new_be_view->trans_view = view;
	    new_be_view->reg_view = vs_view_copy(be_view->reg_view);
	    shm_be->views = g_list_append(shm_be->views, new_be_view);
	    be_view = new_be_view;
	} else {
	    be_view->trans_view = view;
	}
    } else {
	new_be_view = shm_be_view_new(vs_view_get_id(view));
	new_be_view->reg_view = view;
	shm_be->views = g_list_remove(shm_be->views, be_view);
	shm_be->views = g_list_append(shm_be->views, new_be_view);
	shm_be_view_free(be_view);
	be_view = new_be_view;
    }
    

    for (i = g_list_first(shm_be->memb); i; i = i_next) {
	i_next = g_list_next(i);
	be = i->data;
	memb = be->priv;
	if (memb->state == CONNECTING && 
	    vs_view_get_type(view) == VS_VIEW_REG &&
	    vs_view_find_addr(view, memb->addr)) {
	    shm_be_deliver_empty_trans_view(be, NULL, vs_msg);
	    memb->state = CONNECTED;
	    protolay_pass_up(be->pl, rb, 0, NULL);
	} else if (memb->state == CLOSING &&
		   vs_view_get_type(view) == VS_VIEW_TRANS &&
		   !vs_view_find_addr(view, memb->addr) &&
		   vs_msg_get_source(vs_msg) == memb->addr) {
	    memb->state = CLOSED;
	    shm_be_deliver_empty_trans_view(be, view, vs_msg);
	    memb->addr = ADDR_INVALID;
	    shm_be->memb = g_list_remove(shm_be->memb, be);
	} else if (memb->state == CONNECTED || memb->state == CLOSING) {
	    if (vs_view_find_addr(view, memb->addr))
		protolay_pass_up(be->pl, rb, 0, NULL);
	}
    }
    
    return 0;
}


static int shm_be_handle_msg(const readbuf_t *rb, const vs_msg_t *vs_msg)
{
    GList *i;
    shm_be_view_t *source_view;
    shm_be_view_t *be_view;
    shm_be_memb_t *memb;
    vs_backend_t *be;

    if (!(source_view = shm_be_get_current_be_view_by_addr(
	      vs_msg_get_source(vs_msg)))) {
	fprintf(stderr, "Dropping message from %8.8x, source not in any view\n",
		vs_msg_get_source(vs_msg));
    } else {
	for (i = g_list_first(shm_be->memb); i; i = g_list_next(i)) {
	    be = i->data;
	    memb = be->priv;
	    /* Condition to deliver message for a member: 
	     *
	     * 1) Member state must be connected or closing
	     * 2) There must exist current view which member is part of
	     * 3) Message source must exist in reg view of current view
	     * 4) Message source view id and current view id must be equal
	     *
	     * Conditions 1) and 2) may actually be equivivalent
	     *
	     * Combination of conditions 3) and 4) state that if message
	     * is delivered in regular view, it is delivered in exactly one
	     * regular view and the source of the message exists in that view.
	     *
	     * Combination of 2), 3) and 4) implement extended virtual synchrony
	     */
	    if ((memb->state == CONNECTED || memb->state == CLOSING) &&
		(be_view = shm_be_get_current_be_view_by_addr(memb->addr)) &&
		vs_view_find_addr(be_view->reg_view, vs_msg_get_source(vs_msg)) &&
		(vs_view_get_id(be_view->reg_view) ==
		 vs_view_get_id(source_view->reg_view))) {
		protolay_pass_up(be->pl, rb, 0, NULL);
	    } else {
#if 0
		fprintf(stderr, "Message (type=%u,source=%8.8x) not delivered for memb (addr=%8.8x,state=%i)\n",
			vs_msg_get_type(vs_msg), vs_msg_get_source(vs_msg),
			memb->addr, memb->state);
#endif /* 0 */
	    }
	}
    }
    
    return 0;
}

static int shm_be_poll_cb(protolay_t *pl)
{
    int ret = 0;
    shm_be_msg_t *shm_msg;
    vs_msg_t *vs_msg;
    readbuf_t *rb;
    
    do {
	shm_msg = NULL;
	if (g_queue_peek_head(shm_be->msg_queue) && 
	    g_queue_peek_head(shm_be->conf_queue)) {
	    if (rand()%11)
		shm_msg = g_queue_pop_head(shm_be->msg_queue);
	    else 
		shm_msg = g_queue_pop_head(shm_be->conf_queue);
	} else if (g_queue_peek_head(shm_be->msg_queue)) {
	    shm_msg = g_queue_pop_head(shm_be->msg_queue);
	} else if (g_queue_peek_head(shm_be->conf_queue)) {
	    shm_msg = g_queue_pop_head(shm_be->conf_queue);
	}
	
	if (!shm_msg)
	    return 0;
	
	rb = readbuf_new(shm_msg->buf, shm_msg->buflen);
	
	if (!(vs_msg = vs_msg_read(rb, 0))) {
	    ret = EPROTO;
	    goto out;
	}
	
	if (vs_msg_is_conf(vs_msg))
	    ret = shm_be_handle_conf(rb, vs_msg);
	else
	    ret = shm_be_handle_msg(rb, vs_msg);
	vs_msg_free(vs_msg);
    out:
	free(shm_msg->buf);
	free(shm_msg);
	readbuf_free(rb);
    } while (ret == 0);
    return ret;
}

static addr_t shm_be_get_self_addr(const vs_backend_t *be)
{
    shm_be_memb_t *memb;

    memb = be->priv;
    return memb->addr;

}

vs_backend_t *vs_backend_shm_new(const char *conf, poll_t *poll,
				 protolay_t *up_ctx,
				 void (*pass_up_cb)(protolay_t *,
						    const readbuf_t *,
						    const size_t,
						    const up_meta_t *))
{
    vs_backend_t *be;
    shm_be_memb_t *memb;

    if (!shm_be) {
	shm_be = malloc(sizeof(shm_be_t));
	shm_be->msg_queue = g_queue_new();
	shm_be->conf_queue = g_queue_new();
	shm_be->memb = NULL;
	shm_be->refcnt = 1;
	shm_be->last_addr = 0;
	shm_be->view_id = 0;
	shm_be->views = NULL;
    }    
    
    be = malloc(sizeof(vs_backend_t));
    be->connect = &shm_be_connect;
    be->close = &shm_be_close;
    be->free = &shm_be_free;
    be->get_self_addr = &shm_be_get_self_addr;
    be->priv = malloc(sizeof(shm_be_memb_t));
    memb = be->priv;
    memb->addr = ADDR_INVALID;
    memb->state = CLOSED;

    be->pl = protolay_new(be, NULL);
    protolay_set_up(be->pl, up_ctx, pass_up_cb);
    protolay_set_down(up_ctx, be->pl, &shm_be_pass_down_cb);
    protolay_set_poll(be->pl, NULL, NULL, &shm_be_poll_cb);

    return be;
}


#if 0
/* Old implementation lies below... good bye and good luck */
typedef struct vs_backend_shm_ vs_backend_shm_t;

typedef struct vs_backend_shm_msg_ {
    vs_msg_safety_e safety;
    char *msgbuf;
    size_t msgbuf_len;
} vs_backend_shm_msg_t;

typedef enum {
    DISCONNECTED, 
    CONNECTING,
    CONNECTED, 
    DISCONNECTING
} vs_backend_shm_state_e;

typedef struct vs_backend_shm_memb_ {
    vs_backend_t be;
    addr_t id;
    vs_backend_shm_state_e state;
    vs_backend_shm_t *shm_loc;
    void *user_context;
    void  (*recv_cb)(void *, const vs_msg_t *);
} vs_backend_shm_memb_t;

typedef struct vs_backend_shm_view_ {
    vs_view_id_t view_id;
    vs_view_t *reg_view;
    vs_view_t *trans_view;
} vs_backend_shm_view_t;

static vs_backend_shm_view_t *vs_backend_shm_view_new(const vs_view_id_t view_id)
{
    vs_backend_shm_view_t *view;


    view = g_malloc(sizeof(vs_backend_shm_view_t));
    view->view_id = view_id;
    view->reg_view = NULL;
    view->trans_view = NULL;
    return view;
}

static void vs_backend_shm_view_free(vs_backend_shm_view_t *view)
{
    if (view) {
	vs_view_free(view->reg_view);
	vs_view_free(view->trans_view);
	g_free(view);
    }
}

struct vs_backend_shm_ {
    int refcnt;
    addr_t last_id;
    vs_view_id_t view_id;
    GList *views;
    void *user_context;
    void (*send_cb)(void *, const vs_msg_t *);
    GQueue *msg_queue;
    GQueue *conf_queue;
    GList  *memb;
};


static vs_backend_shm_t *shm_be = NULL;



static void print_view(const vs_view_t *view)
{
    const addr_set_iter_t *i;
    printf("type = %s id = %i memb = ", 
	   vs_view_get_type(view) == VS_VIEW_TRANS ? "TRANS" : "REG", 
	   vs_view_get_id(view));
    for (i = addr_set_first(vs_view_get_addr(view)); i; 
	 i = addr_set_next(i))
	printf("%i ", addr_cast(i));
}


static vs_backend_shm_view_t *get_preceeding_view(const vs_view_t *view)
{
    vs_backend_shm_view_t *ret = NULL;
    vs_backend_shm_view_t *view_data;
    GList *i;

    for (i = g_list_last(shm_be->views); i && ret == NULL; 
	 i = g_list_previous(i)) {
	view_data = i->data;
	if (vs_view_get_type(view) == VS_VIEW_TRANS &&
	    vs_view_get_id(view) == vs_view_get_id(view_data->reg_view)) {
	    g_assert(vs_view_get_type(view_data->reg_view) == VS_VIEW_REG);
	    g_assert(addr_set_is_subset(
			 vs_view_get_addr(view), 
			 vs_view_get_addr(view_data->reg_view)));
	    ret = view_data;
	} else if (vs_view_get_type(view) == VS_VIEW_REG && 
		   view_data->trans_view) {
	    g_assert(vs_view_get_type(view_data->trans_view) == VS_VIEW_TRANS);
	       
	    if (addr_set_is_subset(vs_view_get_addr(view_data->trans_view),
				   vs_view_get_addr(view)) &&
		vs_view_get_size(view_data->trans_view) > 0) {
		ret = view_data;
	    }
	}
    }
    return ret;
}

static int view_cmp(const void *a, const void *b)
{
    const vs_view_t *va = (const vs_view_t *)a;
    const vs_view_t *vb = (const vs_view_t *)b;
     
    if (vs_view_get_id(va) < vs_view_get_id(vb))
	return -1;
    else if (vs_view_get_id(va) > vs_view_get_id(vb))
	return 1;
     
    g_assert(addr_set_equal(vs_view_get_addr(va), 
			    vs_view_get_addr(vb)));
    return 0;
}

static vs_msg_t *get_last_view_msg_by_addr(const addr_t addr)
{
    vs_view_t *view;
    msg_t *msgbuf;
    vs_msg_t *msg;
    vs_backend_shm_msg_t *shm_msg;
    GList *i;
    vs_msg_t *ret = NULL;

    for (i = g_queue_peek_tail_link(shm_be->conf_queue); i && ret == NULL; 
	 i = g_list_previous(i)) {
	shm_msg = i->data;
	msgbuf = msg_read(shm_msg->msgbuf, shm_msg->msgbuf_len);
	msg = vs_msg_read(msgbuf);
	  
	if (vs_msg_get_type(msg) == VS_MSG_REG_CONF) {
	    if (!vs_view_read(vs_msg_get_payload(msg), 
			      vs_msg_get_payload_len(msg), 0, &view)) {
		g_assert(!"Corrupted message?");
	    }
	    if (vs_view_find_addr(view, addr))
		ret = vs_msg_copy(msg);
	    vs_view_free(view);
	}
	vs_msg_free(msg);
	msg_free(msgbuf);
    }
    return ret;
}

static vs_backend_shm_view_t *get_current_view_by_addr(const addr_t addr)
{
    GList *i;
    vs_backend_shm_view_t *view;
    vs_backend_shm_view_t *ret = NULL;

    for (i = g_list_last(shm_be->views); i && ret == NULL; 
	 i = g_list_previous(i)) {
	view = i->data;
	if (view->trans_view && vs_view_find_addr(view->trans_view, addr)) {
	    ret = view;
	}
    }
    for (i = g_list_last(shm_be->views); i; 
	 i = g_list_previous(i)) {
	view = i->data;
	if (vs_view_find_addr(view->reg_view, addr)) {
	    if (!ret || vs_view_get_id(ret->reg_view) < vs_view_get_id(view->reg_view))
		ret = view;
	}
    }     
     
    return ret;
}

static vs_view_t *get_last_view_by_addr(const addr_t addr)
{
    vs_msg_t *view_msg;
    vs_backend_shm_view_t *be_view;
    vs_view_t *view;
    vs_view_t *ret = NULL;
    if ((view_msg = get_last_view_msg_by_addr(addr))) {
	if (!vs_view_read(vs_msg_get_payload(view_msg),
			  vs_msg_get_payload_len(view_msg), 
			  0, &view)) {
	    g_assert(!"View read failed");
	}
	g_assert(vs_view_find_addr(view, addr));
	g_assert(vs_view_get_type(view) == VS_VIEW_REG);
	vs_msg_free(view_msg);
	ret = view;
    } else if ((be_view = get_current_view_by_addr(addr))) {
	g_assert(be_view->reg_view);
	g_assert(vs_view_find_addr(be_view->reg_view, addr));
	g_assert(vs_view_get_type(be_view->reg_view) == VS_VIEW_REG);
	ret = vs_view_copy(be_view->reg_view);
    } 
    return ret;
}

static void vs_backend_shm_set_recv_callback(vs_backend_t *be, void *user_context, void (*fn)(void *, const vs_msg_t *))
{
    vs_backend_shm_memb_t *memb = (vs_backend_shm_memb_t *) be;
    memb->user_context = user_context;
    memb->recv_cb = fn;
}

static addr_t vs_backend_shm_get_self_addr(const vs_backend_t *be)
{
    const vs_backend_shm_memb_t *memb = (const vs_backend_shm_memb_t *) be;
    g_assert(memb);
    return memb->id;
}

static vs_view_id_t vs_backend_shm_get_view_id(const vs_backend_t *be)
{
    g_assert(be);
    return shm_be->view_id;
}

static int vs_backend_shm_get_fd(const vs_backend_t *be)
{
    return -1;
}


static void vs_backend_shm_free(vs_backend_t *be)
{
    GList *i;
    vs_backend_shm_msg_t *msg;
    g_assert(((vs_backend_shm_memb_t *)be)->shm_loc == shm_be);
     
    shm_be->memb = g_list_remove(shm_be->memb, be);
    shm_be->refcnt--;
    if (shm_be->refcnt == 0) {
	g_assert(shm_be->memb == NULL);
	for (i = g_queue_peek_head_link(shm_be->msg_queue); i; 
	     i = g_list_next(i)) {
	    msg = i->data;
	    g_free(msg->msgbuf);
	    g_free(msg);
	}

	for (i = g_queue_peek_head_link(shm_be->conf_queue); i; 
	     i = g_list_next(i)) {
	    msg = i->data;
	    g_free(msg->msgbuf);
	    g_free(msg);
	}
	  
	g_queue_free(shm_be->msg_queue);
	g_queue_free(shm_be->conf_queue);
	for (i = g_list_first(shm_be->views); i; i = g_list_next(i)) {
	    vs_backend_shm_view_free(i->data);
	}
	g_list_free(shm_be->views);
	g_free(shm_be);
	shm_be = NULL;

    } 
    g_free(be);
}



static int vs_backend_shm_send(vs_backend_t *be, const vs_msg_t *msg)
{
    vs_backend_shm_msg_t *be_msg = NULL;
    vs_backend_shm_memb_t *memb = (vs_backend_shm_memb_t *) be;

    if (memb->state != CONNECTED && vs_msg_is_user(msg))
	return ENOTCONN;
     
    if (!(be_msg = g_malloc(sizeof(vs_backend_shm_msg_t))))
	return ENOMEM;
    if (!(be_msg->msgbuf_len = msg_get_len(vs_msg_get_writebuf(msg))))
	goto err;
    if (!(be_msg->msgbuf = g_malloc(be_msg->msgbuf_len)))
	goto err;
    if (!msg_write(vs_msg_get_writebuf(msg), be_msg->msgbuf, 
		   be_msg->msgbuf_len))
	goto err;
    be_msg->safety = vs_msg_get_safety(msg);
     
    if (vs_msg_is_conf(msg))
	g_queue_push_tail(shm_be->conf_queue, be_msg);
    else
	g_queue_push_tail(shm_be->msg_queue, be_msg);
    if (shm_be->send_cb) {
	shm_be->send_cb(shm_be->user_context, msg);
    }
    return 0;
err:
    if (be_msg) {
	g_free(be_msg->msgbuf);
	g_free(be_msg);
    }
    return EINVAL;
}



static void vs_backend_shm_send_conf(vs_backend_t *be, const vs_view_t *view)
{
    vs_msg_t *vs_msg;
    msg_t *msg;
    char *view_buf;     
    size_t view_buf_len;
    /* Queue trans conf, reg conf at the tail of queue */
    /* TODO: Introduce some randomness for trans and reg
     * conf places to test upper layers */
     
    if (vs_view_get_type(view) == VS_VIEW_REG) {
	g_assert(vs_view_get_size(view) > 0);
    }
	  
    view_buf_len = vs_view_get_len(view);
    view_buf = g_malloc(view_buf_len);
     
    vs_view_write(view, view_buf, view_buf_len, 0);
    msg = msg_new();
    msg_set_payload(msg, view_buf, view_buf_len);
    vs_msg = vs_msg_new(vs_view_get_type(view) == VS_VIEW_TRANS ? 
			VS_MSG_TRANS_CONF : VS_MSG_REG_CONF,
			vs_backend_shm_get_self_addr(be), 0,
			VS_MSG_SAFETY_SAFE,
			0, msg, NULL);
     
    vs_backend_shm_send(be, vs_msg);
     
    vs_msg_free(vs_msg);
    msg_free(msg);
    g_free(view_buf);
}

static int vs_backend_shm_connect(vs_backend_t *be, const group_id_t group)
{
    /* So far group is ignored */
    vs_backend_shm_memb_t *memb = (vs_backend_shm_memb_t *) be;
    vs_backend_shm_memb_t *memb_p;
    GList *i;
    const addr_set_iter_t *addr_i;
    vs_view_t *trans_view;
    vs_view_t *reg_view;
    vs_view_t *view = NULL;
     
    if (shm_be->memb && g_list_find(shm_be->memb, memb))
	return EPERM;
     
    memb->id = ++shm_be->last_id;
    shm_be->memb = g_list_append(shm_be->memb, memb);

    printf("Connecting %i\n", memb->id);
     
    for (i = g_list_first(shm_be->memb); i && view == NULL; 
	 i = g_list_next(i)) {
	memb_p = (vs_backend_shm_memb_t *)i->data;
	printf("memb %i state %i\n", memb_p->id, memb_p->state);
	if ((memb_p->state == CONNECTING ||memb_p->state == CONNECTED)) {
	    view = get_last_view_by_addr(memb_p->id);
	    g_assert(view);
	    print_view(view);
	    printf("\n");
	}
    }
     
    if (view)
	trans_view = vs_view_new(VS_VIEW_TRANS, vs_view_get_id(view));     
    else 
	trans_view = vs_view_new(VS_VIEW_TRANS, 0);
     
    for (addr_i = addr_set_first(vs_view_get_addr(view)); 
	 addr_i; addr_i = addr_set_next(addr_i))
	vs_view_addr_insert(trans_view, addr_cast(addr_i));

    reg_view = vs_view_new(VS_VIEW_REG, ++shm_be->view_id);
    vs_view_joined_addr_insert(reg_view, memb->id);
    vs_view_addr_insert(reg_view, memb->id);
     
    for (addr_i = addr_set_first(vs_view_get_addr(trans_view));
	 addr_i; addr_i = addr_set_next(addr_i)) {
	vs_view_addr_insert(reg_view, addr_cast(addr_i));
    }
     
    vs_backend_shm_send_conf(be, trans_view);
    vs_backend_shm_send_conf(be, reg_view);
     
    vs_view_free(trans_view);
    vs_view_free(reg_view);
    vs_view_free(view);
    memb->state = CONNECTING;

    return 0;
}



static void vs_backend_shm_disconnect(vs_backend_t *be)
{
    const addr_set_iter_t *addr_i;
    vs_view_t *view = NULL;
    vs_view_t *trans_view;
    vs_view_t *reg_view;
    vs_backend_shm_memb_t *memb = (vs_backend_shm_memb_t *)be;
    if (!g_list_find(shm_be->memb, be))
	return;
     
    printf("Disconnecting %i\n", memb->id);
     
    view = get_last_view_by_addr(memb->id);
    g_assert(view);
     
    printf("Last view: ");
    print_view(view);
    printf("\n");

    trans_view = vs_view_new(VS_VIEW_TRANS, vs_view_get_id(view));
    vs_view_left_addr_insert(trans_view, memb->id);
    for (addr_i = addr_set_first(vs_view_get_addr(view)); addr_i;
	 addr_i = addr_set_next(addr_i)) {
	if (addr_cast(addr_i) != memb->id)
	    vs_view_addr_insert(trans_view, addr_cast(addr_i));
    }
     
    reg_view = vs_view_new(VS_VIEW_REG, ++shm_be->view_id);
    for (addr_i = addr_set_first(vs_view_get_addr(trans_view));
	 addr_i; addr_i = addr_set_next(addr_i)) {
	vs_view_addr_insert(reg_view, addr_cast(addr_i));
    }
     
    printf("Trans view: ");
    print_view(trans_view);
    printf("\n");

    printf("Reg view:");
    print_view(reg_view);
    printf("\n");

    vs_backend_shm_send_conf(be, trans_view);
    if (vs_view_get_size(reg_view) > 0)
	vs_backend_shm_send_conf(be, reg_view);
     
    vs_view_free(trans_view);
    vs_view_free(reg_view);
    vs_view_free(view);

    memb->state = DISCONNECTING;
}



void vs_backend_shm_split()
{
    GList *i, *j;
    GList *new_views = NULL;
    GList *old_views = NULL;
    GList *split_views = NULL;
    int n_new_views;
    int n_old_views;
    int n, split, s, rn;
    addr_set_t *targets, *tmp_set, *set_intr;
    addr_t addr;
    vs_backend_shm_memb_t *memb;
    const addr_set_iter_t *addr_i;
    vs_view_t *view, *split_view1, *split_view2;
    size_t view_size;


    printf("Split!!!\n");
     
    targets = addr_set_new();
     
    for (i = g_list_first(shm_be->memb); i; i = g_list_next(i)) {
	memb = i->data;
	/* Others will be gone at the moment of delivery */
	if (memb->state == CONNECTING || memb->state == CONNECTED)
	    addr_set_insert(targets, memb->id);
    }
     
    for (addr_i = addr_set_first(targets); addr_i; 
	 addr_i = addr_set_next(addr_i)) {
	if (!(view = get_last_view_by_addr(addr_cast(addr_i)))) {
	    g_assert(!"Not possible");
	}
	if (!g_list_find_custom(old_views, view, &view_cmp))
	    old_views = g_list_insert_sorted(old_views, view, &view_cmp);
	else
	    vs_view_free(view);
    }
     
    printf("Old views:\n");
    for (i = g_list_first(old_views); i; i = g_list_next(i)) {
	print_view(i->data);
	printf("\n");
    }

    /* Now we should have complete set of future views... 
     * TODO: Verify that union of views covers addr_set */
     
    n_old_views = g_list_length(old_views);
    n_new_views = n_old_views + 1 + rand()%3;
     
    for (n = n_old_views; n < n_new_views && old_views; n++) {
	i = g_list_nth(old_views, rand()%g_list_length(old_views));
	view = i->data;
	tmp_set = addr_set_copy(vs_view_get_addr(view));
	if ((view_size = addr_set_size(tmp_set)) > 2) {
	    old_views = g_list_remove(old_views, view);
	    split = 1 + rand()%(view_size - 1);
	    split_view1 = vs_view_new(VS_VIEW_REG, vs_view_get_id(view));
	    split_view2 = vs_view_new(VS_VIEW_REG, vs_view_get_id(view));
	    printf("split = %i\n", split);
	    for (s = view_size; s > split; s--) {
		rn = rand()%addr_set_size(tmp_set);
		addr = addr_cast(addr_set_nth(tmp_set, rn));
		vs_view_addr_insert(split_view1, addr);
		addr_set_erase(tmp_set, addr);
	    }
	    for (addr_i = addr_set_first(tmp_set); addr_i; 
		 addr_i = addr_set_next(addr_i))
		vs_view_addr_insert(split_view2, addr_cast(addr_i));
	       
	    new_views = g_list_append(new_views, split_view1);
	    new_views = g_list_append(new_views, split_view2);
	    split_views = g_list_append(split_views, view);
	} 
	addr_set_free(tmp_set);
    }
     

    printf("Split views:\n");
    for (i = g_list_first(split_views); i; i = g_list_next(i)) {
	print_view(i->data);
	printf("\n");
	for (addr_i = addr_set_first(vs_view_get_addr(i->data));
	     addr_i; addr_i = addr_set_next(addr_i)) {
	    for (j = g_list_first(new_views); j; j = g_list_next(j)) {
		if (vs_view_find_addr(j->data, addr_cast(addr_i)))
		    break;
	    }
	    g_assert(j);
	}
    }

    printf("New views:\n");
     
    for (i = g_list_first(new_views); i; i = g_list_next(i)) {
	print_view(i->data);
	printf("\n");
	g_assert(vs_view_get_size(i->data) > 0);
	for (j = i->next; j; j = g_list_next(j)) {
	    set_intr = addr_set_intersection(vs_view_get_addr(i->data), 
					     vs_view_get_addr(j->data));
	    g_assert(addr_set_size(set_intr) == 0);
	    addr_set_free(set_intr);
	}
    }
     
    i = g_list_first(shm_be->memb);
    memb = i ? i->data : NULL;
    g_assert(memb || (!new_views && !split_views && !old_views));

    for (i = g_list_first(new_views); i; i = g_list_next(i)) {
	for (j = g_list_first(split_views); j; j = g_list_next(j)) {
	    if (addr_set_is_subset(vs_view_get_addr(i->data), 
				   vs_view_get_addr(j->data)))
		break;
	}
	g_assert(j);
	view = vs_view_new(VS_VIEW_TRANS, vs_view_get_id(j->data));
	for (addr_i = addr_set_first(vs_view_get_addr(i->data)); addr_i;
	     addr_i = addr_set_next(addr_i)) {
	    vs_view_addr_insert(view, addr_cast(addr_i));
	}
	vs_backend_shm_send_conf((vs_backend_t *)memb, view);
	vs_view_free(view);
    }

    for (i = g_list_first(new_views); i; i = g_list_next(i)) {
	for (j = g_list_first(split_views); j; j = g_list_next(j)) {
	    if (addr_set_is_subset(vs_view_get_addr(i->data), 
				   vs_view_get_addr(j->data)))
		break;
	}
	g_assert(j);
	view = vs_view_new(VS_VIEW_REG, ++shm_be->view_id);
	for (addr_i = addr_set_first(vs_view_get_addr(i->data)); addr_i;
	     addr_i = addr_set_next(addr_i)) {
	    vs_view_addr_insert(view, addr_cast(addr_i));
	}
	vs_backend_shm_send_conf((vs_backend_t *)memb, view);
	vs_view_free(view);
    }
     
     
    for (i = g_list_first(old_views); i; i = g_list_next(i))
	vs_view_free(i->data);
    for (i = g_list_first(split_views); i; i = g_list_next(i))
	vs_view_free(i->data);
    for (i = g_list_first(new_views); i; i = g_list_next(i))
	vs_view_free(i->data);
    g_list_free(old_views);
    g_list_free(split_views);
    g_list_free(new_views);
    addr_set_free(targets);
}

void vs_backend_shm_merge()
{
    GList *old_views = NULL;
    GList *merge_views = NULL;
    addr_set_t *targets;
    const addr_set_iter_t *addr_i;
    GList *i;
    int n, n_merge;
    vs_view_t *view;
    vs_view_t *reg_view, *trans_view = NULL;
    vs_backend_shm_memb_t *memb;
    targets = addr_set_new();
     
    for (i = g_list_first(shm_be->memb); i; i = g_list_next(i)) {
	memb = i->data;
	/* Others will be gone at the moment of delivery */
	if (memb->state == CONNECTING || memb->state == CONNECTED)
	    addr_set_insert(targets, memb->id);
    }
    for (addr_i = addr_set_first(targets); addr_i; 
	 addr_i = addr_set_next(addr_i)) {
	if (!(view = get_last_view_by_addr(addr_cast(addr_i)))) {
	    g_assert(!"Not possible");
	}
	if (!g_list_find_custom(old_views, view, &view_cmp))
	    old_views = g_list_insert_sorted(old_views, view, &view_cmp);
	else
	    vs_view_free(view);
    }
     
    printf("Old views:\n");
    for (i = g_list_first(old_views); i; i = g_list_next(i)) {
	print_view(i->data);
	printf("\n");
    }

    if (g_list_length(old_views) > 1) {
	n_merge = 1 + rand()%g_list_length(old_views);
	for (n = 0; n < n_merge; n++) {
	    view = g_list_nth_data(old_views, rand()%g_list_length(old_views));
	    old_views = g_list_remove(old_views, view);
	    merge_views = g_list_append(merge_views, view);
	}
    }
     
    printf("Merge views:\n");
    for (i = g_list_first(merge_views); i; i = g_list_next(i)) {
	print_view(i->data);
	printf("\n");
    }

    i = g_list_first(shm_be->memb);     
    memb = i ? i->data : NULL;
     
    for (i = g_list_first(merge_views); i; i = g_list_next(i)) {
	view = i->data;
	trans_view = vs_view_new(VS_VIEW_TRANS, vs_view_get_id(view));
	for (addr_i = addr_set_first(vs_view_get_addr(view)); addr_i;
	     addr_i = addr_set_next(addr_i)) {
	    vs_view_addr_insert(trans_view, addr_cast(addr_i));
	}
	vs_backend_shm_send_conf((vs_backend_t *)memb, trans_view);
	vs_view_free(trans_view);
    }
     
    if (trans_view) {
	reg_view = vs_view_new(VS_VIEW_REG, ++shm_be->view_id);
	for (i = g_list_first(merge_views); i; i = g_list_next(i)) {
	    view = i->data;
	    for (addr_i = addr_set_first(vs_view_get_addr(view)); addr_i;
		 addr_i = addr_set_next(addr_i)) {
		vs_view_addr_insert(reg_view, addr_cast(addr_i));
	    }
	}
	vs_backend_shm_send_conf((vs_backend_t *)memb, reg_view);
	printf("Merged view: ");
	print_view(reg_view);
	printf("\n");
	vs_view_free(reg_view);
    }     
     
    for (i = g_list_first(old_views); i; i = g_list_next(i))
	vs_view_free(i->data);
    for (i = g_list_first(merge_views); i; i = g_list_next(i))
	vs_view_free(i->data);
    g_list_free(old_views);
    g_list_free(merge_views);
    addr_set_free(targets);

}

static int deliver_view(vs_backend_shm_memb_t *memb, 
			const vs_msg_t *msg,
			const vs_view_t *view)
{
    msg_t *tmp_msg = NULL;
    vs_msg_t *view_msg = NULL;
    vs_view_t *trans_view = NULL;
    char *view_buf = NULL;
    size_t view_buf_len;
    vs_msg_t *recv_cb_msg;
     
    if (memb->state == CONNECTING && vs_view_get_type(view) == VS_VIEW_REG && 
	vs_view_find_addr(view, memb->id)) {
	trans_view = vs_view_new(VS_VIEW_TRANS, 0);
	view_buf_len = vs_view_get_len(trans_view);
	view_buf = g_malloc(view_buf_len);
	g_assert(vs_view_write(trans_view, view_buf, view_buf_len, 0));
	tmp_msg = msg_new();
	msg_set_payload(tmp_msg, view_buf, view_buf_len);
	view_msg = vs_msg_new(VS_MSG_TRANS_CONF, vs_msg_get_source(msg), 
			      vs_view_get_id(trans_view), 0,
			      VS_MSG_SAFETY_SAFE, tmp_msg, NULL);
	recv_cb_msg = vs_msg_copy(view_msg);
	memb->recv_cb(memb->user_context, recv_cb_msg);
	vs_msg_free(recv_cb_msg);
	memb->state = CONNECTED;
	memb->recv_cb(memb->user_context, msg);
    } else if (memb->state == DISCONNECTING && 
	       vs_view_get_type(view) == VS_VIEW_TRANS && 
	       !vs_view_find_addr(view, memb->id) &&
	       vs_msg_get_source(msg) == memb->id) {
	trans_view = vs_view_new(VS_VIEW_TRANS, vs_view_get_id(view));
	view_buf_len = vs_view_get_len(trans_view);
	view_buf = g_malloc(view_buf_len);
	g_assert(vs_view_write(trans_view, view_buf, view_buf_len, 0));
	tmp_msg = msg_new();
	msg_set_payload(tmp_msg, view_buf, view_buf_len);
	view_msg = vs_msg_new(VS_MSG_TRANS_CONF, 0, 
			      vs_view_get_id(trans_view), 0,
			      VS_MSG_SAFETY_SAFE, tmp_msg, NULL);
	recv_cb_msg = vs_msg_copy(view_msg);
	memb->state = DISCONNECTED;
	memb->recv_cb(memb->user_context, recv_cb_msg);
	vs_msg_free(recv_cb_msg);
	memb->id = ADDR_INVALID;
    } else if (memb->state == CONNECTED || memb->state == DISCONNECTING) {
	if (vs_view_find_addr(view, memb->id))
	    memb->recv_cb(memb->user_context, msg);
    } 
     
    vs_msg_free(view_msg);
    msg_free(tmp_msg);
    g_free(view_buf);
    vs_view_free(trans_view);     
     
    return 0;
}

static int vs_backend_shm_sched(vs_backend_t *be, poll_t *poll, int fd)
{
    GList *i, *i_next;
    vs_backend_shm_memb_t *memb;
    vs_backend_shm_msg_t *shm_msg;
    vs_backend_shm_view_t *be_view;
    vs_backend_shm_view_t *source_view;
    vs_backend_shm_view_t *new_be_view;
    msg_t *msgbuf;
    vs_msg_t *msg;
    vs_view_t *view = NULL;
    bool deliver = true;

     
    shm_msg = NULL;
    if (g_queue_peek_head(shm_be->msg_queue) && 
	g_queue_peek_head(shm_be->conf_queue)) {
	if (rand()%11)
	    shm_msg = g_queue_pop_head(shm_be->msg_queue);
	else 
	    shm_msg = g_queue_pop_head(shm_be->conf_queue);
    } else if (g_queue_peek_head(shm_be->msg_queue)) {
	shm_msg = g_queue_pop_head(shm_be->msg_queue);
    } else if (g_queue_peek_head(shm_be->conf_queue)) {
	shm_msg = g_queue_pop_head(shm_be->conf_queue);
    }
     
    if (!shm_msg) {
	return 0;
    }
     
    msgbuf = msg_read(shm_msg->msgbuf, shm_msg->msgbuf_len);
    msg = vs_msg_read(msgbuf);
    if (!msg) {
	g_free(shm_msg->msgbuf);
	g_free(shm_msg);	  
	msg_free(msgbuf);
	return 0;
    }
     
     
    if (vs_msg_is_conf(msg)) {
	if (!vs_view_read(vs_msg_get_payload(msg), 
			  vs_msg_get_payload_len(msg), 0, &view))
	    return EPROTO;
	if (!(be_view = get_preceeding_view(view))) {
	    if (vs_view_get_type(view) == VS_VIEW_TRANS) {
		if (vs_view_get_size(view) > 0) {
		    print_view(view);
		    printf("\n");
		}
		g_assert(vs_view_get_size(view) == 0);
		g_free(shm_msg->msgbuf);
		g_free(shm_msg);
		vs_msg_free(msg);
		msg_free(msgbuf);
		vs_view_free(view);
		return 1;
	    } else {
		be_view = vs_backend_shm_view_new(vs_view_get_id(view));
		be_view->reg_view = view;
		shm_be->views = g_list_append(shm_be->views, be_view);
	    }
	} else if (vs_view_get_type(view) == VS_VIEW_TRANS) {
	    if (be_view->trans_view) {
		new_be_view = vs_backend_shm_view_new(vs_view_get_id(view));
		new_be_view->trans_view = view;
		new_be_view->reg_view = vs_view_copy(be_view->reg_view);
		shm_be->views = g_list_append(shm_be->views, new_be_view);
		be_view = new_be_view;
	    } else {
		be_view->trans_view = view;
	    }
	} else {
	    new_be_view = vs_backend_shm_view_new(vs_view_get_id(view));
	    new_be_view->reg_view = view;
	    shm_be->views = g_list_remove(shm_be->views, be_view);
	    shm_be->views = g_list_append(shm_be->views, new_be_view);
	    vs_backend_shm_view_free(be_view);
	    be_view = new_be_view;
	}
    } else {
	if (!(source_view = get_current_view_by_addr(vs_msg_get_source(msg)))) {
	    fprintf(stderr, "Dropping message from %i, source not in any view\n",
		    vs_msg_get_source(msg));
	    g_free(shm_msg->msgbuf);
	    g_free(shm_msg);
	    msg_free(msgbuf);
	    vs_msg_free(msg);
	    return 1;
	}
    } 
     

     
    for (i = g_list_first(shm_be->memb); deliver && i; i = i_next) {
	i_next = g_list_next(i);
	memb = i->data;
	if ((memb->state == CONNECTED || memb->state == DISCONNECTING) &&
	    vs_msg_is_user(msg) &&
	    (be_view = get_current_view_by_addr(memb->id)) &&
	    vs_view_find_addr(be_view->reg_view, vs_msg_get_source(msg)) &&
	    (vs_view_get_id(be_view->reg_view) == 
	     vs_view_get_id(source_view->reg_view)) &&
	    memb->recv_cb) {
	    memb->recv_cb(memb->user_context, msg);
	} else if (vs_msg_is_conf(msg) && 
		   vs_view_find_addr(be_view->reg_view, memb->id) && 
		   memb->recv_cb) {
	    deliver_view(memb, msg, view);
	    if (memb->state == DISCONNECTED && memb->id == ADDR_INVALID)
		shm_be->memb = g_list_remove(shm_be->memb, memb);
	}
    }
     
    if (be_view->trans_view && vs_view_get_size(be_view->trans_view) == 0) {
	/* No-one should care about this view anymore... */
	shm_be->views = g_list_remove(shm_be->views, be_view);
	vs_backend_shm_view_free(be_view);
    }
     
     
    if (shm_msg) {
	g_free(shm_msg->msgbuf);
	g_free(shm_msg);
	msg_free(msgbuf);
	vs_msg_free(msg);
	return 1;
    }
    return 0;
}


/*
 * 'Shared memory' backend. Not really SHM yet, but could be 
 * improved. Used for unit testing.
 */
vs_backend_t *vs_backend_shm_new(const char *conf,
				 void *user_context, 
				 void (*send_cb)(void *, const vs_msg_t *))
{
    vs_backend_shm_memb_t *memb;
    if (!shm_be) {
	shm_be = g_malloc(sizeof(vs_backend_shm_t));
	shm_be->msg_queue = g_queue_new();
	shm_be->conf_queue = g_queue_new();
	shm_be->memb = NULL;
	shm_be->refcnt = 1;
	shm_be->last_id = 0;
	shm_be->view_id = 0;
	shm_be->views = NULL;
	shm_be->user_context = user_context;
	shm_be->send_cb = send_cb;
    } else {
	shm_be->refcnt++;
    }

    memb = g_malloc(sizeof(vs_backend_shm_memb_t));
    memb->be.free = &vs_backend_shm_free;
    memb->be.connect = &vs_backend_shm_connect;
    memb->be.disconnect = &vs_backend_shm_disconnect;
    memb->be.send = &vs_backend_shm_send;
    memb->be.sched = &vs_backend_shm_sched;
    memb->be.set_recv_callback = &vs_backend_shm_set_recv_callback;
    memb->be.get_self_addr = &vs_backend_shm_get_self_addr;
    memb->be.get_view_id = &vs_backend_shm_get_view_id;
    memb->be.get_fd = &vs_backend_shm_get_fd;
    memb->id = ADDR_INVALID;
    memb->user_context = NULL;
    memb->recv_cb = NULL;
    memb->shm_loc = shm_be;
    memb->state = DISCONNECTED;
    return (vs_backend_t *) memb;
}

#endif /* 0 */
