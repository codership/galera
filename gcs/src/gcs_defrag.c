/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <errno.h>
#include <unistd.h>
#include <string.h>

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
 *
 * TODO: this function is too long, figure out a way to factor it into several
 *       smaller ones. Note that it is called for every GCS_MSG_ACTION message
 *       so it should be optimal.
 */
ssize_t
gcs_defrag_handle_frag (gcs_defrag_t*         df,
                        const gcs_act_frag_t* frg,
                        struct gcs_act*       act,
                        bool                  local)
{
    if (df->received) {
        /* another fragment of existing action */

        df->frag_no++;

        /* detect possible error condition */
        if (gu_unlikely((df->sent_id != frg->act_id) ||
                        (df->frag_no != frg->frag_no))) {
            if (local && df->reset &&
                (df->sent_id == frg->act_id) && (0 == frg->frag_no)) {
                /* df->sent_id was aborted halfway and is being taken care of
                 * by the sender thread. Forget about it.
                 * Reinit counters and continue with the new action.
                 * Note that for local actions no memory allocation is made.*/
                gu_debug ("Local action %lld reset.", frg->act_id);
                df->size     = frg->act_size;
                df->frag_no  = 0;
                df->received = 0;
                df->reset    = false;
            }
            else {
                gu_error ("Unordered fragment received. Protocol error.");
                gu_error ("Expected: %llu:%ld, received: %llu:%ld",
                          df->sent_id, df->frag_no, frg->act_id, frg->frag_no);
                gu_error ("Contents: '%.*s'", frg->frag_len, (char*)frg->frag);
                df->frag_no--; // revert counter in hope that we get good frag
                assert(0);
                return -EPROTO;
            }
        }
    }
    else {
        /* new action */
        if (gu_likely(0 == frg->frag_no)) {

            df->size    = frg->act_size;
            df->sent_id = frg->act_id;
            df->reset   = false;

            if (gu_likely(!local)) {
#ifndef GCS_FOR_GARB
                /* We need to allocate buffer for it.
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
#else
                /* we don't store actions locally at all */
                df->head = NULL;
                df->tail = df->head;
#endif
            }
        }
        else {
            /* not a first fragment */
            if (!local && df->reset) {
                /* can happen after configuration change,
                   just ignore this message calmly */
                gu_debug ("Ignoring fragment %lld:%ld after action reset",
                          frg->act_id, frg->frag_no);
                return 0;
            }
            else {
                ((char*)frg->frag)[frg->frag_len - 1] = '\0';
                gu_error ("Unordered fragment received. Protocol error.");
                gu_error ("Expected: any:0(first), received: %lld:%ld",
                          frg->act_id, frg->frag_no);
                gu_error ("Contents: '%s'", (char*)frg->frag);
                assert(0);
                return -EPROTO;
            }
        }
    }

    df->received += frg->frag_len;
    assert (df->received <= df->size);

    if (gu_likely(!local)) {
#ifndef GCS_FOR_GARB
        assert (df->tail);
        memcpy (df->tail, frg->frag, frg->frag_len);
        df->tail += frg->frag_len;
#else
        /* we skip memcpy since have not allocated any buffer */
        assert (NULL == df->tail);
        assert (NULL == df->head);
#endif
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
