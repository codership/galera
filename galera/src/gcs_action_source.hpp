//
// Copyright (C) 2010-2018 Codership Oy <info@codership.com>
//

#ifndef GALERA_GCS_ACTION_SOURCE_HPP
#define GALERA_GCS_ACTION_SOURCE_HPP

#include "action_source.hpp"
#include "galera_gcs.hpp"
#ifndef NDEBUG
#include "replicator.hpp"
#define REPL_IMPL Replicator
#else
#include "replicator_smm.hpp"
#define REPL_IMPL ReplicatorSMM
#endif
#include "trx_handle.hpp"

#include "GCache.hpp"

#include "gu_atomic.hpp"

namespace galera
{
    class GcsActionSource : public galera::ActionSource
    {
    public:

        GcsActionSource(TrxHandleSlave::Pool& sp,
                        GCS_IMPL&             gcs,
                        REPL_IMPL&            replicator,
                        gcache::GCache&       gcache)
            :
            trx_pool_      (sp        ),
            gcs_           (gcs       ),
            replicator_    (replicator),
            gcache_        (gcache    ),
            received_      (0         ),
            received_bytes_(0         )
        { }

        ~GcsActionSource()
        {
            log_info << trx_pool_;
        }

        ssize_t   process(void*, bool& exit_loop);
        long long received()       const { return received_(); }
        long long received_bytes() const { return received_bytes_(); }

    private:

        void process_writeset(void*                    recv_ctx,
                              const struct gcs_action& act,
                              bool&                    exit_loop);
        void resend_writeset(const struct gcs_action& act);

        void dispatch(void*, const gcs_action&, bool& exit_loop);

        TrxHandleSlave::Pool& trx_pool_;
        GCS_IMPL&             gcs_;
        REPL_IMPL&            replicator_;
        gcache::GCache&       gcache_;
        gu::Atomic<long long> received_;
        gu::Atomic<long long> received_bytes_;
    };
}

#endif // GALERA_GCS_ACTION_SOURCE_HPP
