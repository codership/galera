//
// Copyright (C) 2010-2020 Codership Oy <info@codership.com>
//

#include "replicator_smm.hpp"
#include "uuid.hpp"
#include <gu_abort.h>

namespace galera {

bool
ReplicatorSMM::state_transfer_required(const wsrep_view_info_t& view_info)
{
    if (view_info.state_gap)
    {
        assert(view_info.view >= 0);

        if (state_uuid_ == view_info.state_id.uuid) // common history
        {
            wsrep_seqno_t const group_seqno(view_info.state_id.seqno);
            wsrep_seqno_t const local_seqno(STATE_SEQNO());

            return (local_seqno < group_seqno);
        }

        return true;
    }

    return false;
}

wsrep_status_t
ReplicatorSMM::sst_received(const wsrep_gtid_t& state_id,
                            const void*         state,
                            size_t              state_len,
                            int                 rcode)
{
    log_info << "SST received: " << state_id.uuid << ':' << state_id.seqno;

    gu::Lock lock(sst_mutex_);

    if (state_() != S_JOINING)
    {
        log_error << "not JOINING when sst_received() called, state: "
                  << state_();
        return WSREP_CONN_FAIL;
    }

    assert(rcode <= 0);
    if (rcode) { assert(state_id.seqno == WSREP_SEQNO_UNDEFINED); }

    sst_uuid_  = state_id.uuid;
    sst_seqno_ = rcode ? WSREP_SEQNO_UNDEFINED : state_id.seqno;
    sst_cond_.signal();

    return WSREP_OK;
}


class StateRequest_v0 : public ReplicatorSMM::StateRequest
{
public:
    StateRequest_v0 (const void* const sst_req, ssize_t const sst_req_len)
        : req_(sst_req), len_(sst_req_len)
    {}
    ~StateRequest_v0 () {}
    virtual const void* req     () const { return req_; }
    virtual ssize_t     len     () const { return len_; }
    virtual const void* sst_req () const { return req_; }
    virtual ssize_t     sst_len () const { return len_; }
    virtual const void* ist_req () const { return 0;    }
    virtual ssize_t     ist_len () const { return 0;    }
private:
    StateRequest_v0 (const StateRequest_v0&);
    StateRequest_v0& operator = (const StateRequest_v0&);
    const void* const req_;
    ssize_t     const len_;
};


class StateRequest_v1 : public ReplicatorSMM::StateRequest
{
public:
    static std::string const MAGIC;
    StateRequest_v1 (const void* sst_req, ssize_t sst_req_len,
                     const void* ist_req, ssize_t ist_req_len);
    StateRequest_v1 (const void* str, ssize_t str_len);
    ~StateRequest_v1 () { if (own_ && req_) free (req_); }
    virtual const void* req     () const { return req_; }
    virtual ssize_t     len     () const { return len_; }
    virtual const void* sst_req () const { return req(sst_offset()); }
    virtual ssize_t     sst_len () const { return len(sst_offset()); }
    virtual const void* ist_req () const { return req(ist_offset()); }
    virtual ssize_t     ist_len () const { return len(ist_offset()); }
private:
    StateRequest_v1 (const StateRequest_v1&);
    StateRequest_v1& operator = (const StateRequest_v1&);

    ssize_t sst_offset() const { return MAGIC.length() + 1; }
    ssize_t ist_offset() const
    {
        return sst_offset() + sizeof(uint32_t) + sst_len();
    }

    ssize_t len (ssize_t offset) const
    {
        return gtohl(*(reinterpret_cast<uint32_t*>(req_ + offset)));
    }

    void*   req (ssize_t offset) const
    {
        if (len(offset) > 0)
            return req_ + offset + sizeof(uint32_t);
        else
            return 0;
    }

    ssize_t const len_;
    char*   const req_;
    bool    const own_;
};

std::string const
StateRequest_v1::MAGIC("STRv1");

#ifndef INT32_MAX
#define INT32_MAX 0x7fffffff
#endif

StateRequest_v1::StateRequest_v1 (
    const void* const sst_req, ssize_t const sst_req_len,
    const void* const ist_req, ssize_t const ist_req_len)
    :
    len_(MAGIC.length() + 1 +
         sizeof(uint32_t) + sst_req_len +
         sizeof(uint32_t) + ist_req_len),
    req_(reinterpret_cast<char*>(malloc(len_))),
    own_(true)
{
    if (!req_)
        gu_throw_error (ENOMEM) << "Could not allocate state request v1";

    if (sst_req_len > INT32_MAX || sst_req_len < 0)
        gu_throw_error (EMSGSIZE) << "SST request length (" << sst_req_len
                               << ") unrepresentable";

    if (ist_req_len > INT32_MAX || ist_req_len < 0)
        gu_throw_error (EMSGSIZE) << "IST request length (" << sst_req_len
                               << ") unrepresentable";

    char* ptr(req_);

    strcpy (ptr, MAGIC.c_str());
    ptr += MAGIC.length() + 1;

    uint32_t* tmp(reinterpret_cast<uint32_t*>(ptr));
    *tmp = htogl(sst_req_len);
    ptr += sizeof(uint32_t);

    memcpy (ptr, sst_req, sst_req_len);
    ptr += sst_req_len;

    tmp = reinterpret_cast<uint32_t*>(ptr);
    *tmp = htogl(ist_req_len);
    ptr += sizeof(uint32_t);

    memcpy (ptr, ist_req, ist_req_len);

    assert ((ptr - req_) == (len_ - ist_req_len));
}

// takes ownership over str buffer
StateRequest_v1::StateRequest_v1 (const void* str, ssize_t str_len)
:
    len_(str_len),
    req_(reinterpret_cast<char*>(const_cast<void*>(str))),
    own_(false)
{
    if (sst_offset() + 2*sizeof(uint32_t) > size_t(len_))
    {
        assert(0);
        gu_throw_error (EINVAL) << "State transfer request is too short: "
                                << len_ << ", must be at least: "
                                << (sst_offset() + 2*sizeof(uint32_t));
    }

    if (strncmp (req_, MAGIC.c_str(), MAGIC.length()))
    {
        assert(0);
        gu_throw_error (EINVAL) << "Wrong magic signature in state request v1.";
    }

    if (sst_offset() + sst_len() + 2*sizeof(uint32_t) > size_t(len_))
    {
        gu_throw_error (EINVAL) << "Malformed state request v1: sst length: "
                                << sst_len() << ", total length: " << len_;
    }

    if (ist_offset() + ist_len() + sizeof(uint32_t) != size_t(len_))
    {
        gu_throw_error (EINVAL) << "Malformed state request v1: parsed field "
            "length " << sst_len() << " is not equal to total request length "
                                << len_;
    }
}


static ReplicatorSMM::StateRequest*
read_state_request (const void* const req, size_t const req_len)
{
    const char* const str(reinterpret_cast<const char*>(req));

    if (req_len > StateRequest_v1::MAGIC.length() &&
        !strncmp(str, StateRequest_v1::MAGIC.c_str(),
                 StateRequest_v1::MAGIC.length()))
    {
        return (new StateRequest_v1(req, req_len));
    }
    else
    {
        return (new StateRequest_v0(req, req_len));
    }
}


class IST_request
{
public:
    IST_request() : peer_(), uuid_(), last_applied_(), group_seqno_() { }
    IST_request(const std::string& peer,
                const wsrep_uuid_t& uuid,
                wsrep_seqno_t last_applied,
                wsrep_seqno_t group_seqno)
        :
        peer_(peer),
        uuid_(uuid),
        last_applied_(last_applied),
        group_seqno_(group_seqno)
    { }
    const std::string&  peer()  const { return peer_ ; }
    const wsrep_uuid_t& uuid()  const { return uuid_ ; }
    wsrep_seqno_t       last_applied() const { return last_applied_; }
    wsrep_seqno_t       group_seqno()  const { return group_seqno_; }
private:
    friend std::ostream& operator<<(std::ostream&, const IST_request&);
    friend std::istream& operator>>(std::istream&, IST_request&);
    std::string peer_;
    wsrep_uuid_t uuid_;
    wsrep_seqno_t last_applied_;
    wsrep_seqno_t group_seqno_;
};

std::ostream& operator<<(std::ostream& os, const IST_request& istr)
{
    return (os
            << istr.uuid_         << ":"
            << istr.last_applied_ << "-"
            << istr.group_seqno_  << "|"
            << istr.peer_);
}

std::istream& operator>>(std::istream& is, IST_request& istr)
{
    char c;
    return (is >> istr.uuid_ >> c >> istr.last_applied_
            >> c >> istr.group_seqno_ >> c >> istr.peer_);
}

static void
get_ist_request(const ReplicatorSMM::StateRequest* str, IST_request* istr)
{
  assert(str->ist_len());
  std::string ist_str(reinterpret_cast<const char*>(str->ist_req()),
                      str->ist_len());
  std::istringstream is(ist_str);
  is >> *istr;
}

static bool
sst_is_trivial (const void* const req, size_t const len)
{
    /* Check that the first string in request == ReplicatorSMM::TRIVIAL_SST */
    size_t const trivial_len = strlen(ReplicatorSMM::TRIVIAL_SST) + 1;
    return (len >= trivial_len &&
            !memcmp (req, ReplicatorSMM::TRIVIAL_SST, trivial_len));
}

wsrep_seqno_t
ReplicatorSMM::donate_sst(void* const         recv_ctx,
                          const StateRequest& streq,
                          const wsrep_gtid_t& state_id,
                          bool const          bypass)
{
    wsrep_cb_status const err(sst_donate_cb_(app_ctx_, recv_ctx,
                                             streq.sst_req(), streq.sst_len(),
                                             &state_id, 0, 0, bypass));
    wsrep_seqno_t const ret
        (WSREP_CB_SUCCESS == err ? state_id.seqno : -ECANCELED);
    if (ret < 0)
    {
        log_error << "SST " << (bypass ? "bypass " : "") << "failed: " << err;
    }

    return ret;
}

void ReplicatorSMM::process_state_req(void*       recv_ctx,
                                      const void* req,
                                      size_t      req_size,
                                      wsrep_seqno_t const seqno_l,
                                      wsrep_seqno_t const donor_seq)
{
    assert(recv_ctx != 0);
    assert(seqno_l > -1);
    assert(req != 0);

    LocalOrder lo(seqno_l);

    gu_trace(local_monitor_.enter(lo));
    apply_monitor_.drain(donor_seq);

    if (co_mode_ != CommitOrder::BYPASS) commit_monitor_.drain(donor_seq);

    state_.shift_to(S_DONOR);

    StateRequest* const streq (read_state_request (req, req_size));

    // somehow the following does not work, string is initialized beyond
    // the first \0:
    //std::string const req_str(reinterpret_cast<const char*>(streq->sst_req()),
    //                          streq->sst_len());
    // have to resort to C ways.

    char* const tmp(strndup(reinterpret_cast<const char*>(streq->sst_req()),
                            streq->sst_len()));
    std::string const req_str(tmp);
    free (tmp);

    bool const skip_state_transfer (sst_is_trivial(streq->sst_req(),
                                                   streq->sst_len())
                          /* compatibility with older garbd, to be removed in
                           * the next release (2.1)*/
                          || req_str == std::string(WSREP_STATE_TRANSFER_NONE)
                                   );

    wsrep_seqno_t rcode (0);
    bool join_now = true;

    if (!skip_state_transfer)
    {
        if (streq->ist_len())
        {
            IST_request istr;
            get_ist_request(streq, &istr);

            if (istr.uuid() == state_uuid_)
            {
                log_info << "IST request: " << istr;

                struct sgl
                {
                    gcache::GCache& gcache_;
                    bool            unlock_;

                    sgl(gcache::GCache& cache) : gcache_(cache), unlock_(false){}
                    ~sgl() { if (unlock_) gcache_.seqno_unlock(); }
                }
                seqno_lock_guard(gcache_);

                try
                {
                    gcache_.seqno_lock(istr.last_applied() + 1);
                    seqno_lock_guard.unlock_ = true;
                }
                catch(gu::NotFound& nf)
                {
                    log_info << "IST first seqno " << istr.last_applied() + 1
                             << " not found from cache, falling back to SST";
                    // @todo: close IST channel explicitly
                    goto full_sst;
                }

                if (streq->sst_len()) // if joiner is waiting for SST, notify it
                {
                    wsrep_gtid_t const state_id =
                        { istr.uuid(), istr.last_applied() };

                    rcode = donate_sst(recv_ctx, *streq, state_id, true);

                    // we will join in sst_sent.
                    join_now = false;
                }

                if (rcode >= 0)
                {
                    try
                    {
                        // Note: End of IST range must be cc_seqno_ instead
                        // of istr.group_seqno() in case there are CCs between
                        // sending and delivering STR. If there are no
                        // intermediate CCs, cc_seqno_ == istr.group_seqno().
                        // Then duplicate message concern in #746 will be
                        // releaved.
                        ist_senders_.run(config_,
                                         istr.peer(),
                                         istr.last_applied() + 1,
                                         cc_seqno_,
                                         protocol_version_);

                        // seqno will be unlocked when sender exists
                        seqno_lock_guard.unlock_ = false;
                    }
                    catch (gu::Exception& e)
                    {
                        log_error << "IST failed: " << e.what();
                        rcode = -e.get_errno();
                    }
                }
                else
                {
                    log_error << "Failed to bypass SST";
                }

                goto out;
            }
        }

    full_sst:

        if (streq->sst_len())
        {
            assert(0 == rcode);

            wsrep_gtid_t const state_id = { state_uuid_, donor_seq };

            rcode = donate_sst(recv_ctx, *streq, state_id, false);

            // we will join in sst_sent.
            join_now = false;
        }
        else
        {
            log_warn << "SST request is null, SST canceled.";
            rcode = -ECANCELED;
        }
    }

out:
    delete streq;

    local_monitor_.leave(lo);

    if (join_now || rcode < 0)
    {
        gcs_.join(rcode < 0 ? rcode : donor_seq);
    }
}


void
ReplicatorSMM::prepare_for_IST (void*& ptr, ssize_t& len,
                                const wsrep_uuid_t& group_uuid,
                                wsrep_seqno_t const group_seqno)
{
    if (state_uuid_ != group_uuid)
    {
        gu_throw_error (EPERM) << "Local state UUID (" << state_uuid_
                               << ") does not match group state UUID ("
                               << group_uuid << ')';
    }

    wsrep_seqno_t const local_seqno(STATE_SEQNO());

    if (local_seqno < 0)
    {
        gu_throw_error (EPERM) << "Local state seqno is undefined";
    }

    assert(local_seqno < group_seqno);

    std::ostringstream os;

    std::string recv_addr = ist_receiver_.prepare(
        local_seqno + 1, group_seqno, protocol_version_);

    os << IST_request(recv_addr, state_uuid_, local_seqno, group_seqno);

    char* str = strdup (os.str().c_str());

    // cppcheck-suppress nullPointer
    if (!str) gu_throw_error (ENOMEM) << "Failed to allocate IST buffer.";

    len = strlen(str) + 1;

    ptr = str;
}


ReplicatorSMM::StateRequest*
ReplicatorSMM::prepare_state_request (const void* const   sst_req,
                                      ssize_t     const   sst_req_len,
                                      const wsrep_uuid_t& group_uuid,
                                      wsrep_seqno_t const group_seqno)
{
    try
    {
        switch (str_proto_ver_)
        {
        case 0:
            return new StateRequest_v0 (sst_req, sst_req_len);
        case 1:
        case 2:
        {
            void*   ist_req(0);
            ssize_t ist_req_len(0);

            try
            {
                gu_trace(prepare_for_IST (ist_req, ist_req_len,
                                          group_uuid, group_seqno));
            }
            catch (gu::Exception& e)
            {
                log_warn
                    << "Failed to prepare for incremental state transfer: "
                    << e.what() << ". IST will be unavailable.";
            }

            StateRequest* ret = new StateRequest_v1 (sst_req, sst_req_len,
                                                     ist_req, ist_req_len);
            free (ist_req);
            return ret;
        }
        default:
            gu_throw_fatal << "Unsupported STR protocol: " << str_proto_ver_;
        }
    }
    catch (std::exception& e)
    {
        log_fatal << "State request preparation failed, aborting: " << e.what();
    }
    catch (...)
    {
        log_fatal << "State request preparation failed, aborting: unknown exception";
    }
    abort();
}

static bool
retry_str(int ret)
{
    return (ret == -EAGAIN || ret == -ENOTCONN);
}

void
ReplicatorSMM::send_state_request (const StateRequest* const req)
{
    long ret;
    long tries = 0;

    gu_uuid_t ist_uuid = {{0, }};
    gcs_seqno_t ist_seqno = GCS_SEQNO_ILL;

    if (req->ist_len())
    {
      IST_request istr;
      get_ist_request(req, &istr);
      ist_uuid = to_gu_uuid(istr.uuid());
      ist_seqno = istr.last_applied();
    }

    do
    {
        tries++;

        gcs_seqno_t seqno_l;

        ret = gcs_.request_state_transfer(str_proto_ver_,
                                          req->req(), req->len(), sst_donor_,
                                          ist_uuid, ist_seqno, &seqno_l);
        if (ret < 0)
        {
            if (!retry_str(ret))
            {
                log_error << "Requesting state transfer failed: "
                          << ret << "(" << strerror(-ret) << ")";
            }
            else if (1 == tries)
            {
                log_info << "Requesting state transfer failed: "
                         << ret << "(" << strerror(-ret) << "). "
                         << "Will keep retrying every " << sst_retry_sec_
                         << " second(s)";
            }
        }

        if (seqno_l != GCS_SEQNO_ILL)
        {
            /* Check that we're not running out of space in monitor. */
            if (local_monitor_.would_block(seqno_l))
            {
                log_error << "Slave queue grew too long while trying to "
                          << "request state transfer " << tries << " time(s). "
                          << "Please make sure that there is "
                          << "at least one fully synced member in the group. "
                          << "Application must be restarted.";
                ret = -EDEADLK;
            }
            else
            {
                // we are already holding local monitor
                LocalOrder lo(seqno_l);
                local_monitor_.self_cancel(lo);
            }
        }
    }
    while (retry_str(ret) && (usleep(sst_retry_sec_ * 1000000), true));

    if (ret >= 0)
    {
        if (1 == tries)
        {
            log_info << "Requesting state transfer: success, donor: " << ret;
        }
        else
        {
            log_info << "Requesting state transfer: success after "
                     << tries << " tries, donor: " << ret;
        }
    }
    else
    {
        sst_state_ = SST_REQ_FAILED;

        st_.set(state_uuid_, STATE_SEQNO(), safe_to_bootstrap_);
        st_.mark_safe();

        if (state_() > S_CLOSING)
        {
            log_fatal << "State transfer request failed unrecoverably: "
                      << -ret << " (" << strerror(-ret) << "). Most likely "
                      << "it is due to inability to communicate with the "
                      << "cluster primary component. Restart required.";
            abort();
        }
        else
        {
            // connection is being closed, send failure is expected
        }
    }
}


void
ReplicatorSMM::request_state_transfer (void* recv_ctx,
                                       const wsrep_uuid_t& group_uuid,
                                       wsrep_seqno_t const group_seqno,
                                       const void*   const sst_req,
                                       ssize_t       const sst_req_len)
{
    assert(sst_req_len >= 0);

    StateRequest* const req(prepare_state_request(sst_req, sst_req_len,
                                                  group_uuid, group_seqno));
    gu::Lock lock(sst_mutex_);

    st_.mark_unsafe();

    GU_DBUG_SYNC_WAIT("before_send_state_request");
    send_state_request (req);

    state_.shift_to(S_JOINING);
    sst_state_ = SST_WAIT;
    GU_DBUG_SYNC_WAIT("after_shift_to_joining");

    /* while waiting for state transfer to complete is a good point
     * to reset gcache, since it may involve some IO too */
    gcache_.seqno_reset(to_gu_uuid(group_uuid), group_seqno);

    if (sst_req_len != 0)
    {

        if (sst_is_trivial(sst_req, sst_req_len))
        {
            sst_uuid_  = group_uuid;
            sst_seqno_ = group_seqno;
        }
        else
        {
            lock.wait(sst_cond_);
        }

        if (sst_uuid_ != group_uuid)
        {
            log_fatal << "Application received wrong state: "
                      << "\n\tReceived: " << sst_uuid_
                      << "\n\tRequired: " << group_uuid;
            sst_state_ = SST_FAILED;
            log_fatal << "Application state transfer failed. This is "
                      << "unrecoverable condition, restart required.";

            st_.set(sst_uuid_, sst_seqno_, safe_to_bootstrap_);
            st_.mark_safe();

            abort();
        }
        else
        {
            update_state_uuid (sst_uuid_);
            apply_monitor_.set_initial_position(-1);
            apply_monitor_.set_initial_position(sst_seqno_);

            if (co_mode_ != CommitOrder::BYPASS)
            {
                commit_monitor_.set_initial_position(-1);
                commit_monitor_.set_initial_position(sst_seqno_);
            }

            log_debug << "Installed new state: " << state_uuid_ << ":"
                      << sst_seqno_;
        }
    }
    else
    {
        assert (state_uuid_ == group_uuid);
    }

    st_.mark_safe();

    if (req->ist_len() > 0)
    {
        // IST is prepared only with str proto ver 1 and above
        if (STATE_SEQNO() < group_seqno)
        {
            log_info << "Receiving IST: " << (group_seqno - STATE_SEQNO())
                     << " writesets, seqnos " << STATE_SEQNO()
                     << "-" << group_seqno;
            ist_receiver_.ready();
            recv_IST(recv_ctx);
            sst_seqno_ = ist_receiver_.finished();

            // Note: apply_monitor_ must be drained to avoid race between
            // IST appliers and GCS appliers, GCS action source may
            // provide actions that have already been applied.
            apply_monitor_.drain(sst_seqno_);
            log_info << "IST received: " << state_uuid_ << ":" << sst_seqno_;
        }
        else
        {
            (void)ist_receiver_.finished();
        }
    }

    delete req;
}


void ReplicatorSMM::recv_IST(void* recv_ctx)
{
    while (true)
    {
        TrxHandle* trx(0);
        int err;
        try
        {
            if ((err = ist_receiver_.recv(&trx)) == 0)
            {
                assert(trx != 0);
                TrxHandleLock lock(*trx);
                // Verify checksum before applying. This is also required
                // to synchronize with possible background checksum thread.
                trx->verify_checksum();
                if (trx->depends_seqno() == -1)
                {
                    ApplyOrder ao(*trx);
                    apply_monitor_.self_cancel(ao);
                    if (co_mode_ != CommitOrder::BYPASS)
                    {
                        CommitOrder co(*trx, co_mode_);
                        commit_monitor_.self_cancel(co);
                    }
                }
                else
                {
                    // replicating and certifying stages have been
                    // processed on donor, just adjust states here
                    trx->set_state(TrxHandle::S_REPLICATING);
                    trx->set_state(TrxHandle::S_CERTIFYING);
                    apply_trx(recv_ctx, trx);
                    GU_DBUG_SYNC_WAIT("recv_IST_after_apply_trx");
                }
            }
            else
            {
                return;
            }
            trx->unref();
        }
        catch (gu::Exception& e)
        {
            log_fatal << "receiving IST failed, node restart required: "
                      << e.what();
            if (trx)
            {
                log_fatal << "failed trx: " << *trx;
            }
            st_.mark_corrupt();
            abort();
        }
    }
}
} /* namespace galera */
