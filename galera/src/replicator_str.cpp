//
// Copyright (C) 2010-2015 Codership Oy <info@codership.com>
//

#include "replicator_smm.hpp"

#include <gu_abort.h>

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
            wsrep_seqno_t const local_seqno(STATE_SEQNO());

            if (state_() >= S_JOINING) /* See #442 - S_JOINING should be
                                          a valid state here */
            {
                if (protocol_version_ >= 8)
                    return (local_seqno + 1 < group_seqno); // this CC will add 1
                else
                    return (local_seqno < group_seqno);
            }
            else
            {
                if ((protocol_version_ >= 8 && local_seqno >= group_seqno) ||
                    (protocol_version_ <  8 && local_seqno >  group_seqno))
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

    StateRequest* const streq(read_state_request(req, req_size));

    LocalOrder lo(seqno_l);

    gu_trace(local_monitor_.enter(lo));
    apply_monitor_.drain(donor_seq);

    if (co_mode_ != CommitOrder::BYPASS) commit_monitor_.drain(donor_seq);

    state_.shift_to(S_DONOR);

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
                        ((protocol_version_ < 8 || cc_lowest_trx_seqno_ == 0) ?
                         istr.last_applied() + 1 :
                         std::min(cc_lowest_trx_seqno_, istr.last_applied()+1));
                    try
                    {
                        ist_senders_.run(config_,
                                         istr.peer(),
                                         first,
                                         cc_seqno_,
                                         cc_lowest_trx_seqno_,
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

        if (streq->sst_len())
        {
            assert(0 == rcode);

            wsrep_gtid_t const state_id = { state_uuid_, donor_seq };

            if (protocol_version_ >= 8)
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
                                 << preload_start << " not found from gcache";
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
    // Up from protocol version 8 joiner is assumed to be able receive
    // some transactions to rebuild cert index, so IST receiver must be
    // prepared regardless of the group.
    wsrep_seqno_t last_applied(STATE_SEQNO());

    if (state_uuid_ != group_uuid)
    {
        if (protocol_version_ < 8)
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

    if (last_applied < 0 && protocol_version_ < 8)
    {
        gu_throw_error (EPERM) << "Local state seqno is undefined";
    }

    wsrep_seqno_t const first_needed(last_applied + 1);

    log_info << "####### IST uuid:" << state_uuid_ << " f: " << first_needed
             << ", l: " << last_needed << ", p: " << protocol_version_; //remove

    std::string recv_addr (ist_receiver_.prepare(first_needed, last_needed,
                                                 protocol_version_));

    std::ostringstream os;

    /* NOTE: in case last_applied is -1, first_needed is 0, but first legal
     * cached seqno is 1 so donor will revert to SST anyways, as is required */
    os << IST_request(recv_addr, state_uuid_, last_applied, last_needed);

    char* str = strdup (os.str().c_str());

    log_debug << "Prepared IST request: " << str;

    // cppcheck-suppress nullPointer
    if (!str) gu_throw_error (ENOMEM) << "Failed to allocate IST buffer.";

    len = strlen(str) + 1;

    ptr = str;
}


ReplicatorSMM::StateRequest*
ReplicatorSMM::prepare_state_request (const void* const   sst_req,
                                      ssize_t     const   sst_req_len,
                                      const wsrep_uuid_t& group_uuid,
                                      wsrep_seqno_t const last_needed_seqno)
{
    try
    {
        switch (str_proto_ver_)
        {
        case 0:
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
        throw;
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
                long const seconds = sst_retry_sec_ * tries;
                log_error << "We ran out of resources, seemingly because "
                          << "we've been unsuccessfully requesting state "
                          << "transfer for over " << seconds << " seconds. "
                          << "Please check that there is "
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

        st_.set(state_uuid_, STATE_SEQNO());
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
        gcache_.seqno_reset();
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

            st_.set(sst_uuid_, sst_seqno_);
            st_.mark_safe();

            abort();
        }
        else
        {
            assert(sst_seqno_ > 0);

            /* There are two places where we may need to adjust GCache.
             * This is the second one.
             * Here we reset seqno map completely if we have gap in seqnos
             * between the received snapshot and current GCache contents.
             * This MUST be done before IST starts. */
            // there may be possible optimization to this when cert index
            // transfer is implemented (it may close the gap), but not by much.
            if (!first_reset && (STATE_SEQNO() /* GCache has */ !=
                                 sst_seqno_    /* current state has */))
            {
                log_info << "Resetting GCache seqno map due to seqno gap: "
                         << STATE_SEQNO() << ".." << sst_seqno_;
                gcache_.seqno_reset();
            }

            update_state_uuid (sst_uuid_);

            if (str_proto_ver_ < 3)
            {
                cert_.assign_initial_position(gu::GTID(sst_uuid_, sst_seqno_),
                                              trx_params_.version_);
                // with higher versions this happens in cert index preload
            }

            apply_monitor_.set_initial_position(-1);
            apply_monitor_.set_initial_position(sst_seqno_);

            if (co_mode_ != CommitOrder::BYPASS)
            {
                commit_monitor_.set_initial_position(-1);
                commit_monitor_.set_initial_position(sst_seqno_);
            }

            log_info << "Installed new state from SST: " << state_uuid_ << ":"
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
        if (state_uuid_ != group_uuid)
        {
            log_fatal << "Sanity check failed: my state UUID " << state_uuid_
                      << " is different from group state UUID " << group_uuid
                      << ". Can't continue with IST. Aborting.";
            st_.set(state_uuid_, STATE_SEQNO());
            st_.mark_safe();
            abort();
        }

        // IST is prepared only with str proto ver 1 and above
        // IST is *always* prepared at protocol version 8 or higher
        if (STATE_SEQNO() < cc_seqno || protocol_version_ >= 8)
        {
            wsrep_seqno_t const ist_from(STATE_SEQNO() + 1);
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
                apply_monitor_.drain(sst_seqno_);
            }
            else
            {
                assert(sst_seqno_ > 0); // must have been esptablished via SST
                assert(ist_seqno >= cc_seqno); // index must be rebuilt up to
                assert(ist_seqno <= sst_seqno_);
            }

            if (ist_seqno == sst_seqno_)
                log_info << "IST received: " << state_uuid_ << ":" << ist_seqno;
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

    assert(sst_seqno_ >= cc_seqno);

    delete req;
}

bool ReplicatorSMM::process_IST_writeset(void* recv_ctx, const gcs_action& act)
{
    assert(GCS_ACT_WRITESET == act.type);

    TrxHandleSlavePtr ts(TrxHandleSlave::New(false, slave_pool_),
                         TrxHandleSlaveDeleter());
    bool exit_loop(false);

    bool const skip(0 == act.size);

    if (gu_likely(!skip))
    {
        assert(act.buf != NULL);

        gu_trace(ts->unserialize<false>(act));

        ts->verify_checksum();

        assert(ts->certified());

        // replicating and certifying stages have been
        // processed on donor, just adjust states here
        ts->set_state(TrxHandle::S_CERTIFYING);

        assert(ts->global_seqno() == act.seqno_g);
        assert(ts->depends_seqno() >= 0);

        gu_trace(apply_trx(recv_ctx, *ts));
        GU_DBUG_SYNC_WAIT("recv_IST_after_apply_trx");

        exit_loop = ts->exit_loop();
    }
    else
    {
        ts->set_global_seqno(act.seqno_g);

        ApplyOrder ao(*ts);
        apply_monitor_.self_cancel(ao);

        if (gu_likely(co_mode_ != CommitOrder::BYPASS))
        {
            CommitOrder co(*ts, co_mode_);
            commit_monitor_.self_cancel(co);
        }
    }

    if (gu_unlikely
        (gu::Logger::no_log(gu::LOG_DEBUG) == false))
    {
        std::ostringstream os;

        if (gu_likely(!skip))
            os << "IST received trx body: " << *ts;
        else
            os << "IST skipping trx " << act.seqno_g;

        log_debug << os;
    }

    return exit_loop;
}


void ReplicatorSMM::recv_IST(void* recv_ctx)
{
    gcs_action act;

    try
    {
        bool exit_loop(false);

        while (!exit_loop)
        {
            int err;

            if (gu_likely((err = ist_receiver_.recv(act)) == 0))
            {
                if (gu_likely(GCS_ACT_WRITESET == act.type))
                {
                    exit_loop = process_IST_writeset(recv_ctx, act);
                }
                else
                {
                    assert(GCS_ACT_CCHANGE == act.type);
                    process_conf_change(recv_ctx, act);
                    GU_DBUG_SYNC_WAIT("recv_IST_after_conf_change");
                }
            }
            else
            {
                assert(EINTR == err);
                return;
            }
        }
    }
    catch (gu::Exception& e)
    {
        log_fatal << "receiving IST failed, node restart required: "
                  << e.what();
        log_fatal << "failed action: " << act;
        st_.mark_corrupt();
        gcs_.close();
        gu_abort();
    }
}


void ReplicatorSMM::preload_index_trx(const gcs_action& act)
{
    assert(GCS_ACT_WRITESET == act.type);


    TrxHandleSlavePtr ts(TrxHandleSlave::New(false, slave_pool_),
                         TrxHandleSlaveDeleter());

    if (gu_likely(0 != act.size))
    {
        assert(act.buf != NULL);

        gu_trace(ts->unserialize<false>(act));

        assert(ts->global_seqno() == act.seqno_g);
        assert(ts->depends_seqno() >= 0);

        ts->verify_checksum();

        if (gu_unlikely(cert_.position() == 0))
        {
            // This is the first pre IST trx for rebuilding cert index
            cert_.assign_initial_position(
                /* proper UUID will be installed by CC */
                gu::GTID(gu::UUID(), ts->global_seqno() - 1),
                         ts->version());
        }

        Certification::TestResult result(cert_.append_trx(ts));

        if (result != Certification::TEST_OK)
        {
            gu_throw_fatal << "Pre IST trx append returned unexpected "
                           << "certification result " << result
                           << ", expected " << Certification::TEST_OK
                           << "must abort to maintain consistency";
        }
        cert_.set_trx_committed(*ts);
    }
}


void ReplicatorSMM::preload_index_cc(const gcs_action& act)
{
    assert(GCS_ACT_CCHANGE == act.type);
    assert(act.seqno_g > 0);

    gcs_act_cchange const conf(act.buf, act.size);

    establish_protocol_versions(conf.repl_proto_ver);
    cert_.adjust_position(gu::GTID(conf.uuid, act.seqno_g),trx_params_.version_);
}


void ReplicatorSMM::preload_index(const gcs_action& act)
{
    switch(act.type)
    {
    case GCS_ACT_WRITESET: preload_index_trx(act); break;
    case GCS_ACT_CCHANGE:  preload_index_cc(act);  break;
    default: assert(0);
    };
}


void ReplicatorSMM::wait(wsrep_seqno_t const upto)
{
    apply_monitor_.wait(upto);
    if (co_mode_ != CommitOrder::BYPASS) commit_monitor_.wait(upto);
}

void ReplicatorSMM::drain_monitors(wsrep_seqno_t const upto)
{
    apply_monitor_.drain(upto);
    if (co_mode_ != CommitOrder::BYPASS) commit_monitor_.drain(upto);
}

} /* namespace galera */
