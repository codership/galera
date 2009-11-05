// Copyright (C) 2009 Codership Oy <info@codership.com>

#include <string.h>
#include "galera_info.h"

static size_t
view_info_size (int members)
{
    return (sizeof(wsrep_view_info_t) + members * sizeof(wsrep_member_info_t));
}

/* create view info out of configuration message */
wsrep_view_info_t* galera_view_info_create (const gcs_act_conf_t* conf)
{
    wsrep_view_info_t* ret = malloc(view_info_size(conf->memb_num));

    if (ret) {
        const char* str = conf->data;
        int m;

        ret->id        = *(wsrep_uuid_t*)&conf->group_uuid;
        ret->conf      = conf->conf_id;
        ret->first     = conf->seqno != GCS_SEQNO_ILL ?
                         (conf->seqno + 1) : WSREP_SEQNO_UNDEFINED;
        ret->status    = conf->conf_id != -1 ?
                         WSREP_VIEW_PRIMARY : WSREP_VIEW_NON_PRIMARY;
        ret->state_gap = conf->st_required;
        ret->my_idx    = conf->my_idx;
        ret->memb_num  = conf->memb_num;

        for (m = 0; m < ret->memb_num; m++) {
            wsrep_member_info_t* member = &ret->members[m];
            snprintf ((char*)&member->id,   sizeof(wsrep_uuid_t), "%s", str);
            str = str + strlen (str) + 1;
            member->status          = WSREP_MEMBER_EMPTY;
            member->last_committed  = WSREP_SEQNO_UNDEFINED;
            member->slave_queue_len = WSREP_SEQNO_UNDEFINED;
            member->cpu_usage       = -1;
            member->load_avg        = -1;

            snprintf (member->name,     sizeof(member->name),
                      "not supported yet");
            snprintf (member->incoming, sizeof(member->incoming),
                      "not supported yet");
        }
    }

    return ret;
}

/* make a copy of view info object */
wsrep_view_info_t* galera_view_info_copy (const wsrep_view_info_t* vi)
{
    size_t ret_size = view_info_size (vi->memb_num);
    wsrep_view_info_t* ret = malloc (ret_size);
    if (ret) {
        memcpy (ret, vi, ret_size);
    }
    return ret;
}
