/*
 * Copyright (C) 2020 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef __gcs_test_utils__
#define __gcs_test_utils__

#include "../gcs_group.hpp"

struct gt_node
{
    gcs_group_t group;
    char id[GCS_COMP_MEMB_ID_MAX_LEN + 1]; /// ID assigned by the backend

    explicit gt_node(const char* name = NULL);
    ~gt_node();

    gcs_node_state_t
    state() const { return group.nodes[group.my_idx].status; }
};

#define GT_MAX_NODES 10

struct gt_group
{
    struct gt_node* nodes[GT_MAX_NODES];
    int             nodes_num;
    bool            primary;

    explicit gt_group(int num = 0, bool prim = true);
    ~gt_group();

    /* deliver new component message to all memebers */
    int deliver_component_msg(bool prim);

    /* perform state exchange between the members */
    int perform_state_exchange();

    /* add node to group (deliver new component and perform state exchange)
     * @param new_id should node get new ID? */
    int add_node(struct gt_node*, bool new_id);

    /* NOTE: this function uses simplified and determinitstic algorithm where
     *       dropped node is always replaced by the last one in group.
     *       For our purposes (reproduction of #465) it fits perfectly.
     * @return dropped node handle */
    struct gt_node* drop_node(int idx);

    /* deliver GCS_MSG_SYNC or GCS_MSG_JOIN msg*/
    int deliver_join_sync_msg (int src_idx, gcs_msg_type_t type);

    /* @return true if all nodes in the group see node @param idx with a state
     * @param check */
    bool verify_node_state_across(int idx, gcs_node_state_t check) const;

    /* start SST on behalf of node idx (joiner)
     * @return donor idx or negative error code */
    int sst_start (int joiner_idx, const char* donor_name);

    /* Finish SST on behalf of a node idx (joiner or donor) */
    int sst_finish(int idx);

    /* join and sync added node (sst_start() + sst_finish()) */
    int sync_node(int joiner_idx);
};

#endif /* __gu_test_utils__ */
