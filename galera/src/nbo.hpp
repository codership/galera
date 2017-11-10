//
// Copyright (C) 2015 Codership Oy <info@codership.com>
//

#ifndef GALERA_NBO_HPP
#define GALERA_NBO_HPP

#include "galera_view.hpp"

#include "gu_buffer.hpp"
#include "gu_serialize.hpp"
#include "gu_logger.hpp"
#include "gu_lock.hpp"

#include "trx_handle.hpp"

#include "wsrep_api.h"

#include <map>

namespace galera
{
    class TrxHandleSlave;
    class MappedBuffer;

    // Helper datatypes for NBO

    // Context to be shared between cert NBOEntry and TrxHandleSlave
    // to signal ending of NBO.
    class NBOCtx
    {
    public:
        NBOCtx()
            :
            mutex_(),
            cond_ (),
            ts_   (),
            aborted_(false)
        { }


        void set_ts(const TrxHandleSlavePtr& ts)
        {
            gu::Lock lock(mutex_);
            assert(ts != 0);
            assert(ts->global_seqno() != WSREP_SEQNO_UNDEFINED);
            ts_ = ts;
            cond_.broadcast();
        }

        wsrep_seqno_t seqno() const
        {
            gu::Lock lock(mutex_);
            return (ts_ == 0 ? WSREP_SEQNO_UNDEFINED : ts_->global_seqno());
        }

        TrxHandleSlavePtr wait_ts()
        {
            gu::Lock lock(mutex_);
            while (ts_ == 0)
            {
                try
                {
                    lock.wait(cond_, gu::datetime::Date::calendar()
                              + gu::datetime::Sec);
                }
                catch (const gu::Exception& e)
                {
                    if (e.get_errno() == ETIMEDOUT)
                    {
                        return TrxHandleSlavePtr();
                    }
                    throw;
                }
            }
            return ts_;
        }

        void set_aborted(bool val)
        {
            gu::Lock lock(mutex_);
            aborted_= val;
            cond_.broadcast();
        }

        bool aborted() const
        {
            gu::Lock lock(mutex_);
            return aborted_;
        }

    private:
        NBOCtx(const NBOCtx&);
        NBOCtx& operator=(const NBOCtx&);

        gu::Mutex         mutex_;
        gu::Cond          cond_;
        TrxHandleSlavePtr ts_;
        bool              aborted_;
    };

    // Key for NBOMap
    class NBOKey
    {
    public:
        NBOKey() : seqno_(WSREP_SEQNO_UNDEFINED) { }

        NBOKey(const wsrep_seqno_t seqno)
            :
            seqno_(seqno)
        { }

        wsrep_seqno_t seqno() const { return seqno_; }


        bool operator<(const NBOKey& other) const
        {
            return (seqno_ < other.seqno_);
        }

        size_t serialize(gu::byte_t* buf, size_t buf_len, size_t offset)
        {
            return gu::serialize8(seqno_, buf, buf_len, offset);
        }

        size_t unserialize(const gu::byte_t* buf, size_t buf_len, size_t offset)
        {
            return gu::unserialize8(buf, buf_len, offset, seqno_);
        }
        static size_t serial_size()
        {
            return 8; //gu::serial_size8(wsrep_seqno_t());
        }

    private:
        wsrep_seqno_t seqno_;
    };


    // Entry for NBOMap
    class NBOEntry
    {
    public:
        NBOEntry(
            gu::shared_ptr<TrxHandleSlave>::type ts,
            gu::shared_ptr<MappedBuffer>::type buf,
            gu::shared_ptr<NBOCtx>::type nbo_ctx)
            :
            ts_ (ts),
            buf_(buf),
            ended_set_(),
            nbo_ctx_(nbo_ctx)
        { }
        TrxHandleSlave* ts_ptr() { return ts_.get(); }
        // const TrxHandleSlave* ts_ptr() const { return ts_.get(); }
        void add_ended(const wsrep_uuid_t& uuid)
        {
            std::pair<View::MembSet::iterator, bool> ret(
                ended_set_.insert(uuid));
            if (ret.second == false)
            {
                log_warn << "duplicate entry "
                         << uuid << " for ended set";
            }
        }

        void clear_ended()
        {
            ended_set_.clear();
        }
        const View::MembSet& ended_set() const { return ended_set_; }
        void end(const TrxHandleSlavePtr& ts)
        {
            assert(ts != 0);
            nbo_ctx_->set_ts(ts);
        }
        gu::shared_ptr<NBOCtx>::type nbo_ctx() { return nbo_ctx_; }

    private:
        gu::shared_ptr<TrxHandleSlave>::type ts_;
        gu::shared_ptr<MappedBuffer>::type   buf_;
        View::MembSet                        ended_set_;
        gu::shared_ptr<NBOCtx>::type         nbo_ctx_;
    };

    typedef std::map<NBOKey, gu::shared_ptr<NBOCtx>::type> NBOCtxMap;
    typedef std::map<NBOKey, NBOEntry> NBOMap;
}


#endif // !GALERA_NBO_HPP
