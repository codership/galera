//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#ifndef GALERA_GCS_HPP
#define GALERA_GCS_HPP

#include "gu_atomic.hpp"
#include "gu_throw.hpp"
#include "gcs.h"
#include "wsrep_api.h"

#include <cerrno>

namespace galera
{
    class GcsI
    {
    public:
        GcsI() { }
        virtual ~GcsI() { }
        virtual int connect(const std::string& cluster_name,
                            const std::string& cluster_url) = 0;
        virtual ssize_t set_initial_position(const wsrep_uuid_t& uuid,
                                             gcs_seqno_t seqno) = 0;
        virtual void close() = 0;
        virtual ssize_t recv(void**, size_t*, gcs_act_type_t*,
                             gcs_seqno_t*, gcs_seqno_t*) = 0;
        virtual ssize_t send(const void*, size_t, gcs_act_type_t, bool) = 0;
        virtual ssize_t repl(const void*, size_t, gcs_act_type_t, bool,
                             gcs_seqno_t* seqno_l, gcs_seqno_t* seqno_g) = 0;
        virtual ssize_t schedule() = 0;
        virtual ssize_t interrupt(ssize_t) = 0;
        virtual ssize_t set_last_applied(gcs_seqno_t) = 0;
        virtual ssize_t request_state_transfer(const void* req, ssize_t req_len,
                                               const std::string& sst_donor,
                                               gcs_seqno_t* seqno_l) = 0;
        virtual ssize_t join(gcs_seqno_t seqno) = 0;
        virtual ssize_t queue_len() const = 0;
    };

    class Gcs : public GcsI
    {
    public:
        Gcs(const std::string& node_name,
            const std::string& node_incoming)
            :
            conn_(gcs_create(node_name.c_str(), node_incoming.c_str()))
        {
            if (conn_ == 0) gu_throw_fatal << "could not create gcs connection";
        }
        ~Gcs() { gcs_destroy(conn_); }
        ssize_t connect(const std::string& cluster_name,
                        const std::string& cluster_url)
        {
            return gcs_open(conn_, cluster_name.c_str(), cluster_url.c_str());
        }

        ssize_t set_initial_position(const wsrep_uuid_t& uuid,
                                     gcs_seqno_t seqno)
        {
            return gcs_init(conn_, seqno, uuid.uuid);
        }

        void close()
        {
            gcs_close(conn_);
        }
        ssize_t recv(void** act, size_t* act_len, gcs_act_type_t* act_type,
                     gcs_seqno_t* seqno_l, gcs_seqno_t* seqno_g)
        {
            // Note: seqno_l and seqno_g are reversed
            return gcs_recv(conn_, act, act_len, act_type, seqno_g, seqno_l);
        }
        ssize_t send(const void* act, size_t act_len, gcs_act_type_t act_type,
                     bool scheduled)
        {
            return gcs_send(conn_, act, scheduled, act_len, act_type);
        }
        ssize_t repl(const void* act, size_t act_len,
                     gcs_act_type_t act_type, bool scheduled,
                     gcs_seqno_t* seqno_l, gcs_seqno_t* seqno_g)
        {
            // Note: seqno_l and seqno_g are reversed
            return gcs_repl(conn_, act, act_len, act_type, scheduled, seqno_g,
                            seqno_l);
        }
        ssize_t schedule() { return gcs_schedule(conn_); }
        ssize_t interrupt(ssize_t handle)
        {
            return gcs_interrupt(conn_, handle);
        }
        ssize_t set_last_applied(gcs_seqno_t last_applied)
        {
            return gcs_set_last_applied(conn_, last_applied);
        }

        ssize_t request_state_transfer(const void* req, ssize_t req_len,
                                       const std::string& sst_donor,
                                       gcs_seqno_t* seqno_l)
        {
            return gcs_request_state_transfer(conn_, req, req_len,
                                              sst_donor.c_str(), seqno_l);
        }
        ssize_t join(gcs_seqno_t seqno) { return gcs_join(conn_, seqno); }
        ssize_t queue_len() const { return gcs_queue_len(conn_); }
    private:
        Gcs(const Gcs&);
        void operator=(const Gcs&);
        gcs_conn_t* conn_;
    };

    class DummyGcs : public GcsI
    {
    public:
        DummyGcs()
            :
            last_applied_(GCS_SEQNO_ILL)
        { }
        ~DummyGcs() {}
        int connect(const std::string& cluster_name,
                    const std::string& cluster_url) { return 0; }
        ssize_t set_initial_position(const wsrep_uuid_t& uuid,
                                     gcs_seqno_t seqno) { return 0; }
        void close() { }
        ssize_t recv(void**, size_t*, gcs_act_type_t*,
                     gcs_seqno_t*, gcs_seqno_t*) { return -ENOTCONN; }
        ssize_t send(const void*, size_t, gcs_act_type_t, bool) { return -ENOTCONN; }
        ssize_t repl(const void*, size_t, gcs_act_type_t, bool,
                     gcs_seqno_t* seqno_l, gcs_seqno_t* seqno_g) { return -ENOTCONN; }
        ssize_t schedule() { return -ENOTCONN; }
        ssize_t interrupt(ssize_t) { return -ENOTCONN; }
        ssize_t set_last_applied(gcs_seqno_t last_applied)
        {
            last_applied_ = last_applied;
            return 0;
        }
        gcs_seqno_t last_applied() const { return last_applied_(); }
        ssize_t request_state_transfer(const void* req, ssize_t req_len,
                                       const std::string& sst_donor,
                                       gcs_seqno_t* seqno_l)
        { return -ENOTCONN; }
        ssize_t join(gcs_seqno_t seqno) { return -ENOTCONN; }
        ssize_t queue_len() const { return 0; }
    private:
        gu::Atomic<gcs_seqno_t> last_applied_;
    };
}

#endif // GALERA_GCS_HPP
