/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * Receiving action context
 */

#ifndef _gcs_defrag_h_
#define _gcs_defrag_h_

#include <string.h>   // for memset()
#include <stdbool.h>
#include <galerautils.h>

#include "gcs.h"     // for gcs_seqno_t et al.
#include "gcs_act_proto.h"
#include "gcs_act.h"

typedef struct gcs_defrag
{
    gcs_seqno_t    sent_id; // sent id (unique for a node)
    uint8_t*       head;    // head of action buffer
    uint8_t*       tail;    // tail of action data
    size_t         size;
    size_t         received;
    ulong          frag_no; // number of fragment received
    bool           reset;
}
gcs_defrag_t;

static inline void
gcs_defrag_init (gcs_defrag_t* df)
{
    memset (df, 0, sizeof (*df));
    df->sent_id = GCS_SEQNO_ILL;
}

/*!
 * Handle received action fragment
 *
 * @return 0              - success,
 *         size of action - success, full action received,
 *         negative       - error.
 */
extern ssize_t
gcs_defrag_handle_frag (gcs_defrag_t*         df,
                        const gcs_act_frag_t* frg,
                        struct gcs_act*       act,
                        bool                  local);

/*! Deassociate, but don't deallocate action resources */
static inline void
gcs_defrag_forget (gcs_defrag_t* df)
{
    gcs_defrag_init (df);
}

/*! Free resources associated with defrag (for lost node cleanup) */
static inline void
gcs_defrag_free (gcs_defrag_t* df)
{
    free (df->head); // alloc'ed with standard malloc
    gcs_defrag_init (df);
}

/*! Mark current action as reset */
static inline void
gcs_defrag_reset (gcs_defrag_t* df)
{
    df->reset = true;
}

#endif /* _gcs_defrag_h_ */
