/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <errno.h>

#include "gcs_act_proto.h"
#include "gcs_recv_act.h"

/*!
 * Handle received message - action fragment
 *
 * In order to optimize branch prediction used gu_likely macros and odered and
 * nested if/else blocks according to branch probability.
 *
 * @return 0              - success,
 *         size of action - success, full action received,
 *         negative       - error.
 */
ssize_t
gcs_recv_act_handle_msg (gcs_recv_act_t*       act,
                         const gcs_recv_msg_t* msg,
                         bool                  foreign)
{
    long           ret;
    gcs_act_frag_t frg;

    ret = gcs_act_proto_read (&frg, msg->buf, msg->size);

    if (gu_likely(!ret)) {

        if (gu_likely(act->received)) {
            /* another fragment of existing action */
            act->frag_no++;
            if (gu_unlikely((act->sent_id != frg.act_id)  ||
                            (act->frag_no != frg.frag_no) ||
                            (act->type    != frg.act_type))) {
                gu_error ("Unordered fragment received. Protocol error.");
                gu_debug ("act_id   expected: %llu, received: %llu\n"
                          "frag_no  expected: %ld, received: %ld\n"
                          "act_type expected: %d, received: %d",
                          act->sent_id, frg.act_id, act->frag_no,frg.frag_no,
                          act->type, frg.act_type);
                act->frag_no--; // revert counter in hope that we get good frag 
                return -EPROTO;
            }
        }
        else {
            /* new action */
            if (gu_likely(0 == frg.frag_no)) {
                act->size    = frg.act_size;
                act->sent_id = frg.act_id;
                act->type    = frg.act_type;

                if (gu_likely(foreign)) {
                    /* A foreign action. We need to allocate buffer for it.
                     * This buffer will be returned to application,
                     * so it must be allocated by standard malloc */
                    act->head = malloc (act->size);
                    if(gu_likely(act->head != NULL))
                        act->tail = act->head;
                    else
                        return -ENOMEM;
                }
            }
            else {
                gu_error ("Unordered fragment received. Protocol error.");
                gu_debug ("frag_no  expected: 0(first), received: %ld\n"
                          "act_id: %llu, act_type: %d",
                          frg.frag_no, frg.act_id, frg.act_type);
                return -EPROTO;
            }
        }

        act->received += frg.frag_len;
        assert (act->received <= act->size);

        if (gu_likely(foreign)) {
            assert (act->tail);
            memcpy (act->tail, frg.frag, frg.frag_len);
            act->tail += frg.frag_len;
        }

        if (gu_likely (act->received != act->size)) {
            return 0;
        }
        else {
            assert (act->received == act->size);
            return act->received;
        }
    }
    else {
	return ret;
    }
}
