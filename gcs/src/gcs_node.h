/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * Node context
 */

#ifndef _gcs_node_h_
#define _gcs_node_h_

#include <errno.h>

#include "gcs.h"
#include "gcs_defrag.h"
#include "gcs_comp_msg.h"
#include "gcs_state.h"

struct gcs_node
{
    gcs_seqno_t        last_applied; // last applied action on that node
//    long             protocol;     // highest supported protocol
//    long             queue_len;    // action queue length on that node
    gcs_state_node_t   status;       // node status
    gcs_proto_t        proto_min;    // supported protocol versions
    gcs_proto_t        proto_max;    // 
    gcs_defrag_t       app;          // defragmenter for application actions
    gcs_defrag_t       oob;        // defragmenter for out-of-band service acts.

    // globally unique id from the component message
    const char         id[GCS_COMP_MEMB_ID_MAX_LEN + 1];

    // to track snapshot status
    char               joiner[GCS_COMP_MEMB_ID_MAX_LEN + 1];
    char               donor [GCS_COMP_MEMB_ID_MAX_LEN + 1];

    const char*        name;         // human-given name
    const char*        inc_addr;     // incoming address - for load balancer
    const gcs_state_t* state;        // state message
};
typedef struct gcs_node gcs_node_t;

/*! Initialize node context */
extern void
gcs_node_init (gcs_node_t* node, const char* id);

/*! Move data from one node object to another */
extern void
gcs_node_move (gcs_node_t* dest, gcs_node_t* src);

/*! Deallocate resources associated with the node object */
extern void
gcs_node_free (gcs_node_t* node);

/*! Reset node's receive buffers */
extern void
gcs_node_reset (gcs_node_t* node);

/*!
 * Handles action message. Is called often - therefore, inlined
 *
 * @return
 */
static inline ssize_t
gcs_node_handle_act_frag (gcs_node_t*           node,
                          const gcs_act_frag_t* frg,
                          gcs_recv_act_t*       act,
                          bool                  local)
{
    if (gu_likely(GCS_ACT_SERVICE != frg->act_type)) {
        return gcs_defrag_handle_frag (&node->app, frg, act, local);
    }
    else if (GCS_ACT_SERVICE == frg->act_type) {
        return gcs_defrag_handle_frag (&node->oob, frg, act, local);
    }
    else {
        gu_warn ("Unrecognised action type: %d", frg->act_type);
        assert(0);
        return -EPROTO;
    }
}

static inline void
gcs_node_set_last_applied (gcs_node_t* node, gcs_seqno_t seqno)
{
    if (gu_unlikely(seqno < node->last_applied)) {
        gu_warn ("Received bogus LAST message: %lld, from node %s, "
                 "expected >= %lld. Ignoring.",
                 seqno, node->id, node->last_applied);
    } else {
        node->last_applied = seqno;
    }
}

static inline gcs_seqno_t
gcs_node_get_last_applied (gcs_node_t* node)
{
    return node->last_applied;
}

/*! Record state message from the node */
extern void
gcs_node_record_state (gcs_node_t* node, gcs_state_t* state);

/*! Update node status according to quorum decisions */
extern void
gcs_node_update_status (gcs_node_t* node, const gcs_state_quorum_t* quorum);

#endif /* _gcs_node_h_ */
