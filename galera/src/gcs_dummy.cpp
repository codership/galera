//
// Copyright (C) 2011-2012 Codership Oy <info@codership.com>
//

#include "gcs.hpp"

namespace galera
{
    DummyGcs::DummyGcs(gu::Config&     config,
                       gcache::GCache& cache,
                       int repl_proto_ver,
                       int appl_proto_ver,
                       const char* node_name,
                       const char* node_incoming)
        :
        gconf_         (&config),
        gcache_        (&cache),
        mtx_           (),
        cond_          (),
        global_seqno_  (0),
        local_seqno_   (0),
        uuid_          (),
        last_applied_  (GCS_SEQNO_ILL),
        state_         (S_OPEN),
        schedule_      (0),
        cc_            (0),
        cc_size_       (0),
        my_name_       (node_name ? node_name : "not specified"),
        incoming_      (node_incoming ? node_incoming : "not given"),
        repl_proto_ver_(repl_proto_ver),
        appl_proto_ver_(appl_proto_ver),
        report_last_applied_(false)
    {
        gu_uuid_generate (&uuid_, 0, 0);
    }

    DummyGcs::DummyGcs()
        :
        gconf_         (0),
        gcache_        (0),
        mtx_           (),
        cond_          (),
        global_seqno_  (0),
        local_seqno_   (0),
        uuid_          (),
        last_applied_  (GCS_SEQNO_ILL),
        state_         (S_OPEN),
        schedule_      (0),
        cc_            (0),
        cc_size_       (0),
        my_name_       ("not specified"),
        incoming_      ("not given"),
        repl_proto_ver_(1),
        appl_proto_ver_(1),
        report_last_applied_(false)
    {
        gu_uuid_generate (&uuid_, 0, 0);
    }

    DummyGcs::~DummyGcs()
    {
        gu::Lock lock(mtx_);
        assert(0 == schedule_);

        if (cc_)
        {
            assert (cc_size_ > 0);
            ::free(cc_);
        }
    }

    ssize_t
    DummyGcs::generate_cc (bool primary)
    {
        cc_size_ = sizeof(gcs_act_conf_t) +
            primary *
            (my_name_.length() + incoming_.length() + GU_UUID_STR_LEN + 3);

        cc_ = ::malloc(cc_size_);

        if (!cc_)
        {
            cc_size_ = 0;
            return -ENOMEM;
        }

        gcs_act_conf_t* const cc(reinterpret_cast<gcs_act_conf_t*>(cc_));

        if (primary)
        {
            cc->seqno = global_seqno_;
            cc->conf_id = 1;
            memcpy (cc->uuid, &uuid_, sizeof(uuid_));
            cc->memb_num = 1;
            cc->my_idx = 0;
            cc->my_state = GCS_NODE_STATE_JOINED;
            cc->repl_proto_ver = repl_proto_ver_;
            cc->appl_proto_ver = appl_proto_ver_;

            char* const str(cc->data);
            ssize_t offt(0);
            offt += gu_uuid_print (&uuid_, str, GU_UUID_STR_LEN+1) + 1;
            offt += sprintf (str + offt, "%s", my_name_.c_str()) + 1;
            sprintf (str + offt, "%s", incoming_.c_str());
        }
        else
        {
            cc->seqno    = GCS_SEQNO_ILL;
            cc->conf_id  = -1;
            cc->memb_num = 0;
            cc->my_idx   = -1;
            cc->my_state = GCS_NODE_STATE_NON_PRIM;
        }

        return cc_size_;
    }

    ssize_t
    DummyGcs::connect(const std::string& cluster_name,
                      const std::string& cluster_url,
                      bool               bootstrap)
    {
        gu::Lock lock(mtx_);

        ssize_t ret = generate_cc (true);

        if (ret > 0)
        {
            //          state_ = S_CONNECTED;
            cond_.signal();
            ret = 0;
        }

        return ret;
    }

    ssize_t
    DummyGcs::set_initial_position(const wsrep_uuid_t& uuid,
                                   gcs_seqno_t seqno)
    {
        gu::Lock lock(mtx_);

        if (memcmp(&uuid, &GU_UUID_NIL, sizeof(wsrep_uuid_t)) &&
            seqno >= 0)
        {
            uuid_ = *(reinterpret_cast<const gu_uuid_t*>(&uuid));
            global_seqno_ = seqno;
        }
        return 0;
    }

    void
    DummyGcs::close()
    {
        log_info << "Closing DummyGcs";

        gu::Lock lock(mtx_);
        generate_cc (false);
//            state_ = S_CLOSED;
        cond_.broadcast();

//        usleep(100000); // 0.1s
    }


    ssize_t
    DummyGcs::generate_seqno_action (gcs_action& act, gcs_act_type_t type)
    {
        gcs_seqno_t* const seqno(
            reinterpret_cast<gcs_seqno_t*>(
                ::malloc(sizeof(gcs_seqno_t))));

        if (!seqno) return -ENOMEM;

        *seqno      = global_seqno_;
        ++local_seqno_;

        act.buf     = seqno;
        act.size    = sizeof(gcs_seqno_t);
        act.seqno_l = local_seqno_;
        act.type    = type;

        return act.size;
    }

    ssize_t
    DummyGcs::recv(gcs_action& act)
    {
        act.seqno_g = GCS_SEQNO_ILL;
        act.seqno_l = GCS_SEQNO_ILL;

        gu::Lock lock(mtx_);

        do
        {
            if (cc_)
            {
                ++local_seqno_;

                act.buf     = cc_;
                act.size    = cc_size_;
                act.seqno_l = local_seqno_;
                act.type    = GCS_ACT_CONF;

                cc_      = 0;
                cc_size_ = 0;

                const gcs_act_conf_t* const cc(
                    reinterpret_cast<const gcs_act_conf_t*>(act.buf));

                if (cc->my_idx < 0)
                {
                    assert (0 == cc->memb_num);
                    state_ = S_CLOSED;
                }
                else
                {
                    assert (1 == cc->memb_num);
                    state_ = S_CONNECTED;
                }

                return act.size;
            }
            else if (S_CONNECTED == state_)
            {
                ssize_t ret = generate_seqno_action(act, GCS_ACT_SYNC);
                if (ret > 0)  state_ = S_SYNCED;
                return ret;
            }
            else if (report_last_applied_)
            {
                report_last_applied_ = false;
                return generate_seqno_action(act, GCS_ACT_COMMIT_CUT);
            }
        }
        while (state_ > S_OPEN && (lock.wait(cond_), true));

        switch (state_)
        {
        case S_OPEN:   return -ENOTCONN;
        case S_CLOSED: return 0;
        default: abort();
        }
    }

    ssize_t
    DummyGcs::interrupt(ssize_t handle)
    {
        log_fatal << "Attempt to interrupt handle: " << handle;
        abort();
        return -ENOSYS;
    }
}

