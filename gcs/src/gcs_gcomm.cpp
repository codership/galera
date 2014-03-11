/*
 * Copyright (C) 2009-2014 Codership Oy <info@codership.com>
 */

/*!
 * @file GComm GCS Backend implementation
 *
 * @todo Figure out if there is lock-free way to handle RecvBuf
 *       push/pop operations.
 *
 */


extern "C"
{
#include "gcs_gcomm.h"
}

// We access data comp msg struct directly
extern "C"
{
#define GCS_COMP_MSG_ACCESS 1
#include "gcs_comp_msg.h"
}


#include <galerautils.hpp>
#include "gu_backtrace.hpp"
#include "gcomm/transport.hpp"
#include "gcomm/util.hpp"
#include "gcomm/conf.hpp"

#ifdef PROFILE_GCS_GCOMM
#define GCOMM_PROFILE 1
#else
#undef GCOMM_PROFILE
#endif // PROFILE_GCS_GCOMM
#include "profile.hpp"

#include <deque>

using namespace std;
using namespace gu;
using namespace gu::prodcons;
using namespace gu::datetime;
using namespace gcomm;
using namespace prof;

class RecvBufData
{
public:
    RecvBufData(const size_t source_idx,
                const Datagram& dgram,
                const ProtoUpMeta& um) :
        source_idx_(source_idx),
        dgram_     (dgram),
        um_        (um)
    { }

    size_t get_source_idx() const { return source_idx_; }
    const Datagram& get_dgram() const { return dgram_; }
    const ProtoUpMeta& get_um() const { return um_; }

private:
    size_t source_idx_;
    Datagram dgram_;
    ProtoUpMeta um_;
};

#if defined(GALERA_USE_BOOST_POOL_ALLOC)

#include <boost/pool/pool_alloc.hpp>

typedef deque<RecvBufData,
              boost::fast_pool_allocator<
                  RecvBufData,
                  boost::default_user_allocator_new_delete,
                  boost::details::pool::null_mutex
                  >
              >
#else

typedef deque<RecvBufData>

#endif /* GALERA_USE_BOOST_POOL_ALLOC */

RecvBufQueue;

class RecvBuf
{
private:

    class Waiting
    {
    public:
        Waiting (bool& w) : w_(w) { w_ = true;  }
        ~Waiting()                { w_ = false; }
    private:
        bool& w_;
    };

public:

    RecvBuf() : mutex_(), cond_(), queue_(), waiting_(false) { }

    void push_back(const RecvBufData& p)
    {
        Lock lock(mutex_);

        queue_.push_back(p);

        if (waiting_ == true) { cond_.signal(); }
    }

    const RecvBufData& front(const Date& timeout)
    {
        Lock lock(mutex_);

        while (queue_.empty())
        {
            Waiting w(waiting_);
            if (gu_likely (timeout == GU_TIME_ETERNITY))
            {
                lock.wait(cond_);
            }
            else
            {
                lock.wait(cond_, timeout);
            }
        }
        assert (false == waiting_);

        return queue_.front();
    }

    void pop_front()
    {
        Lock lock(mutex_);
        assert(queue_.empty() == false);
        queue_.pop_front();
    }

private:

    Mutex mutex_;
    Cond cond_;
    RecvBufQueue queue_;
    bool waiting_;
};


class MsgData : public MessageData
{
public:
    MsgData(const byte_t* data,
            const size_t data_size,
            const gcs_msg_type_t msg_type) :
        data_     (data),
        data_size_(data_size),
        msg_type_ (msg_type)
    { }
    const byte_t* get_data() const { return data_; }
    size_t get_data_size() const { return data_size_; }
    gcs_msg_type_t get_msg_type() const { return msg_type_; }

public:
    MsgData(const MsgData&);
    void operator=(const MsgData&);
    const byte_t*  data_;
    size_t         data_size_;
    gcs_msg_type_t msg_type_;
};


class GCommConn : public Consumer, public Toplay
{
public:

    GCommConn(const URI& u, gu::Config& cnf) :
        Toplay(cnf),
        conf_(cnf),
        uuid_(),
        thd_(),
        uri_(u),
        net_(Protonet::create(conf_)),
        tp_(0),
        mutex_(),
        refcnt_(0),
        terminated_(false),
        error_(0),
        recv_buf_(),
        current_view_(),
        prof_("gcs_gcomm")
    {
        log_info << "backend: " << net_->type();
    }

    ~GCommConn()
    {
        delete net_;
    }

    const UUID& get_uuid() const { return uuid_; }

    static void* run_fn(void* arg)
    {
        static_cast<GCommConn*>(arg)->run();
        return 0;
    }

    void connect(bool) { }

    void connect(const string& channel, bool const bootstrap)
    {
        if (tp_ != 0)
        {
            gu_throw_fatal << "backend connection already open";
        }

        uri_.set_option("gmcast.group", channel);
        tp_ = Transport::create(*net_, uri_);
        gcomm::connect(tp_, this);

        if (bootstrap)
        {
            log_info << "gcomm: bootstrapping new group '" << channel << '\'';
        }
        else
        {
            string peer;
            URI::AuthorityList::const_iterator i, i_next;
            for (i = uri_.get_authority_list().begin();
                 i != uri_.get_authority_list().end(); ++i)
            {
                i_next = i;
                ++i_next;
                string host;
                string port;
                try { host = i->host(); } catch (NotSet&) { }
                try { port = i->port(); } catch (NotSet&) { }
                peer += host != "" ? host + ":" + port : "";
                if (i_next != uri_.get_authority_list().end())
                {
                    peer += ",";
                }
            }
            log_info << "gcomm: connecting to group '" << channel
                     << "', peer '" << peer << "'";
        }

        tp_->connect(bootstrap);

        uuid_ = tp_->uuid();

        int err;

        if ((err = pthread_create(&thd_, 0, &run_fn, this)) != 0)
        {
            gu_throw_error(err);
        }
        log_info << "gcomm: connected";
    }

    void close(bool force = false)
    {
        if (tp_ == 0)
        {
            log_warn << "gcomm: backend already closed";
            return;
        }
        log_info << "gcomm: terminating thread";
        terminate();
        log_info << "gcomm: joining thread";
        pthread_join(thd_, 0);
        log_info << "gcomm: closing backend";
        tp_->close(error_ != 0);
        gcomm::disconnect(tp_, this);
        delete tp_;
        tp_ = 0;

        const Message* msg;

        while ((msg = get_next_msg()) != 0)
        {
            return_ack(Message(&msg->get_producer(), 0, -ECONNABORTED));
        }
        log_info << "gcomm: closed";
        log_debug << prof_;
    }

    void run();

    void notify() { net_->interrupt(); }

    void terminate()
    {
        Lock lock(mutex_);
        terminated_ = true;
        net_->interrupt();
    }

    void handle_up     (const void*        id,
                        const Datagram&    dg,
                        const ProtoUpMeta& um);

    void queue_and_wait(const Message& msg, Message* ack);

    RecvBuf&    get_recv_buf()            { return recv_buf_; }
    size_t      get_mtu()           const
    {
        if (tp_ == 0)
        {
            gu_throw_fatal << "GCommConn::get_mtu(): "
                           << "backend connection not open";
        }
        return tp_->mtu();
    }

    Protonet&   get_pnet()                { return *net_; }
    gu::Config& get_conf()                { return conf_; }
    int         get_error() const         { return error_; }

    class Ref
    {
    public:

        Ref(gcs_backend_t* ptr, bool unset = false) : conn_(0)
        {
            if (ptr->conn != 0)
            {
                conn_ = reinterpret_cast<GCommConn*>(ptr->conn)->ref(unset);

                if (unset == true)
                {
                    ptr->conn = 0;
                }
            }
        }

        ~Ref()
        {
            if (conn_ != 0)
            {
                conn_->unref();
            }
        }

        GCommConn* get() { return conn_; }

    private:

        Ref(const Ref&);
        void operator=(const Ref&);

        GCommConn* conn_;
    };

private:

    GCommConn(const GCommConn&);
    void operator=(const GCommConn&);

    GCommConn* ref(const bool unsetting)
    {
        return this;
    }

    void unref() { }

    gu::Config& conf_;
    UUID        uuid_;
    pthread_t   thd_;
    URI         uri_;
    Protonet*   net_;
    Transport*  tp_;
    Mutex       mutex_;
    size_t      refcnt_;
    bool        terminated_;
    int         error_;
    RecvBuf     recv_buf_;
    View        current_view_;
    Profile     prof_;
};


void
GCommConn::handle_up(const void* id, const Datagram& dg, const ProtoUpMeta& um)
{
    if (um.err_no() != 0)
    {
        error_ = um.err_no();
        recv_buf_.push_back(RecvBufData(numeric_limits<size_t>::max(), dg, um));
    }
    else if (um.has_view() == true)
    {
        current_view_ = um.view();
        recv_buf_.push_back(RecvBufData(numeric_limits<size_t>::max(), dg, um));
        if (current_view_.is_empty())
        {
            log_debug << "handle_up: self leave";
        }
    }
    else
    {
        size_t idx(0);
        for (NodeList::const_iterator i = current_view_.members().begin();
             i != current_view_.members().end(); ++i)
        {
            if (NodeList::key(i) == um.source())
            {
                profile_enter(prof_);
                recv_buf_.push_back(RecvBufData(idx, dg, um));
                profile_leave(prof_);
                break;
            }
            ++idx;
        }
        assert(idx < current_view_.members().size());
    }
}


void GCommConn::queue_and_wait(const Message& msg, Message* ack)
{
    {
        Lock lock(mutex_);
        if (terminated_ == true)
        {
            *ack = Message(&msg.get_producer(), 0, -ECONNABORTED);
            return;
        }
    }
    profile_enter(prof_);
    Consumer::queue_and_wait(msg, ack);
    profile_leave(prof_);
}


void GCommConn::run()
{
    while (true)
    {
        {
            Lock lock(mutex_);

            if (terminated_ == true)
            {
                break;
            }
        }

        try
        {
            net_->event_loop(Sec);
        }
        catch (gu::Exception& e)
        {
            log_error << "exception from gcomm, backend must be restarted: "
                      << e.what();
            // Commented out due to Backtrace() not producing proper
            // backtraces.
            // log_info << "attempting to get backtrace:";
            // Backtrace().print(std::cerr);
            gcomm::Critical<Protonet> crit(get_pnet());
            handle_up(0, Datagram(),
                      ProtoUpMeta(UUID::nil(),
                                  ViewId(V_NON_PRIM),
                                  0,
                                  0xff,
                                  O_DROP,
                                  -1,
                                  e.get_errno()));
            break;
        }
#if 0
        // Disabled catching unknown exceptions due to Backtrace() not
        // producing proper backtraces. We let the application crash
        // and deal with diagnostics.
        catch (...)
        {
            log_error
                << "unknow exception from gcomm, backend must be restarted";
            log_info << "attempting to get backtrace:";
            Backtrace().print(std::cerr);

            gcomm::Critical<Protonet> crit(get_pnet());
            handle_up(0, Datagram(),
                      ProtoUpMeta(UUID::nil(),
                                  ViewId(V_NON_PRIM),
                                  0,
                                  0xff,
                                  O_DROP,
                                  -1,
                                  gu::Exception::E_UNSPEC));
            break;
        }
#endif
    }
}


////////////////////////////////////////////////////////////////////////////
//
//                  Backend interface implementation
//
////////////////////////////////////////////////////////////////////////////


static GCS_BACKEND_MSG_SIZE_FN(gcomm_msg_size)
{
    GCommConn::Ref ref(backend);
    if (ref.get() == 0)
    {
        return -1;
    }
    return ref.get()->get_mtu();
}


static GCS_BACKEND_SEND_FN(gcomm_send)
{
    GCommConn::Ref ref(backend);

    if (gu_unlikely(ref.get() == 0))
    {
        return -EBADFD;
    }

    GCommConn& conn(*ref.get());

    Datagram dg(
        SharedBuffer(
            new Buffer(reinterpret_cast<const byte_t*>(buf),
                       reinterpret_cast<const byte_t*>(buf) + len)));
    gcomm::Critical<Protonet> crit(conn.get_pnet());
    if (gu_unlikely(conn.get_error() != 0))
    {
        return -ECONNABORTED;
    }
    int err = conn.send_down(
        dg,
        ProtoDownMeta(msg_type, msg_type == GCS_MSG_CAUSAL ?
                      O_LOCAL_CAUSAL : O_SAFE));
    return (err == 0 ? len : -err);
}


static void fill_cmp_msg(const View& view, const UUID& my_uuid,
                         gcs_comp_msg_t* cm)
{
    size_t n(0);

    for (NodeList::const_iterator i = view.members().begin();
         i != view.members().end(); ++i)
    {
        const UUID& uuid(NodeList::key(i));

        log_debug << "member: " << n << " uuid: " << uuid;

//        (void)snprintf(cm->memb[n].id, GCS_COMP_MEMB_ID_MAX_LEN, "%s",
//                       uuid._str().c_str());
        long ret = gcs_comp_msg_add (cm, uuid._str().c_str(),
                                     i->second.segment());
        if (ret < 0) {
            gu_throw_error(-ret) << "Failed to add member '" << uuid
                                 << "' to component message.";
        }

        if (uuid == my_uuid)
        {
            log_debug << "my index " << n;
            cm->my_idx = n;
        }

        ++n;
    }
}

static GCS_BACKEND_RECV_FN(gcomm_recv)
{
    GCommConn::Ref ref(backend);

    if (gu_unlikely(ref.get() == 0)) return -EBADFD;

    try
    {
        GCommConn& conn(*ref.get());

        RecvBuf& recv_buf(conn.get_recv_buf());

        const RecvBufData& d(recv_buf.front(timeout));

        msg->sender_idx = d.get_source_idx();

        const Datagram&    dg(d.get_dgram());
        const ProtoUpMeta& um(d.get_um());

        if (gu_likely(dg.len() != 0))
        {
            assert(dg.len() > dg.offset());

            const byte_t* b(gcomm::begin(dg));
            const ssize_t pload_len(gcomm::available(dg));

            msg->size = pload_len;

            if (gu_likely(pload_len <= msg->buf_len))
            {
                memcpy(msg->buf, b, pload_len);
                msg->type = static_cast<gcs_msg_type_t>(um.user_type());
                recv_buf.pop_front();
            }
            else
            {
                msg->type = GCS_MSG_ERROR;
            }
        }
        else if (um.err_no() != 0)
        {
            gcs_comp_msg_t* cm(gcs_comp_msg_leave());
            const ssize_t cm_size(gcs_comp_msg_size(cm));
            msg->size = cm_size;
            if (gu_likely(cm_size <= msg->buf_len))
            {
                memcpy(msg->buf, cm, cm_size);
                recv_buf.pop_front();
                msg->type = GCS_MSG_COMPONENT;
            }
            else
            {
                msg->type = GCS_MSG_ERROR;
            }
            gcs_comp_msg_delete(cm);
        }
        else
        {
            assert(um.has_view() == true);

            const View& view(um.view());

            assert(view.type() == V_PRIM || view.type() == V_NON_PRIM);

            gcs_comp_msg_t* cm(gcs_comp_msg_new(view.type() == V_PRIM,
                                                view.is_bootstrap(),
                                                view.is_empty() ? -1 : 0,
                                                view.members().size()));

            const ssize_t cm_size(gcs_comp_msg_size(cm));

            if (cm->my_idx == -1)
            {
                log_debug << "gcomm recv: self leave";
            }

            msg->size = cm_size;

            if (gu_likely(cm_size <= msg->buf_len))
            {
                fill_cmp_msg(view, conn.get_uuid(), cm);
                memcpy(msg->buf, cm, cm_size);
                recv_buf.pop_front();
                msg->type = GCS_MSG_COMPONENT;
            }
            else
            {
                msg->type = GCS_MSG_ERROR;
            }

            gcs_comp_msg_delete(cm);
        }

        return msg->size;
    }
    catch (Exception& e)
    {
        long err = e.get_errno();

        if (ETIMEDOUT != err) { log_error << e.what(); }

        return -err;
    }
}


static GCS_BACKEND_NAME_FN(gcomm_name)
{
    static const char *name = "gcomm";
    return name;
}


static GCS_BACKEND_OPEN_FN(gcomm_open)
{
    GCommConn::Ref ref(backend);

    if (ref.get() == 0)
    {
        return -EBADFD;
    }

    GCommConn& conn(*ref.get());

    try
    {
        conn.connect(channel, bootstrap);
    }
    catch (Exception& e)
    {
        log_error << "failed to open gcomm backend connection: "
                  << e.get_errno() << ": "
                  << e.what();
        return -e.get_errno();
    }
    return 0;
}


static GCS_BACKEND_CLOSE_FN(gcomm_close)
{
    GCommConn::Ref ref(backend);

    if (ref.get() == 0)
    {
        return -EBADFD;
    }

    GCommConn& conn(*ref.get());
    try
    {
        conn.close();
    }
    catch (Exception& e)
    {
        log_error << "failed to close gcomm backend connection: "
                  << e.get_errno() << ": " << e.what();
        gcomm::Critical<Protonet> crit(conn.get_pnet());
        conn.handle_up(0, Datagram(),
                       ProtoUpMeta(UUID::nil(),
                                   ViewId(V_NON_PRIM),
                                   0,
                                   0xff,
                                   O_DROP,
                                   -1,
                                   e.get_errno()));
        // #661: Pretend that closing was successful, backend should be
        // in unusable state anyway. This allows gcs to finish shutdown
        // sequence properly.
    }

    return 0;
}


static GCS_BACKEND_DESTROY_FN(gcomm_destroy)
{
    GCommConn::Ref ref(backend, true);

    if (ref.get() == 0)
    {
        log_warn << "could not get reference to backend conn";
        return -EBADFD;
    }

    GCommConn* conn(ref.get());
    try
    {
        delete conn;
    }
    catch (Exception& e)
    {
        log_warn << "conn destroy failed: " << e.get_errno();
        return -e.get_errno();
    }

    return 0;
}


static
GCS_BACKEND_PARAM_SET_FN(gcomm_param_set)
{
    GCommConn::Ref ref(backend);
    if (ref.get() == 0)
    {
        return -EBADFD;
    }

    GCommConn& conn(*ref.get());
    try
    {
        gcomm::Critical<Protonet> crit(conn.get_pnet());
        if (gu_unlikely(conn.get_error() != 0))
        {
            return -ECONNABORTED;
        }

        if (conn.get_pnet().set_param(key, value) == false)
        {
            log_debug << "param " << key << " not recognized";
            return 1;
        }
        else
        {
            return 0;
        }
    }
    catch (gu::Exception& e)
    {
        log_warn << "error setting param " << key << " to value " << value
                 << ": " << e.what();
        return -e.get_errno();
    }
    catch (gu::NotFound& nf)
    {
        log_warn << "error setting param " << key << " to value " << value;
        return -EINVAL;
    }
    catch (gu::NotSet& nf)
    {
        log_warn << "error setting param " << key << " to value " << value;
        return -EINVAL;
    }
    catch (...)
    {
        log_fatal << "gcomm param set: caught unknown exception";
        return -ENOTRECOVERABLE;
    }
}


static
GCS_BACKEND_PARAM_GET_FN(gcomm_param_get)
{
    return NULL;
}


GCS_BACKEND_REGISTER_FN(gcs_gcomm_register)
{
    gcomm::Conf::register_params(*reinterpret_cast<gu::Config*>(cnf));
}


GCS_BACKEND_CREATE_FN(gcs_gcomm_create)
{
    GCommConn* conn(0);

    if (!cnf)
    {
        log_error << "Null config object passed to constructor.";
        return -EINVAL;
    }

    try
    {
        gu::URI uri(std::string("pc://") + addr);
        gu::Config& conf(*reinterpret_cast<gu::Config*>(cnf));
        conn = new GCommConn(uri, conf);
    }
    catch (Exception& e)
    {
        log_error << "failed to create gcomm backend connection: "
                  << e.get_errno() << ": "
                  << e.what();
        return -e.get_errno();
    }

    backend->open      = gcomm_open;
    backend->close     = gcomm_close;
    backend->destroy   = gcomm_destroy;
    backend->send      = gcomm_send;
    backend->recv      = gcomm_recv;
    backend->name      = gcomm_name;
    backend->msg_size  = gcomm_msg_size;
    backend->param_set = gcomm_param_set;
    backend->param_get = gcomm_param_get;
    backend->conn      = reinterpret_cast<gcs_backend_conn_t*>(conn);

    return 0;
}
