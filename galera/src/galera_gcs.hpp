//
// Copyright (C) 2010-2014 Codership Oy <info@codership.com>
//

#ifndef GALERA_GCS_HPP
#define GALERA_GCS_HPP

#include "write_set_ng.hpp"
#include "wsrep_api.h"
#include "gcs.hpp"
#include <gu_atomic.hpp>
#include <gu_throw.hpp>
#include <gu_config.hpp>
#include <gu_buf.hpp>
#include <gu_status.hpp>
#include <gu_lock.hpp> // for gu::Mutex and gu::Cond

#include <GCache.hpp>
#include <cerrno>

#define GCS_IMPL Gcs

namespace galera
{
    class GcsI
    {
    public:
        GcsI() {}
        virtual ~GcsI() {}
        virtual ssize_t connect(const std::string& cluster_name,
                                const std::string& cluster_url,
                                bool               bootstrap) = 0;
        virtual ssize_t set_initial_position(const gu::GTID& gtid) = 0;
        virtual void    close() = 0;
        virtual ssize_t recv(gcs_action& act) = 0;

        typedef WriteSetNG::GatherVector WriteSetVector;

        virtual ssize_t sendv(const WriteSetVector&, size_t,
                              gcs_act_type_t, bool, bool) = 0;
        virtual ssize_t send (const void*, size_t, gcs_act_type_t, bool) = 0;
        virtual ssize_t replv(const WriteSetVector&,
                              gcs_action& act, bool) = 0;
        virtual ssize_t repl (gcs_action& act, bool) = 0;
        virtual void    caused(gu::GTID& gtid,
                               gu::datetime::Date& wait_until) = 0;
        virtual ssize_t schedule() = 0;
        virtual ssize_t interrupt(ssize_t) = 0;
        virtual ssize_t resume_recv() = 0;
        virtual ssize_t request_state_transfer(int version,
                                               const void* req, ssize_t req_len,
                                               const std::string& sst_donor,
                                               const gu::GTID& ist_gtid,
                                               gcs_seqno_t& order) = 0;
        virtual ssize_t desync(gcs_seqno_t& seqno_l) = 0;
        virtual void    join(const gu::GTID&, int code) = 0;
        virtual gcs_seqno_t local_sequence() = 0;
        virtual ssize_t set_last_applied(const gu::GTID&) = 0;
        virtual int     vote(const gu::GTID& gtid,
                             uint64_t        code,
                             const void*     data,
                             size_t          data_len) = 0;
        virtual void    get_stats(gcs_stats*) const = 0;
        virtual void    flush_stats() = 0;
        virtual void    get_status(gu::Status&) const = 0;
        /*! @throws NotFound */
        virtual void    param_set (const std::string& key,
                                   const std::string& value) = 0;

        /*! @throws NotFound */
        virtual char*   param_get (const std::string& key) const = 0;

        virtual size_t  max_action_size() const = 0;
    };

    class Gcs : public GcsI
    {
    public:

        Gcs(gu::Config&     config,
            gcache::GCache& cache,
            int repl_proto_ver        = 0,
            int appl_proto_ver        = 0,
            const char* node_name     = 0,
            const char* node_incoming = 0)
            :
            conn_(gcs_create(reinterpret_cast<gu_config_t*>(&config),
                             reinterpret_cast<gcache_t*>(&cache),
                             node_name, node_incoming,
                             repl_proto_ver, appl_proto_ver))
        {
            log_info << "Passing config to GCS: " << config;
            if (conn_ == 0) gu_throw_fatal << "could not create gcs connection";
        }

        ~Gcs() { gcs_destroy(conn_); }

        ssize_t connect(const std::string& cluster_name,
                        const std::string& cluster_url,
                        bool  const        bootstrap)
        {
            return gcs_open(conn_,
                            cluster_name.c_str(),
                            cluster_url.c_str(),
                            bootstrap);
        }

        ssize_t set_initial_position(const gu::GTID& gtid)
        {
            return gcs_init(conn_, gtid);
        }

        void close()
        {
            gcs_close(conn_);
        }

        ssize_t recv(struct gcs_action& act)
        {
            return gcs_recv(conn_, &act);
        }

        ssize_t sendv(const WriteSetVector& actv, size_t act_len,
                      gcs_act_type_t act_type, bool scheduled, bool grab)
        {
            return gcs_sendv(conn_, &actv[0], act_len, act_type, scheduled, grab);
        }

        ssize_t send(const void* act, size_t act_len, gcs_act_type_t act_type,
                     bool scheduled)
        {
            return gcs_send(conn_, act, act_len, act_type, scheduled);
        }

        ssize_t replv(const WriteSetVector& actv,
                      struct gcs_action& act, bool scheduled)
        {
            return gcs_replv(conn_, &actv[0], &act, scheduled);
        }

        ssize_t repl(struct gcs_action& act, bool scheduled)
        {
            return gcs_repl(conn_, &act, scheduled);
        }

        void caused(gu::GTID& gtid, gu::datetime::Date& wait_until)
        {
            long err;

            while ((err = gcs_caused(conn_, gtid)) == -EAGAIN &&
                   gu::datetime::Date::calendar() < wait_until)
            {
                usleep(1000);
            }

            if (err == -EAGAIN) err = -ETIMEDOUT;

            if (err < 0)
            {
                gu_throw_error(-err);
            }
        }

        ssize_t schedule()   { return gcs_schedule(conn_); }

        ssize_t interrupt(ssize_t handle)
        {
            return gcs_interrupt(conn_, handle);
        }

        ssize_t resume_recv()
        {
            return gcs_resume_recv(conn_);
        }

        ssize_t set_last_applied(const gu::GTID& gtid)
        {
            assert(gtid.uuid()  != GU_UUID_NIL);
            assert(gtid.seqno() >= 0);

            return gcs_set_last_applied(conn_, gtid);
        }

        int vote(const gu::GTID& gtid, uint64_t const code,
                 const void* const msg, size_t const msg_len)
        {
            assert(gtid.uuid()  != GU_UUID_NIL);
            assert(gtid.seqno() >= 0);

            return gcs_vote(conn_, gtid, code, msg, msg_len);
        }

        ssize_t request_state_transfer(int version,
                                       const void* req, ssize_t req_len,
                                       const std::string& sst_donor,
                                       const gu::GTID& ist_gtid,
                                       gcs_seqno_t& seqno_l)
        {
            return gcs_request_state_transfer(conn_,
                                              version,
                                              req, req_len,
                                              sst_donor.c_str(),
                                              ist_gtid,
                                              seqno_l);
        }

        ssize_t desync (gcs_seqno_t& seqno_l)
        {
            return gcs_desync(conn_, seqno_l);
        }

        void join (const gu::GTID& gtid, int const code)
        {
            long const err(gcs_join(conn_, gtid, code));

            if (err < 0)
            {
                gu_throw_error (-err) << "gcs_join(" << gtid << ") failed";
            }
        }

        gcs_seqno_t local_sequence()
        {
            return gcs_local_sequence(conn_);
        }

        void get_stats(gcs_stats* stats) const
        {
            return gcs_get_stats(conn_, stats);
        }

        void flush_stats()
        {
            return gcs_flush_stats(conn_);
        }

        void get_status(gu::Status& status) const
        {
            gcs_get_status(conn_, status);
        }

        void param_set (const std::string& key, const std::string& value)
        {
            long ret = gcs_param_set (conn_, key.c_str(), value.c_str());

            if (1 == ret)
            {
                throw gu::NotFound();
            }
            else if (ret)
            {
                gu_throw_error(-ret) << "Setting '" << key << "' to '"
                                     << value << "' failed";
            }
        }

        char* param_get (const std::string& key) const
        {
            gu_throw_error(ENOSYS) << "Not implemented: " << __FUNCTION__;
            return 0;
        }

        size_t  max_action_size() const { return GCS_MAX_ACT_SIZE; }

    private:

        Gcs(const Gcs&);
        void operator=(const Gcs&);

        gcs_conn_t* conn_;
    };

    class DummyGcs : public GcsI
    {
    public:

        DummyGcs(gu::Config&     config,
                 gcache::GCache& cache,
                 int repl_proto_ver        = 0,
                 int appl_proto_ver        = 0,
                 const char* node_name     = 0,
                 const char* node_incoming = 0);

        DummyGcs(); // for unit tests

        ~DummyGcs();

        ssize_t connect(const std::string& cluster_name,
                        const std::string& cluster_url,
                        bool               bootstrap);

        ssize_t set_initial_position(const gu::GTID& gtid);

        void close();

        ssize_t recv(gcs_action& act);

        ssize_t sendv(const WriteSetVector&, size_t, gcs_act_type_t, bool, bool)
        { return -ENOSYS; }

        ssize_t send(const void*, size_t, gcs_act_type_t, bool)
        { return -ENOSYS; }

        ssize_t replv(const WriteSetVector& actv,
                      gcs_action& act, bool scheduled)
        {
            ssize_t ret(set_seqnos(act));

            if (gu_likely(0 != gcache_ && ret > 0))
            {
                assert (ret == act.size);
                gu::byte_t* ptr(
                    reinterpret_cast<gu::byte_t*>(gcache_->malloc(act.size)));
                act.buf = ptr;
                ssize_t copied(0);
                for (int i(0); copied < act.size; ++i)
                {
                    memcpy (ptr + copied, actv[i].ptr, actv[i].size);
                    copied += actv[i].size;
                }
                assert (copied == act.size);
            }

            return ret;
        }

        ssize_t repl(gcs_action& act, bool scheduled)
        {
            ssize_t ret(set_seqnos(act));

            if (gu_likely(0 != gcache_ && ret > 0))
            {
                assert (ret == act.size);
                void* const ptr(gcache_->malloc(act.size));
                memcpy (ptr, act.buf, act.size);
                act.buf = ptr;
                // no freeing here - initial act.buf belongs to the caller
            }

            return ret;
        }

        void caused(gu::GTID& gtid, gu::datetime::Date& wait_until)
        {
            gtid.set(uuid_, global_seqno_);
        }

        ssize_t schedule()
        {
            return 1;
        }

        ssize_t interrupt(ssize_t handle);

        ssize_t resume_recv() { return 0; }

        ssize_t set_last_applied(const gu::GTID& gtid)
        {
            gu::Lock lock(mtx_);

            last_applied_ = gtid.seqno();
            report_last_applied_ = true;

            cond_.signal();

            return 0;
        }

        gcs_seqno_t last_applied() const { return last_applied_; }

        int vote(const gu::GTID& gtid, uint64_t const code,
                 const void* const msg, size_t const msg_len)
        {
            return 0; // we always agree with ourselves
        }

        ssize_t request_state_transfer(int version,
                                       const void* req, ssize_t req_len,
                                       const std::string& sst_donor,
                                       const gu::GTID& ist_gtid,
                                       gcs_seqno_t& seqno_l)
        {
            seqno_l = GCS_SEQNO_ILL;
            return -ENOSYS;
        }

        ssize_t desync (gcs_seqno_t& seqno_l)
        {
            seqno_l = GCS_SEQNO_ILL;
            return -ENOTCONN;
        }

        void join(const gu::GTID& gtid, int const code)
        {
            gu_throw_error(ENOTCONN);
        }

        gcs_seqno_t local_sequence()
        {
            gu::Lock lock(mtx_);
            return ++local_seqno_;
        }

        void get_stats(gcs_stats* stats) const
        {
            memset (stats, 0, sizeof(*stats));
        }

        void flush_stats() {}

        void get_status(gu::Status& status) const
        {}

        void param_set (const std::string& key, const std::string& value)
        {}

        char* param_get (const std::string& key) const { return 0; }

        size_t max_action_size() const { return 0x7FFFFFFF; }

    private:

        typedef enum
        {
            S_CLOSED,
            S_OPEN,
            S_CONNECTED,
            S_SYNCED
        } conn_state_t;

        ssize_t generate_seqno_action (gcs_action& act, gcs_act_type_t type);
        ssize_t generate_cc (bool primary);

        gu::Config*  gconf_;
        gcache::GCache* gcache_;
        gu::Mutex    mtx_;
        gu::Cond     cond_;
        gcs_seqno_t  global_seqno_;
        gcs_seqno_t  local_seqno_;
        gu::UUID     uuid_;
        gcs_seqno_t  last_applied_;
        conn_state_t state_;
        gu::Lock*    schedule_;
        void*        cc_;
        ssize_t      cc_size_;
        std::string const my_name_;
        std::string const incoming_;
        int          repl_proto_ver_;
        int          appl_proto_ver_;
        bool         report_last_applied_;

        ssize_t set_seqnos (gcs_action& act)
        {
            act.seqno_g = GCS_SEQNO_ILL;
            act.seqno_l = GCS_SEQNO_ILL;

            ssize_t ret(-EBADFD);

            {
                gu::Lock lock(mtx_);

                switch (state_)
                {
                case S_CONNECTED:
                case S_SYNCED:
                {
                    ++global_seqno_;
                    act.seqno_g = global_seqno_;
                    ++local_seqno_;
                    act.seqno_l = local_seqno_;
                    ret = act.size;
                    break;
                }
                case S_CLOSED:
                    ret = -EBADFD;
                    break;
                case S_OPEN:
                    ret = -ENOTCONN;
                    break;
                }
            }

            return ret;
        }

        DummyGcs (const DummyGcs&);
        DummyGcs& operator=(const DummyGcs&);
    };
}

#endif // GALERA_GCS_HPP
