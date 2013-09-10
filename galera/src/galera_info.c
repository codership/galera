// Copyright (C) 2009-2013 Codership Oy <info@codership.com>

#include "galera_info.h"
#include <galerautils.h>
#include <string.h>

static size_t
view_info_size (int members)
{
    return (sizeof(wsrep_view_info_t) + members * sizeof(wsrep_member_info_t));
}

/* create view info out of configuration message */
wsrep_view_info_t* galera_view_info_create (const gcs_act_conf_t* conf,
                                            bool                  st_required)
{
    wsrep_view_info_t* ret = malloc(view_info_size(conf->memb_num));

    if (ret) {
        const char* str = conf->data;
        int m;

        wsrep_uuid_t  uuid  = *(wsrep_uuid_t*)&conf->uuid;
        wsrep_seqno_t seqno = conf->seqno != GCS_SEQNO_ILL ?
                              conf->seqno : WSREP_SEQNO_UNDEFINED;
        wsrep_gtid_t  gtid  = { uuid, seqno };

        ret->state_id  = gtid;
        ret->view      = conf->conf_id;
        ret->status    = conf->conf_id != -1 ?
                         WSREP_VIEW_PRIMARY : WSREP_VIEW_NON_PRIMARY;
        ret->state_gap = st_required;
        ret->my_idx    = conf->my_idx;
        ret->memb_num  = conf->memb_num;
        ret->proto_ver = conf->appl_proto_ver;

        for (m = 0; m < ret->memb_num; m++) {
            wsrep_member_info_t* member = &ret->members[m];

            size_t id_len = strlen(str);
            gu_uuid_scan (str, id_len, (gu_uuid_t*)&member->id);
            str = str + id_len + 1;

            strncpy(member->name, str, sizeof(member->name) - 1);
            member->name[sizeof(member->name) - 1] = '\0';
            str = str + strlen(str) + 1;

            strncpy(member->incoming, str, sizeof(member->incoming) - 1);
            member->incoming[sizeof(member->incoming) - 1] = '\0';
            str = str + strlen(str) + 1;

            str += sizeof(gcs_seqno_t); // skip cached seqno.
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
