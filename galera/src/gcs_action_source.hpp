//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#ifndef GALERA_GCS_ACTION_SOURCE_HPP
#define GALERA_GCS_ACTION_SOURCE_HPP

#include "action_source.hpp"
#include "gcs.hpp"
#include "replicator.hpp"

#include "GCache.hpp"

#include "gu_atomic.hpp"

namespace galera
{
    class GcsActionSource : public galera::ActionSource
    {
    public:
        GcsActionSource(Gcs& gcs, Replicator& replicator,
                        gcache::GCache& gcache)
            :
            gcs_           (gcs       ),
            replicator_    (replicator),
            gcache_        (gcache    ),
            received_      (0         ),
            received_bytes_(0         )
        { }
        ~GcsActionSource() { }
        ssize_t process(void*);
        long long received() const { return received_(); }
        long long received_bytes() const { return received_bytes_(); }
    private:
        void dispatch(void*, const void*, size_t, gcs_act_type_t,
                      wsrep_seqno_t, wsrep_seqno_t);
        Gcs&        gcs_;
        Replicator& replicator_;
        gcache::GCache&     gcache_;
        gu::Atomic<long long> received_;
        gu::Atomic<long long> received_bytes_;
    };

    class GcsActionTrx
    {
    public:
        GcsActionTrx(const void* act,
                     size_t act_size,
                     gcs_seqno_t seqno_l,
                     gcs_seqno_t seqno_g);
        ~GcsActionTrx();
        TrxHandle* trx() const { return trx_; }
    private:
        GcsActionTrx(const GcsActionTrx&);
        void operator=(const GcsActionTrx&);
        TrxHandle* trx_;
    };

}

#endif // GALERA_GCS_ACTION_SOURCE_HPP
