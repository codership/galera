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

typedef struct gcs_defrag
{
    gcs_seqno_t    sent_id; // sent id (unique for a node)
    uint8_t*       head;    // head of action buffer
    uint8_t*       tail;    // tail of action data
    size_t         size;
    size_t         received;
    long           frag_no; // number of fragment received
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
                        bool                  local);

/*!
 * Pop received action buffer and get ready to receive another
 *
 * @return pointer to action buffer, NULL if action is local - must be
 *         fetched from local fifo.
 */
static inline uint8_t*
gcs_defrag_pop (gcs_defrag_t* df)
{
    register uint8_t* ret = df->head;

    assert (df->size == df->received);
    gcs_defrag_init (df);

    return ret;
}


/*! Deassociate, but don't deallocate action resources */
static inline void
gcs_defrag_forget (gcs_defrag_t* df)
{
    df->head = NULL;
}

/*! Free resources associated with defrag (for lost node cleanup) */
static inline void
gcs_defrag_free (gcs_defrag_t* df)
{
    free (df->head); // alloc'ed with standard malloc
    df->head = NULL;
}

#endif /* _gcs_defrag_h_ */
