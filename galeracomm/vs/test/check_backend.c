#include "vs_backend.h"
#include "vs_backend_shm.h"
#include "gcomm/vs_msg.h"
#include "gcomm/vs_view.h"
#include "gcomm/msg.h"
#include "gcomm/addr.h"

#include <check.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Process context
 */
typedef struct proc_context_ {
     GQueue *sent;
} proc_context_t; 

/*
 * Backend context
 */
typedef enum {
     BE_DISCONNECTED,
     BE_CONNECTED
} be_state_e;


typedef struct be_context_ {
     addr_t last_addr;
     be_state_e state;
     vs_backend_t *be;
     GQueue *received;
     GQueue *views;
} be_context_t;



/*
 * Message helpers
 */

static void print_msg(const vs_msg_t *msg)
{
     vs_view_t *view;
     const addr_set_iter_t *i;
     printf("MSG: type=%i source=%i seq=%llu ", 
            vs_msg_get_type(msg), 
            vs_msg_get_source(msg), 
            vs_msg_get_seq(msg));
     if (vs_msg_get_type(msg) == VS_MSG_REG_CONF || 
	 vs_msg_get_type(msg) == VS_MSG_TRANS_CONF) {
          fail_unless(vs_view_read(
                           vs_msg_get_payload(msg),
                           vs_msg_get_payload_len(msg),
                           0, &view));
          printf("VIEW (%s,%i): ", 
                 vs_view_get_type(view) == VS_VIEW_TRANS ? "TRANS" : "REG", 
                 vs_view_get_id(view));
	  for (i = addr_set_first(vs_view_get_addr(view)); i; 
	       i = addr_set_next(i))
		    printf("%i ", addr_cast(i));
	  vs_view_free(view);
     }

}

static void print_view(const vs_view_t *view)
{
     const addr_set_iter_t *i;
     printf("type = %s id = %i memb = ", vs_view_get_type(view) == VS_VIEW_TRANS ? "TRANS" : "REG", vs_view_get_id(view));
     for (i = addr_set_first(vs_view_get_addr(view)); i; 
          i = addr_set_next(i))
          printf("%i ", addr_cast(i));
}


static bool msg_equals(const vs_msg_t *a, const vs_msg_t *b)
{
     if (vs_msg_get_type(a) != vs_msg_get_type(b))
          return false;
     if (vs_msg_get_source(a) != vs_msg_get_source(b))
          return false;
     if (vs_msg_get_seq(a) != vs_msg_get_seq(b))
          return false;
     if (vs_msg_get_safety(a) != vs_msg_get_safety(b))
          return false;

     if (vs_msg_get_payload_len(a) != 
         vs_msg_get_payload_len(b))
          return false;

     if (memcmp(vs_msg_get_payload(a), 
                vs_msg_get_payload(b), 
                vs_msg_get_payload_len(a)) != 0)
          return false;

     return true;
}

static bool last_received_msg_equals(const be_context_t *ctx, 
                                     const vs_msg_t *msg)
{
     const vs_msg_t *msg2;
     vs_msg_t *a;
     vs_msg_t *b;
     bool ret;
     GList *i;
     if (!((i = g_queue_peek_tail_link(ctx->received)) && i->data))
          return false;
     
     msg2 = i->data;
     a = vs_msg_copy(msg);
     b = vs_msg_copy(msg2);
     ret = msg_equals(a, b);
     vs_msg_free(a);
     vs_msg_free(b);
     return ret;
}

static bool last_received_view_equals(be_context_t *ctx, const vs_view_t *view)
{
     bool ret;
     vs_msg_t *msg;
     vs_view_t *msg_view;
     GList *i;
     
     if (!((i = g_queue_peek_tail_link(ctx->received)) && i->data))
          return false;
     msg = i->data;
     if (vs_msg_get_type(msg) != VS_MSG_TRANS_CONF &&
	 vs_msg_get_type(msg) != VS_MSG_REG_CONF)
          return false;
     if (!vs_view_read(vs_msg_get_payload(msg),
                       vs_msg_get_payload_len(msg), 0, &msg_view))
          return false;
     
     /* Cheating here... ctx should keep track about it's state */
     if (vs_view_get_type(msg_view) == VS_VIEW_TRANS && 
         vs_view_get_id(msg_view) == 0)
          return true;
     ret = vs_view_equal(view, msg_view);
     vs_view_free(msg_view);
     return ret;
}



/*
 * Process context
 */
static proc_context_t *proc_context_new()
{
     proc_context_t *ctx;
     ctx = g_malloc(sizeof(proc_context_t));
     ctx->sent = g_queue_new();
     return ctx;
}


static void proc_context_free(proc_context_t *ctx)
{
     GList *i;
     for (i = g_queue_peek_head_link(ctx->sent); i; i = g_list_next(i))
          vs_msg_free(i->data);
     g_queue_free(ctx->sent);     
     ctx->sent = NULL;
     g_free(ctx);
}


/*
 * Backend context
 */

static be_context_t *be_context_new(vs_backend_t *be)
{
     be_context_t *ret = g_malloc(sizeof(be_context_t));
     ret->state = BE_DISCONNECTED;
     ret->last_addr = ADDR_INVALID;
     ret->be = be;
     ret->received = g_queue_new();
     ret->views = g_queue_new();
     return ret;
}

static void be_context_free(be_context_t *ctx)
{
     GList *i;
     g_assert(ctx);
     if (ctx) {
          for (i = g_queue_peek_head_link(ctx->received);
               i; i = g_list_next(i))
               vs_msg_free(i->data);
          g_queue_free(ctx->received);
          for (i = g_queue_peek_head_link(ctx->views);
               i; i = g_list_next(i))
               vs_msg_free(i->data);
          g_queue_free(ctx->views);
          vs_backend_free(ctx->be);
          g_free(ctx);
     }     
}



static void send_some(be_context_t **ctx, int from, int to)
{
     int i, r, r_max;
     int idx;
     char buf[128];
     vs_msg_t *msg;
     msg_t *msgbuf;

     if (from > to)
          return;
     
     r_max = 128 + rand()%128;
     for (r = 0; r < r_max; r++) {

          idx = from + rand()%(to - from + 1);
          fail_unless(vs_backend_sched(ctx[idx]->be, NULL, -1) == 0);
          
          for (i = 0; i < 128; i++) {
               buf[i] = rand();
          }
	  
	  msgbuf = msg_new();
	  msg_set_payload(msgbuf, buf, rand()%128);
	  
          msg = vs_msg_new(VS_MSG_DATA, 
			   vs_backend_get_self_addr(ctx[idx]->be),
			   vs_backend_get_view_id(ctx[idx]->be),
			   0,
			   VS_MSG_SAFETY_SAFE,
			   msgbuf, NULL);
          fail_if(vs_backend_send(ctx[idx]->be, msg));
          vs_msg_free(msg);
	  msg_free(msgbuf);
          fail_unless(vs_backend_sched(ctx[idx]->be, NULL, -1) == 1);
     }

     r_max = 128 + rand()%128;
     for (r = 0; r < r_max; r++) {
          idx = from + rand()%(to - from + 1);
          
          for (i = 0; i < 128; i++) {
               buf[i] = rand();
          }
          
	  msgbuf = msg_new();
	  msg_set_payload(msgbuf, buf, rand()%128);
          msg = vs_msg_new(VS_MSG_DATA, 
			   vs_backend_get_self_addr(ctx[idx]->be),
			   vs_backend_get_view_id(ctx[idx]->be),
			   0,
			   VS_MSG_SAFETY_SAFE,
			   msgbuf, NULL);
          
          fail_if(vs_backend_send(ctx[idx]->be, msg));
          vs_msg_free(msg);
	  msg_free(msgbuf);
     }
     while (vs_backend_sched(ctx[from]->be, NULL, -1) == 1);
}

static void send_some_more(GList *ctx_list)
{
     static seq_t seq = 0;
     int i, n, j, b, ll;
     be_context_t *ctx;
     vs_msg_t *msg;
     msg_t *msgbuf;
     char buf[128];
     int err;
     ll = g_list_length(ctx_list);
     if (!ll)
          return;
     n = rand()%7;
     for (i = 0; i < n; i++) {
          ctx = g_list_nth(ctx_list, rand()%ll)->data;
          if (ctx->state == BE_CONNECTED) {
	       /* printf("BE(%i) sending\n", vs_backend_get_self_addr(ctx->be)); */
               b = rand()%128;
               for (j = 0; j < b; j++) {
                    buf[j] = rand();
               }
	       msgbuf = msg_new();
	       msg_set_payload(msgbuf, buf, b);
               msg = vs_msg_new(VS_MSG_DATA, 
				vs_backend_get_self_addr(ctx->be),
				vs_backend_get_view_id(ctx->be),
				seq++, 
				VS_MSG_SAFETY_SAFE, msgbuf, NULL);
               if ((err = vs_backend_send(ctx->be, msg)))
		    printf("Send failed: %s\n", strerror(err));
               vs_msg_free(msg);
	       msg_free(msgbuf);
          }
     }
}



static void be_send_cb(void *user_context, const vs_msg_t *msg)
{
     proc_context_t *ctx = (proc_context_t *) user_context; 
     g_queue_push_tail(ctx->sent, vs_msg_copy(msg));
}

static int data_msg_received = 0;

static void be_recv_cb(void *user_context, const vs_msg_t *msg)
{
     be_context_t *ctx = (be_context_t *) user_context; 
     vs_view_t *view = NULL;
     vs_view_t *trans_view;
     GList *i;
     vs_msg_t *last_msg;
     
     /* Data messages should not be delivered until reg conf is reached */
     if (ctx->state == BE_DISCONNECTED)
          fail_if(vs_msg_get_type(msg) != VS_MSG_TRANS_CONF &&
		  vs_msg_get_type(msg) != VS_MSG_REG_CONF);
     
     if (vs_msg_get_type(msg) != VS_MSG_DATA) {
	  printf("BE(%i): ", vs_backend_get_self_addr(ctx->be));
	  print_msg(msg);
	  printf("\n");
     } 
     i = g_queue_peek_tail_link(ctx->received);
     g_queue_push_tail(ctx->received, vs_msg_copy(msg));
     
     /* If message is config message, read view from message */
     if (vs_msg_get_type(msg) == VS_MSG_REG_CONF ||
	 vs_msg_get_type(msg) == VS_MSG_TRANS_CONF) {
          g_queue_push_tail(ctx->views, vs_msg_copy(msg));
          fail_unless(vs_view_read(vs_msg_get_payload(msg),
                                   vs_msg_get_payload_len(msg), 
                                   0, &view));	  
     }
     
     if (ctx->state == BE_DISCONNECTED && 
	 vs_msg_get_type(msg) == VS_MSG_TRANS_CONF) {
	  fail_unless(vs_view_get_type(view) == VS_VIEW_TRANS);
	  /* First view id must be 0 */
	  fail_unless(vs_view_get_id(view) == 0);
     } else if (ctx->state == BE_DISCONNECTED && 
		vs_msg_get_type(msg) == VS_MSG_REG_CONF) {
	  fail_unless(vs_view_get_type(view) == VS_VIEW_REG);
	  fail_unless(!!i);
	  /* Previous message must be empty transitional configuration with
	   * id = 0 */
	  last_msg = i->data;
	  fail_unless(vs_view_read(vs_msg_get_payload(last_msg),
				   vs_msg_get_payload_len(last_msg),
				   0, &trans_view));
	  fail_unless(vs_view_get_type(trans_view) == VS_VIEW_TRANS);
	  fail_unless(vs_view_get_size(trans_view) == 0);
	  fail_unless(vs_view_get_id(trans_view) == 0);
	  vs_view_free(trans_view);
	  
	  /* Reg conf must contain self address */
	  fail_unless(vs_view_find_addr(view, vs_backend_get_self_addr(ctx->be)));
	  
	  /* All ok, connected */
	  ctx->last_addr = vs_backend_get_self_addr(ctx->be);
	  ctx->state = BE_CONNECTED;
     } else if (ctx->state == BE_CONNECTED && 
		vs_msg_get_type(msg) == VS_MSG_TRANS_CONF) {
	  fail_unless(vs_view_get_type(view) == VS_VIEW_TRANS);
	  if (vs_view_get_size(view) == 0) {
               ctx->state = BE_DISCONNECTED;
          }
     } else if (ctx->state == BE_CONNECTED && 
		vs_msg_get_type(msg) == VS_MSG_REG_CONF) {
          fail_unless(vs_view_get_type(view) == VS_VIEW_REG);
	  fail_unless(vs_view_find_addr(view, vs_backend_get_self_addr(ctx->be)));
     } else {
	  /* Just a data message */
	  data_msg_received++;
     }
     vs_view_free(view);
}

/*
 * Complete view is set of messages 
 * [reg_view, [reg_msgs], trans_view, [trans_msgs], reg_view)
 */
typedef struct complete_view_ {
     vs_view_t *reg_view;
     GQueue *reg_msgs;
     vs_view_t *trans_view;
     GQueue *trans_msgs;
} complete_view_t;

static complete_view_t *complete_view_new()
{
     complete_view_t *ret;
     ret = g_malloc(sizeof(complete_view_t));

     ret->reg_view = NULL;
     ret->reg_msgs = g_queue_new();
     ret->trans_view = NULL;
     ret->trans_msgs = g_queue_new();
     return ret;
}

static void complete_view_free(complete_view_t *v)
{
     GList *i;
     vs_view_free(v->reg_view);
     vs_view_free(v->trans_view);
     for (i = g_queue_peek_head_link(v->reg_msgs); i; i = g_list_next(i)) {
	  vs_msg_free(i->data);
     }
     g_queue_free(v->reg_msgs);
     for (i = g_queue_peek_head_link(v->trans_msgs); i; i = g_list_next(i)) {
	  vs_msg_free(i->data);
     }
     g_queue_free(v->trans_msgs);
     g_free(v);
}

int complete_view_cmp(const void *a, const void *b)
{
     const complete_view_t *av = (const complete_view_t *) a;
     const complete_view_t *bv = (const complete_view_t *) b;
     addr_t aaddr;
     addr_t baddr;
     addr_set_t *intersection;
     if (vs_view_get_id(av->reg_view) != vs_view_get_id(bv->reg_view)) {
	  return vs_view_get_id(av->reg_view) < vs_view_get_id(bv->reg_view) ?
	       -1 : 1;
     }
     
     fail_unless(addr_set_equal(vs_view_get_addr(av->reg_view),
				vs_view_get_addr(bv->reg_view)));
     
     aaddr = addr_cast(addr_set_first(vs_view_get_addr(av->trans_view)));
     baddr = addr_cast(addr_set_first(vs_view_get_addr(bv->trans_view)));
     if (aaddr != baddr) {
	  intersection = addr_set_intersection(
	       vs_view_get_addr(av->trans_view), 
	       vs_view_get_addr(bv->trans_view));
	  fail_unless(addr_set_size(intersection) == 0);
	  addr_set_free(intersection);
	  return aaddr < baddr ? -1 : 1;
     } else {
	  fail_unless(addr_set_equal(
			   vs_view_get_addr(av->trans_view), 
			   vs_view_get_addr(bv->trans_view)));
	  return 0;
     }
     
}


static complete_view_t *extract_complete_view(GQueue *q)
{
     GList *i, *i_next;
     GList *reg_msg = NULL;
     GList *trans_msg = NULL;
     bool found_next_reg = false;
     vs_msg_t *msg;
     vs_view_t *view;
     vs_view_t *trans_view;
     complete_view_t *ret = NULL;
     for (i = g_queue_peek_head_link(q); i && found_next_reg == false; 
	  i = i_next) {
	  i_next = g_list_next(i);
	  msg = i->data;
	  switch (vs_msg_get_type(msg)) {
	  case VS_MSG_TRANS_CONF:
	       fail_unless(vs_view_read(vs_msg_get_payload(msg),
					vs_msg_get_payload_len(msg), 0, 
					&trans_view));
	       fail_unless(vs_view_get_type(trans_view) == VS_VIEW_TRANS);
	       if (reg_msg == NULL) {
		    /* Leading trans confs must always be empty, 
		     * purge em */
		    if (vs_view_get_size(trans_view)) {
			 print_view(trans_view);
			 printf("\n");
		    }
		    fail_unless(vs_view_get_size(trans_view) == 0);
		    g_queue_delete_link(q, i);
		    vs_msg_free(msg);
	       } else {
		    trans_msg = i;
		    fail_unless(vs_view_read(
				     vs_msg_get_payload(reg_msg->data), 
				     vs_msg_get_payload_len(reg_msg->data),
				     0, &view));
		    if (!addr_set_is_subset(vs_view_get_addr(trans_view),
					    vs_view_get_addr(view))) {
			 printf("Trans view: ");
			 print_view(trans_view);
			 printf("\nReg view: ");
			 print_view(view);
			 printf("\n");
		    }
		    fail_unless(addr_set_is_subset(vs_view_get_addr(trans_view),
						   vs_view_get_addr(view)));
		    if (vs_view_get_size(trans_view) == 0) {
			 /* Everyone got away... */
			 found_next_reg = true;
		    }
		    vs_view_free(view);
	       }
	       vs_view_free(trans_view);

	       break;
	       
	  case VS_MSG_REG_CONF:
	       fail_unless(!!reg_msg || !trans_msg);
	       if (reg_msg == NULL) {
		    reg_msg = i;
	       } else {
		    fail_unless(vs_view_read(
				     vs_msg_get_payload(i->data), 
				     vs_msg_get_payload_len(i->data),
				     0, &view));
		    fail_unless(vs_view_read(
				     vs_msg_get_payload(trans_msg->data), 
				     vs_msg_get_payload_len(trans_msg->data),
				     0, &trans_view));
		    fail_unless(addr_set_is_subset(
				     vs_view_get_addr(trans_view),
				     vs_view_get_addr(view)));
		    vs_view_free(view);
		    vs_view_free(trans_view);
		    found_next_reg = true;
	       }
	       break;
	       
	  case VS_MSG_DATA:
	       break;
	  default:
	       /* */
	       fail_unless(!"WTF?");
	  }

     }


     if (found_next_reg) {
	  ret = complete_view_new();

	  /* Read regular view */
	  msg = reg_msg->data;
	  fail_unless(vs_view_read(vs_msg_get_payload(msg), 
				   vs_msg_get_payload_len(msg), 0, 
				   &ret->reg_view));
	  g_queue_delete_link(q, reg_msg);
	  vs_msg_free(msg);
	  /* Read reg messages */
	  for (i = g_queue_peek_head_link(q); i && i != trans_msg; i = i_next) {
	       i_next = g_list_next(i);
	       g_queue_push_tail(ret->reg_msgs, i->data);
	       g_queue_delete_link(q, i);
	  }
	  
	  /* Read trans view */
	  msg = trans_msg->data;
	  fail_unless(vs_view_read(vs_msg_get_payload(msg), 
				   vs_msg_get_payload_len(msg), 0, 
				   &ret->trans_view));
	  g_queue_delete_link(q, trans_msg);
	  vs_msg_free(msg);

	  /* Read trans messages */
	  for (i = g_queue_peek_head_link(q); i; i = i_next) {
	       i_next = g_list_next(i);
	       msg = i->data;
	       if (vs_msg_get_type(msg) == VS_MSG_DATA) {
		    g_queue_push_tail(ret->trans_msgs, i->data);
		    g_queue_delete_link(q, i);
	       } else {
		    fail_unless(vs_msg_get_type(msg) == VS_MSG_REG_CONF ||
				vs_msg_get_type(msg) == VS_MSG_TRANS_CONF);
		    fail_unless(vs_view_read(
				     vs_msg_get_payload(msg), 
				     vs_msg_get_payload_len(msg), 0,
				     &view));
		    fail_unless(vs_view_get_type(view) == VS_VIEW_REG ||
				(vs_view_get_id(view) == 0 && 
				 vs_view_get_size(view) == 0));
		    vs_view_free(view);
		    break;
	       }
	  }
     }
     if (ret)
	  printf("Complete view %i\n", vs_view_get_id(ret->reg_view));
     return ret;
}

static void verify_be_view(const complete_view_t *cv,
			   be_context_t *ctx)
{
     GList *i, *i_next, *j, *reg_iter = NULL;
     vs_view_t *trans_view = NULL;
     vs_view_t *reg_view = NULL;
     vs_view_t *view;
     vs_msg_t *msg;

     if (g_queue_peek_head_link(ctx->views) == NULL)
	  return;

     /* printf("BE(%i): \n", ctx->last_addr); */

     for (i = g_queue_peek_head_link(ctx->views); 
	  i && trans_view == NULL; i = i_next) {
	  i_next = g_list_next(i);
	  msg = i->data;
	  fail_unless(vs_msg_get_type(msg) == VS_MSG_TRANS_CONF ||
		      vs_msg_get_type(msg) == VS_MSG_REG_CONF);
	  fail_unless(vs_view_read(
			   vs_msg_get_payload(msg),
			   vs_msg_get_payload_len(msg), 0,
			   &view));

	  if (vs_msg_get_type(msg) == VS_MSG_TRANS_CONF) {
	       if (reg_view == NULL) {
		    fail_unless(vs_view_get_size(view) == 0);
		    g_queue_delete_link(ctx->views, i);
		    vs_msg_free(msg);
		    vs_view_free(view);
	       } else {
		    if (vs_view_get_id(view) == 
			vs_view_get_id(cv->trans_view) &&
			addr_set_equal(vs_view_get_addr(view), 
				       vs_view_get_addr(cv->trans_view))) {
			 trans_view = view;
			 vs_msg_free(reg_iter->data);
			 g_queue_delete_link(ctx->views, reg_iter);
			 vs_msg_free(msg);
			 g_queue_delete_link(ctx->views, i);
		    } else {
			 vs_view_free(view);
			 vs_view_free(reg_view);
			 return;
		    }
	       }
	  } else if (vs_msg_get_type(msg) == VS_MSG_REG_CONF) {
	       if (vs_view_get_id(view) == vs_view_get_id(cv->reg_view)) {
		    fail_unless(!reg_view);
		    fail_unless(addr_set_equal(vs_view_get_addr(view), 
					       vs_view_get_addr(cv->reg_view)));
		    reg_view = view;
		    reg_iter = i;
	       } else {
		    vs_view_free(view);
		    return;
	       }
	  } else {
	       fail_unless(!"wtf?");
	  }
     }
     
     fail_unless((reg_view && trans_view), "Reg view = %p trans view = %p",
		 reg_view, trans_view);
     vs_view_free(reg_view);
     vs_view_free(trans_view);
     reg_view = trans_view = NULL;

     for (i = g_queue_peek_head_link(ctx->received); i; i = i_next) {
	  i_next = g_list_next(i);
	  msg = i->data;
	  switch (vs_msg_get_type(msg)) {
	  case VS_MSG_TRANS_CONF:
	       fail_unless(vs_view_read(
				vs_msg_get_payload(msg),
				vs_msg_get_payload_len(msg), 0, &view));
	       fail_unless(vs_view_get_type(view) == VS_VIEW_TRANS);
	       if (reg_view == NULL) {
		    fail_unless(vs_view_get_size(view) == 0);
		    vs_view_free(view);
	       } else {
		    trans_view = view;
	       }
	       g_queue_delete_link(ctx->received, i);
	       vs_msg_free(msg);
	       break;
	       
	  case VS_MSG_REG_CONF:
	       fail_unless(vs_view_read(
				vs_msg_get_payload(msg),
				vs_msg_get_payload_len(msg), 0, &view));
	       fail_unless(vs_view_get_type(view) == VS_VIEW_REG);
	       if (vs_view_get_id(view) != vs_view_get_id(cv->reg_view)) {
		    vs_view_free(view);
		    return;
	       }
	       fail_if(!!trans_view);
	       reg_view = view;
	       g_queue_delete_link(ctx->received, i);
	       vs_msg_free(msg);
	       break;
	       
	  case VS_MSG_DATA:
	       if (reg_view) {
		    for (j = g_queue_peek_head_link(cv->reg_msgs); j && i;
			 j = g_list_next(j)) {
			 if (!msg_equals(j->data, i->data)) {
			      printf("Send Q Msg: \n");
			      print_msg(j->data);
			      printf("\nRecv Q Msg on BE(%i) (don't trust be address...): \n", 
				     ctx->last_addr);
			      print_msg(i->data);
			      printf("\n");
			 }
			 fail_unless(msg_equals(j->data, i->data));
			 vs_msg_free(i->data);
			 g_queue_delete_link(ctx->received, i);
			 i = i_next;
			 i_next = g_list_next(i);
		    }
	       } else if (trans_view) {
		    for (j = g_queue_peek_head_link(cv->trans_msgs); j;
			 j = g_list_next(j)) {
			 fail_unless(msg_equals(j->data, i->data));
			 vs_msg_free(i->data);
			 g_queue_delete_link(ctx->received, i);
			 i = i_next;
			 i_next = g_list_next(i);
		    }		    
	       } else {
		    fail_unless(!"wtf?");
	       }
	       break;
	       
	  default:
	       fail_unless(!"wtf?");
	       
	  }
     }

}

static void verify_view(const complete_view_t *cv, 
			    be_context_t **ctx, int n)
{
     int i;
     GList *j;
     vs_msg_t *msg;
     if (!addr_set_is_subset(vs_view_get_addr(cv->trans_view),
			     vs_view_get_addr(cv->reg_view))) {
	  printf("Views:\n");
	  print_view(cv->trans_view);
	  printf("\n");
	  print_view(cv->reg_view);
	  printf("\n");
     }
     fail_unless(addr_set_is_subset(
		      vs_view_get_addr(cv->trans_view),
		      vs_view_get_addr(cv->reg_view)));
     fail_unless(vs_view_get_id(cv->trans_view) == vs_view_get_id(cv->reg_view));
     fail_unless(vs_view_get_type(cv->trans_view) == VS_VIEW_TRANS);
     fail_unless(vs_view_get_type(cv->reg_view) == VS_VIEW_REG);
     
     for (j = g_queue_peek_head_link(cv->reg_msgs); j; 
	  j = g_list_next(j)) {
	  msg = j->data;
	  if (!vs_view_find_addr(cv->reg_view, vs_msg_get_source(msg))) {
	       printf("View: ");
	       print_view(cv->reg_view);
	       printf("\nMessage: ");
	       print_msg(msg);
	       printf("\n");
	  }
	  fail_unless(vs_view_find_addr(cv->reg_view,
					vs_msg_get_source(msg)));
     }
     for (j = g_queue_peek_head_link(cv->trans_msgs); j; 
	  j = g_list_next(j)) {
	  msg = j->data;
	  fail_unless(vs_view_find_addr(cv->reg_view,
					vs_msg_get_source(msg)));
     }
     
     for (i = 0; i < n; i++) {
	  verify_be_view(cv, ctx[i]);
     }
     
}

static void checkpoint(proc_context_t *proc_ctx, be_context_t **ctx, int n)
{
     int be;
     GList *complete_views = NULL;
     GList *i;
     complete_view_t *view;

     for (be = 0; be < n; be++) {
	  while ((view = extract_complete_view(ctx[be]->received))) {
	       if (!g_list_find_custom(complete_views, view, &complete_view_cmp)) {
		    complete_views = g_list_insert_sorted(complete_views, 
							  view, &complete_view_cmp);
	       } else {
		    complete_view_free(view);
	       }
	  }
     }
     
     for (i = complete_views; i; i = g_list_next(i)) {
	  verify_view(i->data, ctx, n);
	  complete_view_free(i->data);
     }
     g_list_free(complete_views);
}


START_TEST(test_vs_msg)
{
     int i;
     vs_msg_t *msg, *readmsg, *msg_cp;
     msg_t *msgbuf;
     char *buf;
     size_t buflen;
     msg_t *readbuf;
     char *payload;
     size_t payload_len;
     fail_unless(!!(msg = vs_msg_new(VS_MSG_REG_CONF,
				     1, 2, 3, VS_MSG_SAFETY_SAFE,
				     NULL, NULL)));
     vs_msg_free(msg);

     payload = g_malloc(128);
     payload_len = 128;
     for (i = 0; i < 128; i++)
	  payload[i] = i;

     fail_unless(!!(msgbuf = msg_new()));
     msg_set_payload(msgbuf, payload, payload_len);
     fail_unless(!!(msg = vs_msg_new(VS_MSG_REG_CONF, 
				     1, 2, 3, VS_MSG_SAFETY_SAFE, 
				     msgbuf, NULL)));     
     buflen = msg_get_len(msgbuf);
     buf = g_malloc(buflen);
     fail_unless(msg_write(msgbuf, buf, buflen));

     fail_unless(!!(readbuf = msg_read(buf, buflen)));
     fail_unless(!!(readmsg = vs_msg_read(readbuf)));
     
     fail_unless(!!(msg_cp = vs_msg_copy(msg)));

     fail_unless(msg_equals(msg_cp, readmsg));
     vs_msg_free(msg);
     vs_msg_free(msg_cp);
     vs_msg_free(readmsg);
     msg_free(msgbuf);
     msg_free(readbuf);
     g_free(buf);
     g_free(payload);
}
END_TEST


START_TEST(test_vs_backend_simple)
{
     vs_backend_t *be;
     be_context_t *ctx;
     proc_context_t *proc_ctx;
     vs_msg_t *msg;
     msg_t *msgbuf;

     fail_unless(!!(proc_ctx = proc_context_new()));
     fail_unless(!!(be = vs_backend_new("shm:test", proc_ctx, &be_send_cb)));
     fail_unless(!!(ctx = be_context_new(be)));
     vs_backend_set_recv_cb(be, ctx, &be_recv_cb);
     
     fail_unless(vs_backend_sched(be, NULL, -1) == 0);
     fail_if(vs_backend_connect(be, 0));
     
     fail_unless(vs_backend_sched(be, NULL, -1) == 1);
     fail_unless(vs_backend_sched(be, NULL, -1) == 1);
     fail_unless(vs_backend_sched(be, NULL, -1) == 0);
     
     msgbuf = msg_new();
     msg = vs_msg_new(VS_MSG_DATA, 
		      vs_backend_get_self_addr(be), 
		      vs_backend_get_view_id(be), 0,
		      VS_MSG_SAFETY_SAFE,
		      msgbuf, NULL);
     vs_backend_send(be, msg);
     
     
     fail_unless(vs_backend_sched(be, NULL, -1) == 1);
     fail_unless(vs_backend_sched(be, NULL, -1) == 0);
     
     fail_unless(last_received_msg_equals(ctx, msg));
     vs_msg_free(msg);
     msg_free(msgbuf);
     
     vs_backend_disconnect(be);
     
     fail_unless(vs_backend_sched(be, NULL, -1) == 1);
     fail_unless(vs_backend_sched(be, NULL, -1) == 0);
     
     be_context_free(ctx);
     proc_context_free(proc_ctx);
}
END_TEST

START_TEST(test_vs_backend_complex)
{
     int i, j;
#define N 4
     int n_context = N;
     be_context_t *ctx[N];
#undef N
     proc_context_t *proc_context;
     int view_id = 0;
     vs_view_t *view;
     vs_backend_t *be;
     vs_backend_t *main_be;

     fail_unless(!!(proc_context = proc_context_new()));
     
     memset(ctx, 0, sizeof(ctx));
     
     for (i = 0; i < n_context; i++) {
          fail_unless(!!(be = vs_backend_new("shm:test", proc_context, &be_send_cb)));
          fail_unless(!!(ctx[i] = be_context_new(be)));
          vs_backend_set_recv_cb(be, ctx[i], &be_recv_cb);
	  if (i == 0)
	       main_be = be;
     }
     
     fail_unless(vs_backend_sched(main_be, NULL, -1) == 0);
     
     for (i = 0; i < n_context; i++) {
          fail_if(vs_backend_connect(ctx[i]->be, 0));
          
          printf("Trans\n");
          fail_unless(!!(view = vs_view_new(VS_VIEW_TRANS, view_id)));
          for (j = 0; j < i; j++) 
               vs_view_addr_insert(view, j + 1);
	  
          fail_unless(vs_backend_sched(main_be, NULL, -1) == 1);
          for (j = 0; j < i - 1; j++)
               fail_unless(last_received_view_equals(ctx[j], view), 
                           "Invalid transitional view (%i,%i,%i)", i, j, view_id);
          vs_view_free(view);
          
          printf("Reg\n");
          fail_unless(!!(view = vs_view_new(VS_VIEW_REG, ++view_id)));
          for (j = 0; j <= i; j++)
               vs_view_addr_insert(view, j + 1);
          vs_view_joined_addr_insert(view, i + 1);
          fail_unless(vs_backend_sched(main_be, NULL, -1) == 1);
          for (j = 0; j < i; j++)
               fail_unless(last_received_view_equals(ctx[j], view), 
                           "Invalid regular view (%i,%i,%i)", i, j, view_id);
          fail_unless(vs_backend_sched(main_be, NULL, -1) == 0);
          vs_view_free(view);

          
          send_some(ctx, 0, i);

     }

     send_some(ctx, 0, n_context - 1);
     
     for (i = 0; i < n_context; i++) {
          fail_unless(vs_backend_sched(main_be, NULL, -1) == 0);
          vs_backend_disconnect(ctx[i]->be);

          printf("Trans\n");
          view = vs_view_new(VS_VIEW_TRANS, view_id);
          for (j = i + 1; j < n_context; j++) 
               vs_view_addr_insert(view, j + 1);
          vs_view_left_addr_insert(view, i + 1);
          fail_unless(vs_backend_sched(main_be, NULL, -1) == 1);
          for (j = i + 1; j < n_context; j++)
               fail_unless(last_received_view_equals(ctx[j], view),
                           "Invalid trans view (%i,%i,%i)",
                           i, j, view_id);
          vs_view_free(view);
          
          printf("Reg %i\n", i);
          view = vs_view_new(VS_VIEW_REG, ++view_id);
          for (j = i + 1; j < n_context; j++)
               vs_view_addr_insert(view, j + 1);
	  if (i < n_context - 1)
	       fail_unless(vs_backend_sched(main_be, NULL, -1) == 1);
          for (j = i + 1; j < n_context; j++)
               fail_unless(last_received_view_equals(ctx[j], view));
          vs_view_free(view);
          fail_unless(vs_backend_sched(main_be, NULL, -1) == 0);
          
          send_some(ctx, i + 1, n_context - 1);
     }


     for (i = 0; i < n_context; i++) {
          be_context_free(ctx[i]);
     }
     proc_context_free(proc_context);
     
}
END_TEST


START_TEST(test_vs_backend_random)
{
     int i;
     int be_cnt = 0;
     GList *iter;
     GList *active_ctx = NULL;
     GList *passive_ctx = NULL;
     be_context_t *ctx[32];
     proc_context_t *proc_ctx;
     double join_prob = 0.02, leave_prob = 0.008;
     unsigned int t;
     be_context_t *ctx_p;
     vs_backend_t *be;
     vs_backend_t *main_be;
     int err;
     
     fail_unless(!!(proc_ctx = proc_context_new()));

     for (i = 0; i < 32; i++) {
          fail_unless(!!(be = vs_backend_new("shm:test", 
                                             proc_ctx, &be_send_cb)));
          fail_unless(!!(ctx[i] = be_context_new(be)));
          vs_backend_set_recv_cb(be, ctx[i], &be_recv_cb);
          passive_ctx = g_list_append(passive_ctx, ctx[i]);
	  if (i == 0)
	       main_be = be;
     }
     
     for (t = 0; t < 10000; t++) {
          if ((double)rand()/RAND_MAX < join_prob && 
              g_list_length(passive_ctx)) {
               iter = g_list_nth(passive_ctx, rand()%g_list_length(passive_ctx))
;
               ctx_p = iter->data;
               if (ctx_p->state == BE_DISCONNECTED) {
                    passive_ctx = g_list_delete_link(passive_ctx, iter);
                    active_ctx = g_list_append(active_ctx, ctx_p);
                    err = vs_backend_connect(ctx_p->be, 0);
                    fail_if(err, "Error: %i", err);
                    printf("join: %i\n", vs_backend_get_self_addr(ctx_p->be));
		    be_cnt++;
               }
          } 
          if ((double)rand()/RAND_MAX < leave_prob &&
              g_list_length(active_ctx)) {
               iter = g_list_nth(active_ctx, rand()%g_list_length(active_ctx));
               ctx_p = iter->data;
               if (ctx_p->state == BE_CONNECTED) {
                    active_ctx = g_list_delete_link(active_ctx, iter);
                    passive_ctx = g_list_append(passive_ctx, ctx_p);
                    vs_backend_disconnect(ctx_p->be);
                    printf("leave: %i\n", vs_backend_get_self_addr(ctx_p->be));
               }
          }

          send_some_more(active_ctx);
          if (rand()%11 == 0)
               while (vs_backend_sched(main_be, NULL, -1));
          if (t % 10 == 0)
               printf("t = %i\n", t);

          if (t % 50 == 0) {
               while (vs_backend_sched(main_be, NULL, -1));
               checkpoint(proc_ctx, ctx, 32);
          }

	  if (rand()%300 == 0)
	       vs_backend_shm_split();
	  if (rand()%150 == 0)
	       vs_backend_shm_merge();


     }
     while (vs_backend_sched(main_be, NULL, -1));
     checkpoint(proc_ctx, ctx, 32);
     for (i = 0; i < 32; i++) {
          be_context_free(ctx[i]);
     }
     proc_context_free(proc_ctx);
     g_list_free(active_ctx);
     g_list_free(passive_ctx);
     printf("Random stats: data messages received %g per be\n", 
	    (double)data_msg_received/be_cnt);
}
END_TEST


static Suite *vs_suite()
{
     Suite *s;
     TCase *tc_msg;
     TCase *tc_backend_simple;
     TCase *tc_backend_complex;
     TCase *tc_backend_random;

     s = suite_create("VS Backend");
     
     tc_msg = tcase_create("Message test");
     tcase_add_test(tc_msg, test_vs_msg);
     suite_add_tcase(s, tc_msg);

     tc_backend_simple = tcase_create("Backend test simple");
     tcase_add_test(tc_backend_simple, test_vs_backend_simple);
     suite_add_tcase(s, tc_backend_simple);

     tc_backend_complex = tcase_create("Backend complex test");
     tcase_add_test(tc_backend_complex, test_vs_backend_complex);
     suite_add_tcase(s, tc_backend_complex);

     tc_backend_random = tcase_create("Backend random test");
     tcase_add_test(tc_backend_random, test_vs_backend_random);
     tcase_set_timeout(tc_backend_random, 120);
     suite_add_tcase(s, tc_backend_random);

     return s;
}

int main(int argc, char *argv[])
{
     int nfail;

     Suite *s; 
     SRunner *sr;

     s = vs_suite();
     sr = srunner_create(s);
     
     srunner_run_all(sr, CK_NORMAL);
     nfail = srunner_ntests_failed(sr);
     srunner_free(sr);
     return (nfail == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
