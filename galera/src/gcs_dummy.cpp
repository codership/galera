//
// Copyright (C) 2011-2015 Codership Oy <info@codership.com>
//

#include "galera_gcs.hpp"

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
        uuid_          (NULL, 0),
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
    {}

    DummyGcs::DummyGcs()
        :
        gconf_         (0),
        gcache_        (0),
        mtx_           (),
        cond_          (),
        global_seqno_  (0),
        local_seqno_   (0),
        uuid_          (NULL, 0),
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
    {}

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
        gcs_act_cchange cc;

        gcs_node_state_t const my_state
            (primary ? GCS_NODE_STATE_JOINED : GCS_NODE_STATE_NON_PRIM);

        if (primary)
        {
            ++global_seqno_;

            cc.seqno   = global_seqno_;
            cc.conf_id = 1;
            cc.uuid    = *uuid_.ptr();
            cc.repl_proto_ver = repl_proto_ver_;
            cc.appl_proto_ver = appl_proto_ver_;

            /* we have single member here */
            gcs_act_cchange::member m;

            m.uuid_     = *uuid_.ptr();
            m.name_     = my_name_;
            m.incoming_ = incoming_;
            m.state_    = my_state;

            cc.memb.push_back(m);
        }
        else
        {
            cc.seqno    = GCS_SEQNO_ILL;
            cc.conf_id  = -1;
        }

        cc_size_ = cc.write(&cc_);

        if (!cc_)
        {
            cc_size_ = 0;
            return -ENOMEM;
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
            cond_.signal();
            ret = 0;
        }

        return ret;
    }

    ssize_t
    DummyGcs::set_initial_position(const gu::GTID& gtid)
    {
        gu::Lock lock(mtx_);

        if (gtid.uuid() != GU_UUID_NIL && gtid.seqno() >= 0)
        {
            uuid_ = gtid.uuid();
            global_seqno_ = gtid.seqno();
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
        gcs_seqno_t* const seqno
            (static_cast<gcs_seqno_t*>(::malloc(sizeof(gcs_seqno_t))));

        if (!seqno) return -ENOMEM;

        *seqno      = global_seqno_;
        ++local_seqno_;

        act.buf     = seqno;
        act.size    = sizeof(*seqno);
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
                act.type    = GCS_ACT_CCHANGE;

                cc_      = 0;
                cc_size_ = 0;

                gcs_act_cchange const cc(act.buf, act.size);

                act.seqno_g = (cc.conf_id >= 0 ? 0 : -1);

                int const my_idx(act.seqno_g);

                if (my_idx < 0)
                {
                    assert (0 == cc.memb.size());
                    state_ = S_CLOSED;
                }
                else
                {
                    assert (1 == cc.memb.size());
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

