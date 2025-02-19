/*
 * Copyright (C) 2008-2024 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gcs_defrag.hpp"

#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>

#define DF_ALLOC()                                              \
    do {                                                        \
        df->head = gcs_gcache_malloc(df->cache, df->size, &df->plain);  \
                                                                \
        if (gu_unlikely(NULL == df->head)) {                    \
            gu_error ("Could not allocate memory for new "      \
                      "action of size: %zd", df->size);         \
            return -ENOMEM;                                     \
        }                                                       \
        assert(df->plain);                                      \
        df->tail = static_cast<uint8_t*>(df->plain);            \
    } while (0)

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
                 * Reinit counters and continue with the new action. */
                gu_debug("Local action %" PRId64 ", size %zu reset.",
                         frg->act_id, frg->act_size);
                df->frag_no = 0;
                df->received = 0;
                df->tail     = static_cast<uint8_t*>(df->plain);
                df->reset    = false;

                if (df->size != frg->act_size) {

                    df->size = frg->act_size;

#ifndef GCS_FOR_GARB
                    if (df->cache !=NULL) {
                        gcache_free (df->cache, df->head);
                    }
                    else {
                        free (df->head);
                    }

                    DF_ALLOC();
#endif /* GCS_FOR_GARB */
                }
            }
            else if (frg->act_id == df->sent_id && frg->frag_no < df->frag_no) {
                /* gh172: tolerate duplicate fragments in production. */
                gu_warn("Duplicate fragment %" PRId64 ":%ld, expected %" PRId64
                        ":%ld. "
                        "Skipping.",
                        frg->act_id, frg->frag_no, df->sent_id, df->frag_no);
                df->frag_no--; // revert counter in hope that we get good frag
#ifndef GCS_CORE_TESTING // allow unit tests to pass in debug mode
                assert(0);
#endif
                return 0;
            }
            else {
                gu_error ("Unordered fragment received. Protocol error.");
                gu_error("Expected: %" PRId64 ":%ld, received: %" PRId64 ":%ld",
                         df->sent_id, df->frag_no, frg->act_id, frg->frag_no);
                gu_error("Contents: '%.*s'", static_cast<int>(frg->frag_len),
                         (char*)frg->frag);
                df->frag_no--; // revert counter in hope that we get good frag
#ifndef GCS_CORE_TESTING // allow unit tests to pass in debug mode
                assert(0);
#endif
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

#ifndef GCS_FOR_GARB
            DF_ALLOC();
#else
            /* we don't store actions locally at all */
            df->plain = df->head = df->tail = NULL;
#endif
        }
        else {
            /* not a first fragment */
            if (!local && df->reset) {
                /* can happen after configuration change,
                   just ignore this message calmly */
                gu_debug("Ignoring fragment %" PRId64
                         ":%ld (size %zu) after reset",
                         frg->act_id, frg->frag_no, frg->act_size);
                return 0;
            }
            else {
                ((char*)frg->frag)[frg->frag_len - 1] = '\0';
                gu_error ("Unordered fragment received. Protocol error.");
                gu_error ("Expected: any:0(first), received: %" PRId64 ":%ld",
                          frg->act_id, frg->frag_no);
                gu_error ("Contents: '%s', local: %s, reset: %s",
                          (char*)frg->frag, local ? "yes" : "no",
                          df->reset ? "yes" : "no");
#ifndef GCS_CORE_TESTING // allow unit tests to pass in debug mode
                assert(0);
#endif
                return -EPROTO;
            }
        }
    }

#ifndef GCS_FOR_GARB
    assert (df->tail);
    memcpy (df->tail, frg->frag, frg->frag_len);
    df->tail += frg->frag_len;
#else
    /* we skip memcpy since have not allocated any buffer */
    assert (NULL == df->tail);
    assert (NULL == df->head);
#endif

    df->received += frg->frag_len;
    assert (df->received <= df->size);

    int ret;

    if (df->received == df->size) {
        act->buf     = df->head;
        act->buf_len = df->received;
#if 1
        ret = act->buf_len;
#else
        /* Refs gh185. Above original logic is preserved which relies on
         * resetting group->frag_reset when local action needs to be resent.
         * However a proper solution seems to be to use reset flag of own
         * defrag channel (at least it is per channel, not global like
         * group->frag_reset). This proper logic is shown below. Note that
         * for it to work gcs_group_handle_act_msg() must be able to handle
         * -ERESTART return code. */
        if (gu_likely(!df->reset))
        {
            ret = act->buf_len;
        }
        else
        {
            /* foreign action should simply never get here, only local actions
             * are allowed to complete in reset state (to return -ERESTART) to
             * a sending thread. */
            assert(local);
            ret = -ERESTART;
        }
#endif
        /* after this action can spend some time in a slave queue, so let drop
         * plaintext if the queue happens to be too long */
        gcs_gcache_drop_plaintext(df->cache, df->head);
        gcs_defrag_init (df, df->cache);
        assert(!df->reset);
    }
    else {
        ret = 0;
    }

    return ret;
}
