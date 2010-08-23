// Copyright (C) 2007 Codership Oy <info@codership.com>
/*
 * Quorum object - implementation
 *
 * Quorum state diagram:
 *
 *                           comp_msg
 *                           +------+
 *                           V      |
 *     +------+  comp_msg  +----------+  VE finished  +------+
 *     | INIT |----------->|  VOTING  |-------------->| DONE |
 *     +------+            +----------+               +------+
 *                               ^                       |
 *                               +-----------------------+
 *                                        comp_msg
 *
 * 1. When the quorum object is created it is initialized to INIT state.
 *
 * 2. When primary component message (new primary configuration) is received
 *    each node regardless of the state:
 *    a) sends a vote containing its
 *       - group UUID;
 *       - number of the current primary configuration (-1 if unknown);
 *       - index in the last primary configuration - to know representatives
 *         (-1 if not coming from PRIM_COMP);
 *    b) goes into VOTING state.
 *
 * 3. When a node is in the VOTING state and all (enough?) votes have
 *    been recived, it
 *    a) calculates a quorum (previous quorum members can do it immediately)
 *    b) goes into DONE state
 *
 * NOTE: non-trivial votes are sent only by the members who come from a
 *       previous primary configuration and therefore can compute quorum
 *       by themselves.
 */

#include <errno.h>
#include <arpa/inet.h> // htonl/ntohl
#include <string.h>    // memset
#include <galerautils.h>

#include "gcs_msg_type.h"
#include "gcs_quorum.h"

/** A helper type to count quorum votes */
typedef struct
{
    size_t         ctr;
    gcs_vote_msg_t vote;
}
quorum_vote_t;

struct gcs_quorum {
    gcs_uuid_t          uuid;         /// group UUID to detect collisions
    gcs_quorum_init_t   init_cond;
    gcs_quorum_state_t  state;
    gcs_quorum_status_t status;
    long                prim_id;
    gcs_comp_msg_t*     memb;
    gcs_comp_msg_t*     memb_pending;
    bool*               memb_states;  // here we record member states
    size_t              votes_cnt;
    quorum_vote_t*      votes;       /// we put received votes here
};

static const long QUORUM_PRIM_NONE = -1;

#define QUORUM_VOTE_FORMAT \
        "(uuid:"GCS_UUID_FORMAT", prim_id:%d, status:%d)"

#define QUORUM_VOTE_ARGS(vote) \
        GCS_UUID_ARGS(&((vote)->uuid)), (vote)->prim_id, (vote)->status

/** Creates quorum object. */
gcs_quorum_t*
gcs_quorum_create (gcs_quorum_init_t init_cond)
{
    gcs_quorum_t* ret = GU_CALLOC (1, gcs_quorum_t);

    if (ret) {
        gcs_uuid_generate(&ret->uuid, NULL, 0);
        ret->init_cond    = init_cond;
        ret->state        = GCS_QUORUM_INIT;
        ret->status       = GCS_QUORUM_UNKNOWN;
        ret->prim_id      = QUORUM_PRIM_NONE;
        ret->votes_cnt    = 0;
        ret->votes        = NULL;
        ret->memb         = NULL;
        ret->memb_pending = NULL;
    }

    return ret;
}

/** Destroys quorum object. */
void
gcs_quorum_destroy (gcs_quorum_t* quorum)
{
    if (NULL != quorum) {
        if (quorum->votes) {
            gu_free (quorum->votes);
        }
        if (quorum->memb) {
            gcs_comp_msg_delete (quorum->memb);
        }
        if (quorum->memb_pending) {
            gcs_comp_msg_delete (quorum->memb_pending);
        }
    }

    /* here we want to catch attempt to free NULL quorum, so no checking */
    gu_free (quorum);
}

/** Calculate quorum given old and new memberships */
static gcs_quorum_status_t
quorum_from_memb (const gcs_comp_msg_t* old_memb,
                  const gcs_comp_msg_t* new_memb)
{
    int votes = 0;
    int rep;
    long old;
    long old_num = gcs_comp_msg_num(old_memb);

    /* first, check if old representative is present in new config and count */
    rep    = (gcs_comp_msg_idx(new_memb, gcs_comp_msg_id(old_memb, 0)) >= 0);
    votes += rep;
    
    /* then, count how many other old members made it to new configuration */
    for (old = 1; old < old_num; old++) {
        votes +=
	    (gcs_comp_msg_idx(new_memb, gcs_comp_msg_id(old_memb, old)) >= 0);
    }

    /* if exactly half, take one extra representative vote, if present */
    if (votes * 2 == old_num) votes += rep;

    if (votes * 2 > old_num)
        return GCS_QUORUM_YES;
    else
        return GCS_QUORUM_NO;
}

/** Replaces old pending membership with the new one. */
static void
quorum_realloc_memb_pending (gcs_quorum_t* quorum, const gcs_comp_msg_t* memb)
{
    if (quorum->memb_pending) {
        assert (GCS_QUORUM_VOTING == quorum->state);
        gcs_comp_msg_delete (quorum->memb_pending);
    }

    quorum->memb_pending = gcs_comp_msg_copy (memb);
}

static void
quorum_realloc_votes (gcs_quorum_t* quorum, size_t votes_num)
{
    if (quorum->votes) gu_free (quorum->votes);

    quorum->votes_cnt = 0; // reset votes counter
    quorum->votes     = GU_CALLOC (votes_num, quorum_vote_t);
}

/** Takes component message and computes our vote if possible. */
const gcs_vote_msg_t*
gcs_quorum_memb (gcs_quorum_t* quorum, const gcs_comp_msg_t* memb)
{
    long my_id = gcs_comp_msg_self (memb);

    /* allocate resources required to handle new configuration */
    quorum_realloc_memb_pending (quorum, memb);
    quorum_realloc_votes        (quorum, gcs_comp_msg_num (memb));
    if (NULL == quorum->memb_pending ||
        NULL == quorum->votes) {
        gu_fatal ("Out of memory :( Can't recover.");
        quorum->status = GCS_QUORUM_ERROR;
    }

    if (GCS_QUORUM_INIT == quorum->state) {
        /* first regular configuration on this node */
        if (GCS_QUORUM_AUTO == quorum->init_cond) {
            /* init flag is AUTO - start new REG_PRIM if there are 
             * no "more authoritative" members */
            quorum->status  = GCS_QUORUM_YES;
        }
        else {
            /* cond_init != AUTO - have to listen to other mambers */
            quorum->status = GCS_QUORUM_UNKNOWN;
        }
    }
    else {
        /* new regular configuration */
        if (GCS_QUORUM_YES == quorum->status) {
            /* my previous configuration had a quorum, can compute this one */
            quorum->status = quorum_from_memb (quorum->memb, memb);
            assert (GCS_QUORUM_UNKNOWN != quorum->status);
        }
        else {
            /* I come from non-primary config. Have to wait for votes */
            quorum->status = GCS_QUORUM_UNKNOWN;
        }
    }

    /* initialize our vote */
    quorum->votes[my_id].vote.uuid    = quorum->uuid;
    quorum->votes[my_id].vote.prim_id = htogl(quorum->prim_id);
    quorum->votes[my_id].vote.status  = htogl(quorum->status);
        
    quorum->state = GCS_QUORUM_VOTING;

    return &quorum->votes[my_id].vote;
}

static const char*
quorum_state2str (gcs_quorum_state_t state)
{
    switch (state) {
    case GCS_QUORUM_INIT:   return "'INIT'"  ;
    case GCS_QUORUM_VOTING: return "'VOTING'";
    case GCS_QUORUM_DONE:   return "'DONE'"  ;
    case GCS_QUORUM_ERROR:  return "'ERROR'" ;
    default: return "(invalid state)"        ;
    }
}

/** Calculates quorum from votes */
static gcs_quorum_state_t
quorum_from_votes (gcs_quorum_t* quorum)
{
    quorum_vote_t* votes    = quorum->votes;
    size_t         memb_num = gcs_comp_msg_num (quorum->memb_pending);

    struct {
        gcs_uuid_t uuid;
        long       cnt;
    } uuids[memb_num];

    size_t uuid_cnt = 0; // counts how many different uuids are in membership
    size_t i;

    /* Check for possible different group uuids
     * (only from former primary members) */
    memset (&uuids, 0, sizeof(uuids));
    for (i = 0; i < memb_num; i++) { 
        if (QUORUM_PRIM_NONE != votes[i].vote.prim_id) {
            /* look for this uuid in our list of different uuids */
            size_t j;
            for (j = 0; j < uuid_cnt; j++) {
                if (!gcs_uuid_compare(&uuids[j].uuid, &votes[i].vote.uuid)) {
                    uuids[j].cnt++;
                    goto uuid_found;
                }                    
            }
            
            /* uuid is not in our list, add */
            uuids[uuid_cnt].cnt  = 1;
            uuids[uuid_cnt].uuid = votes[i].vote.uuid;
            uuid_cnt++;
        }
    uuid_found:
        ;
    }

    if (0 == uuid_cnt) {
        /* no members who have ever been in REG_PRIM */
        /* this can happen if, for example, 1 or more nodes have been
         * started simultanously and formed a group. Follow representative */
        quorum->status = votes[0].vote.status;
        if (GCS_QUORUM_YES == quorum->status)
            quorum->uuid   = votes[0].vote.uuid;
    }
    else {
        long latest_prim = -1;
        if (uuid_cnt > 1) {
            /* Memebers from different groups merged together. Can be a result
             * of connecting two groups that started up separately but had the
             * same name. All sequence numbers are uncomparable. */
            /* There are several alternatives of what to do:
             * - choose the oldest uuid, others die;
             * - choose the most numerous and then the oldest uuid, others die;
             * - everybody dies.
             * for now choose the last one.
             */
            gu_fatal ("Distinct quorums merged:");
            for (i = 0; i < uuid_cnt; i++) {
                gu_info ("uuid:" GCS_UUID_FORMAT ", count: %l",
                         GCS_UUID_ARGS(&uuids[i].uuid), uuids[i].cnt);
            }
            gu_fatal ("Can't continue.");
            quorum->status = GCS_QUORUM_UNKNOWN;
            quorum->state  = GCS_QUORUM_ERROR;
            return quorum->state;
        }
        else {
            /* normal situation, only one UUID */
            quorum->uuid = uuids[0].uuid;
        }

        /* Now we have UUID, have to find the most recent REG_PRIM */
        for (i = 0; i < memb_num; i++) {
            if (!gcs_uuid_compare(&quorum->uuid, &votes[i].vote.uuid) &&
                latest_prim < votes[i].vote.prim_id) {
                latest_prim = votes[i].vote.prim_id;
                if (GCS_QUORUM_UNKNOWN != votes[i].vote.status) {
                    quorum->status = votes[i].vote.status;
                }
            }
        }

        /* if we're in quorum, update prim_id */
        if (GCS_QUORUM_YES == quorum->status) {
            quorum->prim_id = latest_prim;
        }

        /* Now one last check to see if all members agree */
        for (i = 0; i < memb_num; i++) {
            if (!gcs_uuid_compare(&quorum->uuid, &votes[i].vote.uuid) &&
                latest_prim    == votes[i].vote.prim_id               &&
                quorum->status != votes[i].vote.status) {
                gu_fatal ("Nodes from the same configuration "
                          "disagree about status");
                quorum->status = GCS_QUORUM_UNKNOWN;
                quorum->state  = GCS_QUORUM_ERROR;
                return quorum->state;
            }
        }
    }

    return quorum->state;
}

/** Processes quorum vote and returns state of voting. */
gcs_quorum_state_t
gcs_quorum_vote (gcs_quorum_t* quorum, const gcs_vote_msg_t* vote,
		 size_t vote_len, long sender_id)
{
    /* check size of the message */
    if (sizeof(gcs_vote_msg_t) != vote_len) {
        gu_fatal ("Received a vote of size %l, should be %l",
                  vote_len, sizeof(gcs_vote_msg_t));
        assert (0);
        quorum->state = GCS_QUORUM_ERROR;
        return quorum->state;
    }

    /* check the sender (move this to caller?) */
    if (sender_id >= gcs_comp_msg_num (quorum->memb_pending)) {
        /* this is a backend error on our side, must crash */
        gu_fatal ("Received a vote " QUORUM_VOTE_FORMAT ", from a member %d "
                  "not in this membership (max id: %d).",
                  QUORUM_VOTE_ARGS(vote), sender_id,
                  gcs_comp_msg_num(quorum->memb_pending));
        assert(0);
        quorum->state = GCS_QUORUM_ERROR;
        return quorum->state;
    }

    /* check the context state */
    if (quorum->state != GCS_QUORUM_VOTING) {
        /** @fixme: it is not clear what to do in this case. Some other
         *          node's malfunctioning should not crash this one */
        gu_warn ("Received a vote " QUORUM_VOTE_FORMAT ", from member %d "
                 "in %s state.",
                 QUORUM_VOTE_ARGS(vote), sender_id,
                 quorum_state2str (quorum->state));
        return quorum->state;
    }

    /* count how many votes we received from this node */
    quorum->votes[sender_id].ctr++;
    assert (quorum->votes[sender_id].ctr > 0);

    /* check if we have already received a vote from this member */
    if (1 != quorum->votes[sender_id].ctr) {
        gu_warn ("%d votes from %d. Discarding. " QUORUM_VOTE_FORMAT,
                 quorum->votes[sender_id].ctr, sender_id,
                 QUORUM_VOTE_ARGS(&quorum->votes[sender_id].vote));
        return quorum->state;
    }

    if (gcs_comp_msg_self (quorum->memb_pending) == sender_id) {
        /* check that we got our own vote right */
        if (gcs_uuid_compare (&quorum->votes[sender_id].vote.uuid,
                              &vote->uuid)
            ||
            quorum->votes[sender_id].vote.prim_id != gtohl(vote->prim_id)
            ||
            quorum->votes[sender_id].vote.status  != gtohl(vote->status))
        {
            gu_fatal ("My own vote is corrupted:\n"
                      "Expected: " QUORUM_VOTE_FORMAT "\n"
                      "Received: " QUORUM_VOTE_FORMAT,
                      QUORUM_VOTE_ARGS(&quorum->votes[sender_id].vote),
                      QUORUM_VOTE_ARGS(vote));
            assert(0);
            quorum->status = GCS_QUORUM_ERROR;
            return quorum->state;
        }
    }
    else {
        quorum->votes[sender_id].vote.uuid    = vote->uuid;
        quorum->votes[sender_id].vote.prim_id = gtohl(vote->prim_id);
        quorum->votes[sender_id].vote.status  = gtohl(vote->status);
    }

    /* count how many nodes we had received votes from */
    quorum->votes_cnt++;
    assert (quorum->votes_cnt > 0);

    if (quorum->votes_cnt < gcs_comp_msg_num (quorum->memb_pending)) {
        /* wait for other votes */
        return quorum->state;
    }

    /* all votes received, see what's up */
    quorum_from_votes (quorum);
    if (GCS_QUORUM_ERROR == quorum->state) return quorum->state;

    /* if we still could not figure the status, it means no quorum */
    if (GCS_QUORUM_UNKNOWN == quorum->status)
        quorum->status = GCS_QUORUM_NO;

    if (GCS_QUORUM_YES == quorum->status) {
        /* New REG_PRIM, update membership */
        gcs_comp_msg_delete (quorum->memb);
        quorum->memb = quorum->memb_pending;
        quorum->memb_pending = NULL;
        quorum->prim_id++;
    }

    quorum->state = GCS_QUORUM_DONE;

    return quorum->state;
}

/** Returns quorum status */
gcs_quorum_status_t
gcs_quorum_status (const gcs_quorum_t* quorum)
{
    return quorum->status;
}

/** Assume quorum unconditionnaly */
void
gcs_quorum_force (gcs_quorum_t* quorum)
{
    gu_error ("NOT IMPLEMENTED YET");
}

