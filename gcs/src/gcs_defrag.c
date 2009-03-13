/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <errno.h>
#include <unistd.h>

#include "gcs_act_proto.h"
#include "gcs_defrag.h"

/*!
 * Handle action fragment
 *
 * Unless a whole action is returned, contents of act is undefined
 *
 * In order to optimize branch prediction used gu_likely macros and odered and
 * nested if/else blocks according to branch probability.
 *
 * @return 0              - success,
 *         size of action - success, full action received,
 *         negative       - error.
 */
ssize_t
gcs_defrag_handle_frag (gcs_defrag_t*         df,
                        const gcs_act_frag_t* frg,
                        gcs_recv_act_t*       act,
                        bool                  local)
{
    if (gu_likely(df->received)) {
        /* another fragment of existing action */
        df->frag_no++;
        if (gu_unlikely((df->sent_id != frg->act_id) ||
                        (df->frag_no != frg->frag_no))) {
            gu_error ("Unordered fragment received. Protocol error.");
            gu_error ("\nact_id   expected: %llu, received: %llu\n"
                      "frag_no  expected: %ld, received: %ld",
                      df->sent_id, frg->act_id, df->frag_no, frg->frag_no);
            df->frag_no--; // revert counter in hope that we get good frag
            assert(0);
            return -EPROTO;
        }
    }
    else {
        /* new action */
        if (gu_likely(0 == frg->frag_no)) {
            df->size    = frg->act_size;
            df->sent_id = frg->act_id;

            if (gu_likely(!local)) {
                /* A foreign action. We need to allocate buffer for it.
                 * This buffer will be returned to application,
                 * so it must be allocated by standard malloc */
                df->head = malloc (df->size);
                if(gu_likely(df->head != NULL))
                    df->tail = df->head;
                else {
                    gu_error ("Could not allocate memory for new foreign "
                              "action of size: %z", df->size);
                    assert(0);
                    return -ENOMEM;
                }
            }
        }
        else {
            gu_error ("Unordered fragment received. Protocol error.");
            gu_debug ("frag_no  expected: 0(first), received: %ld\n"
                      "act_id: %llu",
                      frg->frag_no, frg->act_id);
            assert(0);
            return -EPROTO;
        }
    }

    df->received += frg->frag_len;
    assert (df->received <= df->size);

    if (gu_likely(!local)) {
        assert (df->tail);
        memcpy (df->tail, frg->frag, frg->frag_len);
        df->tail += frg->frag_len;
    }

    if (gu_likely (df->received != df->size)) {
        return 0;
    }
    else {
        assert (df->received == df->size);
        act->buf     = df->head;
        act->buf_len = df->received;
        gcs_defrag_init (df);
        return act->buf_len;
    }
}
