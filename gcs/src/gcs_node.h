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

struct gcs_node
{
    gcs_seqno_t    last_applied; // last applied action on that node
    long           queue_len;    // action queue length on that node
    gcs_defrag_t app;          // defragmenter for application actions
    gcs_defrag_t oob;          // defragmenter for out-of-band service acts.

    // globally unique id from the component message
    const char      id[GCS_COMP_MEMB_ID_MAX_LEN + 1];
};

typedef struct gcs_node gcs_node_t;

/*! Initialize node context */
extern void
gcs_node_init (gcs_node_t* node);

/*! Move data from one node object to another */
extern void
gcs_node_move (gcs_node_t* dest, gcs_node_t* src);

/*! Deallocate resources associated with the node object */
extern void
gcs_node_free (gcs_node_t* node);

/*! Reset node's receive buffers */
static inline void
gcs_node_reset (gcs_node_t* node) { gcs_node_free(node); }

/*!
 * Handles action message. Is called often - therefore, inlined
 *
 * @return
 */
static inline ssize_t
gcs_node_handle_act_frag (gcs_node_t* node, gcs_act_frag_t* frg, bool local)
{
    if (gu_likely(GCS_ACT_DATA == frg->act_type)) {
        return gcs_defrag_handle_frag (&node->app, frg, local);
    }
    else if (GCS_ACT_SERVICE == frg->act_type) {
        return gcs_defrag_handle_frag (&node->oob, frg, local);
    }
    else {
        return -EPROTO;
    }
}
#endif /* _gcs_node_h_ */
