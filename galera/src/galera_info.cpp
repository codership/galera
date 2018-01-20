// Copyright (C) 2009-2018 Codership Oy <info@codership.com>

#include "galera_info.hpp"

#include <gu_uuid.hpp>
#include <gu_throw.hpp>

#include <string.h>
#include <vector>

static size_t
view_info_size (int members)
{
    return (sizeof(wsrep_view_info_t) + members * sizeof(wsrep_member_info_t));
}

/* create view info out of configuration message */
wsrep_view_info_t* galera_view_info_create (const gcs_act_cchange& conf,
                                            wsrep_cap_t const      capabilities,
                                            int const              my_idx,
                                            wsrep_uuid_t&          my_uuid)
{
    wsrep_view_info_t* ret = static_cast<wsrep_view_info_t*>(
        ::malloc(view_info_size(conf.memb.size())));

    if (ret)
    {
        wsrep_seqno_t const seqno
            (conf.seqno != GCS_SEQNO_ILL ? conf.seqno : WSREP_SEQNO_UNDEFINED);
        wsrep_gtid_t const gtid = { conf.uuid, seqno };

        ret->state_id  = gtid;
        ret->view      = conf.conf_id;
        ret->status    = conf.conf_id != -1 ?
            WSREP_VIEW_PRIMARY : WSREP_VIEW_NON_PRIMARY;
        ret->capabilities = capabilities;
        ret->my_idx    = -1;
        ret->memb_num  = conf.memb.size();
        ret->proto_ver = conf.appl_proto_ver;

        for (int m = 0; m < ret->memb_num; ++m)
        {
            const gcs_act_cchange::member& cm(conf.memb[m]);    // from
            wsrep_member_info_t&           wm(ret->members[m]); // to

            wm.id = cm.uuid_;

            if (wm.id == my_uuid)
            {
                ret->my_idx = m;
            }

            strncpy(wm.name, cm.name_.c_str(), sizeof(wm.name) - 1);
            wm.name[sizeof(wm.name) - 1] = '\0';

            strncpy(wm.incoming, cm.incoming_.c_str(), sizeof(wm.incoming) - 1);
            wm.incoming[sizeof(wm.incoming) - 1] = '\0';

        }

        if (WSREP_UUID_UNDEFINED == my_uuid && my_idx >= 0)
        {
            assert(-1 == ret->my_idx);
            ret->my_idx = my_idx;
            assert(ret->my_idx < ret->memb_num);
            my_uuid = ret->members[ret->my_idx].id;
        }
    }
    else
    {
        gu_throw_error(ENOMEM) << "Failed to allocate galera view info";
    }
    return ret;
}

/* make a copy of view info object */
wsrep_view_info_t* galera_view_info_copy (const wsrep_view_info_t* vi)
{
    size_t ret_size = view_info_size (vi->memb_num);
    wsrep_view_info_t* ret = static_cast<wsrep_view_info_t*>(malloc (ret_size));
    if (ret) {
        memcpy (ret, vi, ret_size);
    }
    return ret;
}
