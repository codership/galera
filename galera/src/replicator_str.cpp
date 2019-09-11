//
// Copyright (C) 2010-2019 Codership Oy <info@codership.com>
//

#include "replicator_smm.hpp"
#include "galera_info.hpp"

#include <gu_abort.h>
#include <gu_throw.hpp>

namespace galera {

bool
ReplicatorSMM::state_transfer_required(const wsrep_view_info_t& view_info,
                                       bool const rejoined)
{
    if (rejoined)
    {
        assert(view_info.view >= 0);

        if (state_uuid_ == view_info.state_id.uuid) // common history
        {
            wsrep_seqno_t const group_seqno(view_info.state_id.seqno);
            wsrep_seqno_t const local_seqno(last_committed());

            if (state_() >= S_JOINING) /* See #442 - S_JOINING should be
                                          a valid state here */
            {
                if (str_proto_ver_ >= 3)
                    return (local_seqno + 1 < group_seqno); // this CC will add 1
                else
                    return (local_seqno < group_seqno);
            }
            else
            {
                if ((str_proto_ver_ >= 3 && local_seqno >= group_seqno) ||
                    (str_proto_ver_ <  3 && local_seqno >  group_seqno))
                {
                    close();
                    gu_throw_fatal
                        << "Local state seqno (" << local_seqno
                        << ") is greater than group seqno (" <<group_seqno
                        << "): states diverged. Aborting to avoid potential "
                        << "data loss. Remove '" << state_file_
                        << "' file and restart if you wish to continue.";
                }

                return (local_seqno != group_seqno);
            }
        }

        return true;
    }

    return false;
}

wsrep_status_t
ReplicatorSMM::sst_received(const wsrep_gtid_t& state_id,
                            const wsrep_buf_t* const state,
                            int                const rcode)
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
    assert(false == sst_received_);
    sst_received_ = true;
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
    virtual int         version () const { return 0;    }
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
    virtual int         version () const { return 1;    }
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
        int32_t ret;
        gu::unserialize4(req_, offset, ret);
        return ret;
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
    req_(static_cast<char*>(malloc(len_))),
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

    ptr += gu::serialize4(uint32_t(sst_req_len), ptr, 0);

    memcpy (ptr, sst_req, sst_req_len);
    ptr += sst_req_len;

    ptr += gu::serialize4(uint32_t(ist_req_len), ptr, 0);

    memcpy (ptr, ist_req, ist_req_len);

    assert ((ptr - req_) == (len_ - ist_req_len));
}

// takes ownership over str buffer
StateRequest_v1::StateRequest_v1 (const void* str, ssize_t str_len)
:
    len_(str_len),
    req_(static_cast<char*>(const_cast<void*>(str))),
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
    const char* const str(static_cast<const char*>(req));

    bool const v1(req_len > StateRequest_v1::MAGIC.length() &&
                  !strncmp(str, StateRequest_v1::MAGIC.c_str(),
                           StateRequest_v1::MAGIC.length()));

    log_info << "Detected STR version: " << int(v1) << ", req_len: "
             << req_len << ", req: " << str;
    if (v1)
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
                wsrep_seqno_t last_missing_seqno)
        :
        peer_(peer),
        uuid_(uuid),
        last_applied_(last_applied),
        group_seqno_(last_missing_seqno)
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
  std::string ist_str(static_cast<const char*>(str->ist_req()),
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
    wsrep_buf_t const str = { streq.sst_req(), size_t(streq.sst_len()) };

    wsrep_cb_status const err(sst_donate_cb_(app_ctx_, recv_ctx, &str,
                                             &state_id, NULL, bypass));

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

    StateRequest* const streq(read_state_request(req, req_size));

    LocalOrder lo(seqno_l);

    gu_trace(local_monitor_.enter(lo));
    apply_monitor_.drain(donor_seq);

    if (co_mode_ != CommitOrder::BYPASS) commit_monitor_.drain(donor_seq);

    state_.shift_to(S_DONOR);

    // somehow the following does not work, string is initialized beyond
    // the first \0:
    //std::string const req_str(static_cast<const char*>(streq->sst_req()),
    //                          streq->sst_len());
    // have to resort to C ways.

    char* const tmp(strndup(static_cast<const char*>(streq->sst_req()),
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

            if (istr.uuid() == state_uuid_ && istr.last_applied() >= 0)
            {
                log_info << "IST request: " << istr;

                try
                {
                    gcache_.seqno_lock(istr.last_applied() + 1);
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
                    wsrep_seqno_t const first
                        ((str_proto_ver_ < 3 || cc_lowest_trx_seqno_ == 0) ?
                         istr.last_applied() + 1 :
                         std::min(cc_lowest_trx_seqno_, istr.last_applied()+1));
                    try
                    {
                        ist_senders_.run(config_,
                                         istr.peer(),
                                         first,
                                         cc_seqno_,
                                         cc_lowest_trx_seqno_,
                                         /* Historically IST messages versioned
                                          * with the global replicator protocol.
                                          * Need to keep it that way for backward
                                          * compatibility */
                                         protocol_version_);
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

        if (cert_.nbo_size() > 0)
        {
            log_warn << "Non-blocking operation in progress, cannot donate SST";
            rcode = -EAGAIN;
        }
        else if (streq->sst_len())
        {
            assert(0 == rcode);

            wsrep_gtid_t const state_id = { state_uuid_, donor_seq };

            if (str_proto_ver_ >= 3)
            {
                if (streq->version() > 0)
                {
                    if (streq->ist_len() <= 0)
                    {
                        log_warn << "Joiner didn't provide IST connection info -"
                            " cert. index preload impossible, bailing out.";
                        rcode = -ENOMSG;
                        goto out;
                    }

                    wsrep_seqno_t preload_start(cc_lowest_trx_seqno_);

                    try
                    {
                        if (preload_start <= 0)
                        {
                            preload_start = cc_seqno_;
                        }

                        gcache_.seqno_lock(preload_start);
                    }
                    catch (gu::NotFound& nf)
                    {
                        log_warn << "Cert index preload first seqno "
                                 << preload_start
                                 << " not found from gcache (min available: "
                                 << gcache_.seqno_min() << ')';
                        rcode = -ENOMSG;
                        goto out;
                    }

                    log_info << "Cert index preload: " << preload_start
                             << " -> " << cc_seqno_;

                    IST_request istr;
                    get_ist_request(streq, &istr);
                    // Send trxs to rebuild cert index.
                    ist_senders_.run(config_,
                                     istr.peer(),
                                     preload_start,
                                     cc_seqno_,
                                     preload_start,
                                     /* Historically IST messages are versioned
                                      * with the global replicator protocol.
                                      * Need to keep it that way for backward
                                      * compatibility */
                                     protocol_version_);
                }
                else /* streq->version() == 0 */
                {
                    log_info << "STR v0: assuming backup request, skipping "
                        "cert. index preload.";
                }
            }

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
        gcs_.join(gu::GTID(state_uuid_, donor_seq), rcode);
    }
}


void
ReplicatorSMM::prepare_for_IST (void*& ptr, ssize_t& len,
                                const wsrep_uuid_t& group_uuid,
                                wsrep_seqno_t const last_needed)
{
    assert(group_uuid != GU_UUID_NIL);
    // Up from STR protocol version 3 joiner is assumed to be able receive
    // some transactions to rebuild cert index, so IST receiver must be
    // prepared regardless of the group.
    wsrep_seqno_t last_applied(last_committed());
    ist_event_queue_.reset();
    if (state_uuid_ != group_uuid)
    {
        if (str_proto_ver_ < 3)
        {
            gu_throw_error (EPERM) << "Local state UUID (" << state_uuid_
                                   << ") does not match group state UUID ("
                                   << group_uuid << ')';
        }
        else
        {
            last_applied = -1; // to cause full SST
        }
    }
    else
    {
        assert(last_applied < last_needed);
    }

    if (last_applied < 0 && str_proto_ver_ < 3)
    {
        gu_throw_error (EPERM) << "Local state seqno is undefined";
    }

    wsrep_seqno_t const first_needed(last_applied + 1);

    log_info << "####### IST uuid:" << state_uuid_ << " f: " << first_needed
             << ", l: " << last_needed << ", STRv: " << str_proto_ver_; //remove

    /* Historically IST messages are versioned with the global replicator
     * protocol. Need to keep it that way for backward compatibility */
    std::string recv_addr(ist_receiver_.prepare(first_needed, last_needed,
                                                protocol_version_,source_id()));

    std::ostringstream os;

    /* NOTE: in case last_applied is -1, first_needed is 0, but first legal
     * cached seqno is 1 so donor will revert to SST anyways, as is required */
    os << IST_request(recv_addr, state_uuid_, last_applied, last_needed);

    char* str = strdup (os.str().c_str());

    // cppcheck-suppress nullPointer
    if (!str) gu_throw_error (ENOMEM) << "Failed to allocate IST buffer.";

    log_debug << "Prepared IST request: " << str;

    len = strlen(str) + 1;

    ptr = str;
}


ReplicatorSMM::StateRequest*
ReplicatorSMM::prepare_state_request (const void*         sst_req,
                                      ssize_t             sst_req_len,
                                      const wsrep_uuid_t& group_uuid,
                                      wsrep_seqno_t const last_needed_seqno)
{
    try
    {
        // IF there are ongoing NBO, SST might not be possible because
        // ongoing NBO is blocking and waiting for NBO end events.
        // Therefore in precense of ongoing NBOs we set SST request
        // string to zero and hope that donor can serve IST.

        size_t const nbo_size(cert_.nbo_size());
        if (nbo_size)
        {
            log_info << "Non-blocking operation is ongoing. "
                "Node can receive IST only.";

            sst_req     = NULL;
            sst_req_len = 0;
        }

        switch (str_proto_ver_)
        {
        case 0:
            if (0 == sst_req_len)
                gu_throw_error(EPERM) << "SST is not possible.";
            return new StateRequest_v0 (sst_req, sst_req_len);
        case 1:
        case 2:
        case 3:
        {
            void*   ist_req(0);
            ssize_t ist_req_len(0);

            try
            {
                gu_trace(prepare_for_IST (ist_req, ist_req_len,
                                          group_uuid, last_needed_seqno));
                assert(ist_req_len > 0);
                assert(NULL != ist_req);
            }
            catch (gu::Exception& e)
            {
                log_warn
                    << "Failed to prepare for incremental state transfer: "
                    << e.what() << ". IST will be unavailable.";

                if (0 == sst_req_len)
                    gu_throw_error(EPERM) << "neither SST nor IST is possible.";
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
        log_fatal << "State Transfer Request preparation failed: " << e.what()
                  << " Can't continue, aborting.";
    }
    catch (...)
    {
        log_fatal << "State Transfer Request preparation failed: "
            "unknown exception. Can't continue, aborting.";
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
      ist_uuid  = istr.uuid();
      ist_seqno = istr.last_applied();
    }

    do
    {
        tries++;

        gcs_seqno_t seqno_l;

        ret = gcs_.request_state_transfer(str_proto_ver_,
                                          req->req(), req->len(), sst_donor_,
                                          gu::GTID(ist_uuid, ist_seqno),seqno_l);
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

        st_.set(state_uuid_, last_committed(), safe_to_bootstrap_);
        st_.mark_safe();

        gu::Lock lock(closing_mutex_);

        if (!closing_ && state_() > S_CLOSED)
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
                                       wsrep_seqno_t const cc_seqno,
                                       const void*   const sst_req,
                                       ssize_t       const sst_req_len)
{
    assert(sst_req_len >= 0);

    StateRequest* const req(prepare_state_request(sst_req, sst_req_len,
                                                  group_uuid, cc_seqno));
    gu::Lock sst_lock(sst_mutex_);
    sst_received_ = false;

    st_.mark_unsafe();

    send_state_request(req);

    state_.shift_to(S_JOINING);
    sst_state_ = SST_WAIT;
    sst_seqno_ = WSREP_SEQNO_UNDEFINED;

    /* There are two places where we may need to adjust GCache.
     * This is the first one, which we can do while waiting for SST to complete.
     * Here we reset seqno map completely if we have different histories.
     * This MUST be done before IST starts. */
    bool const first_reset
        (state_uuid_ /* GCache has */ != group_uuid /* current PC has */);
    if (first_reset)
    {
        log_info << "Resetting GCache seqno map due to different histories.";
        gcache_.seqno_reset(gu::GTID(group_uuid, cc_seqno));
    }

    if (sst_req_len != 0)
    {
        if (sst_is_trivial(sst_req, sst_req_len))
        {
            sst_uuid_  = group_uuid;
            sst_seqno_ = cc_seqno;
            sst_received_ = true;
        }
        else
        {
            while (false == sst_received_) sst_lock.wait(sst_cond_);
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
            assert(sst_seqno_ != WSREP_SEQNO_UNDEFINED);

            /* There are two places where we may need to adjust GCache.
             * This is the second one.
             * Here we reset seqno map completely if we have gap in seqnos
             * between the received snapshot and current GCache contents.
             * This MUST be done before IST starts. */
            // there may be possible optimization to this when cert index
            // transfer is implemented (it may close the gap), but not by much.
            if (!first_reset && (last_committed() /* GCache has */ !=
                                 sst_seqno_    /* current state has */))
            {
                log_info << "Resetting GCache seqno map due to seqno gap: "
                         << last_committed() << ".." << sst_seqno_;
                gcache_.seqno_reset(gu::GTID(sst_uuid_, sst_seqno_));
            }

            update_state_uuid (sst_uuid_);

            if (str_proto_ver_ < 3)
            {
                // all IST events will bypass certification
                gu::GTID const cert_position
                    (sst_uuid_, std::max(cc_seqno, sst_seqno_));
                cert_.assign_initial_position(cert_position,
                                              trx_params_.version_);
                // with higher versions this happens in cert index preload
            }

            apply_monitor_.set_initial_position(WSREP_UUID_UNDEFINED, -1);
            apply_monitor_.set_initial_position(sst_uuid_, sst_seqno_);

            if (co_mode_ != CommitOrder::BYPASS)
            {
                commit_monitor_.set_initial_position(WSREP_UUID_UNDEFINED, -1);
                commit_monitor_.set_initial_position(sst_uuid_, sst_seqno_);
            }

            log_info << "Installed new state from SST: " << state_uuid_ << ":"
                      << sst_seqno_;
        }
    }
    else
    {
        assert (state_uuid_ == group_uuid);
        sst_seqno_ = last_committed();
    }

    if (st_.corrupt())
    {
        if (sst_req_len != 0 && !sst_is_trivial(sst_req, sst_req_len))
        {
            st_.mark_uncorrupt(sst_uuid_, sst_seqno_);
        }
        else
        {
            log_fatal << "Application state is corrupt and cannot "
                      << "be recorvered. Restart required.";
            abort();
        }
    }
    else
    {
        st_.mark_safe();
    }

    if (req->ist_len() > 0)
    {
        if (state_uuid_ != group_uuid)
        {
            log_fatal << "Sanity check failed: my state UUID " << state_uuid_
                      << " is different from group state UUID " << group_uuid
                      << ". Can't continue with IST. Aborting.";
            st_.set(state_uuid_, last_committed(), safe_to_bootstrap_);
            st_.mark_safe();
            abort();
        }

        // IST is prepared only with str proto ver 1 and above
        // IST is *always* prepared at str proto ver 3 or higher
        if (last_committed() < cc_seqno || str_proto_ver_ >= 3)
        {
            wsrep_seqno_t const ist_from(last_committed() + 1);
            wsrep_seqno_t const ist_to(cc_seqno);
            bool const do_ist(ist_from > 0 && ist_from <= ist_to);

            if (do_ist)
            {
                log_info << "Receiving IST: " << (ist_to - ist_from + 1)
                         << " writesets, seqnos " << ist_from
                         << "-" << ist_to;
            }
            else
            {
                log_info << "Cert. index preload up to "
                         << ist_from - 1;
            }

            ist_receiver_.ready(ist_from);
            recv_IST(recv_ctx);

            wsrep_seqno_t const ist_seqno(ist_receiver_.finished());

            if (do_ist)
            {
                assert(ist_seqno > sst_seqno_); // must exceed sst_seqno_
                sst_seqno_ = ist_seqno;

                // Note: apply_monitor_ must be drained to avoid race between
                // IST appliers and GCS appliers, GCS action source may
                // provide actions that have already been applied via IST.
                apply_monitor_.drain(ist_seqno);
            }
            else
            {
                assert(sst_seqno_ > 0); // must have been esptablished via SST
                assert(ist_seqno >= cc_seqno); // index must be rebuilt up to
                assert(ist_seqno <= sst_seqno_);
            }

            if (ist_seqno == sst_seqno_)
            {
                log_info << "IST received: " << state_uuid_ << ":" <<ist_seqno;
                if (str_proto_ver_ < 3)
                {
                    // see cert_.assign_initial_position() above
                    assert(cc_seqno == ist_seqno);
                    assert(cert_.lowest_trx_seqno() == ist_seqno);
                }
            }
            else
                log_info << "Cert. index preloaded up to " << ist_seqno;
        }
        else
        {
            (void)ist_receiver_.finished();
        }
    }
    else
    {
        // full SST can't be in the past
        assert(sst_seqno_ >= cc_seqno);
    }

#ifndef NDEBUG
    {
        gu::Lock lock(closing_mutex_);
        assert(sst_seqno_ >= cc_seqno || closing_ || state_() == S_CLOSED);
    }
#endif /* NDEBUG */

    delete req;
}

void ReplicatorSMM::process_IST_writeset(void* recv_ctx,
                                         const TrxHandleSlavePtr& ts_ptr)
{
    TrxHandleSlave& ts(*ts_ptr);

    assert(ts.global_seqno() > 0);
    assert(ts.state() != TrxHandle::S_COMMITTED);
    assert(ts.state() != TrxHandle::S_ROLLED_BACK);

    bool const skip(ts.is_dummy());

    if (gu_likely(!skip))
    {
        ts.verify_checksum();

        assert(ts.certified());
        assert(ts.depends_seqno() >= 0);
    }
    else
    {
        assert(ts.is_dummy());
    }

    gu_trace(apply_trx(recv_ctx, ts));
    GU_DBUG_SYNC_WAIT("recv_IST_after_apply_trx");

    if (gu_unlikely
        (gu::Logger::no_log(gu::LOG_DEBUG) == false))
    {
        std::ostringstream os;

        if (gu_likely(!skip))
            os << "IST received trx body: " << ts;
        else
            os << "IST skipping trx " << ts.global_seqno();

        log_debug << os.str();
    }
}


void ReplicatorSMM::recv_IST(void* recv_ctx)
{
    ISTEvent::Type event_type(ISTEvent::T_NULL);
    TrxHandleSlavePtr ts;
    wsrep_view_info_t* view;

    try
    {
        bool exit_loop(false);

        while (exit_loop == false)
        {
            ISTEvent ev(ist_event_queue_.pop_front());
            event_type = ev.type();
            switch (event_type)
            {
            case ISTEvent::T_NULL:
                exit_loop = true;
                continue;
            case ISTEvent::T_TRX:
                ts = ev.ts();
                assert(ts);
                process_IST_writeset(recv_ctx, ts);
                exit_loop = ts->exit_loop();
                continue;
            case ISTEvent::T_VIEW:
            {
                view = ev.view();
                wsrep_seqno_t const cs(view->state_id.seqno);

                submit_view_info(recv_ctx, view);

                ::free(view);

                CommitOrder co(cs, CommitOrder::NO_OOOC);
                commit_monitor_.leave(co);
                ApplyOrder ao(cs, cs - 1, false);
                apply_monitor_.leave(ao);
                GU_DBUG_SYNC_WAIT("recv_IST_after_conf_change");
                continue;
            }
            }

            gu_throw_fatal << "Unrecognized event of type " << ev.type();
        }
    }
    catch (gu::Exception& e)
    {
        std::ostringstream os;
        os << "Receiving IST failed, node restart required: " << e.what();

        switch (event_type)
        {
        case ISTEvent::T_NULL:
            os << ". Null event.";
            break;
        case ISTEvent::T_TRX:
            if (ts)
                os << ". Failed writeset: " << *ts;
            else
                os << ". Corrupt IST event queue.";
            break;
        case ISTEvent::T_VIEW:
            os << ". VIEW event";
            break;
        }

        log_fatal << os.str();

        mark_corrupt_and_close();
    }
}


void ReplicatorSMM::ist_trx(const TrxHandleSlavePtr& tsp, bool must_apply,
                            bool preload)
{
    assert(tsp != 0);
    TrxHandleSlave& ts(*tsp);

    assert(ts.depends_seqno() >= 0 || ts.state() == TrxHandle::S_ABORTING ||
           ts.nbo_end());
    assert(ts.local_seqno() == WSREP_SEQNO_UNDEFINED);

    ts.verify_checksum();
    if (gu_unlikely(cert_.position() == WSREP_SEQNO_UNDEFINED))
    {
        // This is the first pre IST event for rebuilding cert index
        cert_.assign_initial_position(
            /* proper UUID will be installed by CC */
            gu::GTID(gu::UUID(), ts.global_seqno() - 1), ts.version());
    }

    if (ts.nbo_start() || ts.nbo_end())
    {
        if (must_apply)
        {
            ts.verify_checksum();
            ts.set_state(TrxHandle::S_CERTIFYING);
            Certification::TestResult result(cert_.append_trx(tsp));
            switch (result)
            {
            case Certification::TEST_OK:
                if (ts.nbo_end())
                {
                    // This is the same as in  process_trx()
                    if (ts.ends_nbo() == WSREP_SEQNO_UNDEFINED)
                    {
                        assert(ts.is_dummy());
                    }
                    else
                    {
                        // Signal NBO waiter
                        gu::shared_ptr<NBOCtx>::type nbo_ctx(
                            cert_.nbo_ctx(ts.ends_nbo()));
                        assert(nbo_ctx != 0);
                        nbo_ctx->set_ts(tsp);
                        return; // not pushing to queue below
                    }
                }
                break;
            case Certification::TEST_FAILED:
            {
                assert(ts.nbo_end()); // non-effective nbo_end
                assert(ts.is_dummy());
                break;
            }
            }
            /* regardless of certification outcome, event must be passed to
             * apply_trx() as it carries global seqno */
        }
        else
        {
            // Skipping NBO events in preload is fine since joiner either
            // have all events applied in case of pure IST and donor refuses to
            // donate SST from the position there are NBOs going on.
            assert(preload);
            log_debug << "Skipping NBO event: " << ts;
            wsrep_seqno_t const pos(cert_.increment_position());
            assert(ts.global_seqno() == pos);
            (void)pos;
        }
#if 0
        log_info << "\n     IST processing NBO_"
                 << (ts.nbo_start() ? "START(" : "END(")
                 << ts.global_seqno() << ")"
                 << (must_apply ? ", must apply" : ", skip")
                 << ", ends NBO: " << ts.ends_nbo();
#endif
    }
    else
    {
        if (gu_unlikely(preload == true))
        {
            if (gu_likely(!ts.is_dummy()))
            {
                ts.set_state(TrxHandle::S_CERTIFYING);
                Certification::TestResult result(cert_.append_trx(tsp));
                if (result != Certification::TEST_OK)
                {
                    gu_throw_fatal << "Pre IST trx append returned unexpected "
                                   << "certification result " << result
                                   << ", expected " << Certification::TEST_OK
                                   << "must abort to maintain consistency";
                }
                // Mark trx committed for certification bookkeeping here
                // if it won't pass to applying stage
                if (!must_apply) cert_.set_trx_committed(ts);
            }
            else
            {
                wsrep_seqno_t const pos(cert_.increment_position());
                assert(ts.global_seqno() == pos);
                (void)pos;
            }
        }
        else if (ts.state() == TrxHandle::S_REPLICATING)
        {
            ts.set_state(TrxHandle::S_CERTIFYING);
        }
    }

    if (gu_likely(must_apply == true))
    {
        ist_event_queue_.push_back(tsp);
    }
}

void ReplicatorSMM::ist_end(int error)
{
    ist_event_queue_.eof(error);
}

void ReplicatorSMM::ist_cc(const gcs_action& act, bool must_apply,
                           bool preload)
{
    assert(GCS_ACT_CCHANGE == act.type);
    assert(act.seqno_g > 0);

    gcs_act_cchange const conf(act.buf, act.size);

    assert(conf.conf_id >= 0); // Primary configuration
    assert(conf.seqno == act.seqno_g);

    wsrep_uuid_t uuid_undefined(WSREP_UUID_UNDEFINED);
    wsrep_view_info_t* const view_info(
        galera_view_info_create(conf, capabilities(conf.repl_proto_ver),
                                -1, uuid_undefined));

    if (gu_unlikely(cert_.position() == WSREP_SEQNO_UNDEFINED) &&
        (must_apply || preload))
    {
        // This is the first IST event for rebuilding cert index,
        // need to initialize certification
        establish_protocol_versions(conf.repl_proto_ver);
        cert_.assign_initial_position(gu::GTID(conf.uuid, conf.seqno - 1),
                                      trx_params_.version_);
    }

    if (must_apply == true)
    {
        process_conf_change(0, act);
        /* TO monitors need to be entered here to maintain critical
         * section over passing the view through the event queue to
         * an applier and ensure that the view is submitted in isolation.
         * Applier is to leave monitors and free the view after it is
         * submitted */
        ApplyOrder ao(conf.seqno, conf.seqno - 1, false);
        apply_monitor_.enter(ao);
        CommitOrder co(conf.seqno, CommitOrder::NO_OOOC);
        commit_monitor_.enter(co);
        ist_event_queue_.push_back(view_info);
    }
    else
    {
        if (preload == true)
        {
            /* CC is part of index preload but won't be processed
             * by process_conf_change()
             * Order of these calls is essential: trx_params_.version_ may
             * be altered by establish_protocol_versions() */
            establish_protocol_versions(conf.repl_proto_ver);
            cert_.adjust_position(*view_info, gu::GTID(conf.uuid, conf.seqno),
                                  trx_params_.version_);
            // record CC releated state seqnos, needed for IST on DONOR
            record_cc_seqnos(conf.seqno, "preload");
        }

        ::free(view_info);
    }
}

} /* namespace galera */
