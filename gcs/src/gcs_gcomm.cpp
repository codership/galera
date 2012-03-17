/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
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
#include "gcomm/transport.hpp"
#include "gcomm/util.hpp"

#ifdef PROFILE_GCS_GCOMM
#define GCOMM_PROFILE 1
#else
#undef GCOMM_PROFILE
#endif // PROFILE_GCS_GCOMM
#include "profile.hpp"

#include <boost/pool/pool_alloc.hpp>
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
    RecvBufData(const size_t source_idx_,
                const Datagram& dgram_,
                const ProtoUpMeta& um_) :
        source_idx(source_idx_),
        dgram(dgram_),
        um(um_)
    { }

    size_t get_source_idx() const { return source_idx; }
    const Datagram& get_dgram() const { return dgram; }
    const ProtoUpMeta& get_um() const { return um; }

private:
    size_t source_idx;
    Datagram dgram;
    ProtoUpMeta um;
};


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

    RecvBuf() : mutex(), cond(), queue(), waiting(false) { }

    void push_back(const RecvBufData& p)
    {
        Lock lock(mutex);

        queue.push_back(p);

        if (waiting == true) { cond.signal(); }
    }

    const RecvBufData& front(const Date& timeout) throw (Exception)
    {
        Lock lock(mutex);

        while (queue.empty())
        {
            Waiting w(waiting);
            if (gu_likely (timeout == GU_TIME_ETERNITY))
            {
                lock.wait(cond);
            }
            else
            {
                lock.wait(cond, timeout);
            }
        }
        assert (false == waiting);

        return queue.front();
    }

    void pop_front()
    {
        Lock lock(mutex);
        assert(queue.empty() == false);
        queue.pop_front();
    }

private:

    class DummyMutex
    {
    public:
        void lock()   {}
        void unlock() {}
    };

    Mutex mutex;
    Cond cond;
    deque<RecvBufData,
          boost::fast_pool_allocator<
              RecvBufData,
              boost::default_user_allocator_new_delete, DummyMutex> > queue;
    bool waiting;
};


class MsgData : public MessageData
{
public:
    MsgData(const byte_t* data_,
            const size_t data_size_,
            const gcs_msg_type_t msg_type_) :
        data(data_),
        data_size(data_size_),
        msg_type(msg_type_)
    { }
    const byte_t* get_data() const { return data; }
    size_t get_data_size() const { return data_size; }
    gcs_msg_type_t get_msg_type() const { return msg_type; }

public:
    MsgData(const MsgData&);
    void operator=(const MsgData&);
    const byte_t* data;
    size_t  data_size;
    gcs_msg_type_t msg_type;
};


class GCommConn : public Consumer, public Toplay
{
public:

    GCommConn(const URI& u, gu::Config& cnf) :
        Toplay(cnf),
        conf(cnf),
        uuid(),
        thd(),
        uri(u),
        use_prod_cons(from_string<bool>(
                          uri.get_option("gcomm.use_prod_cons", "false"))),
        net(Protonet::create(conf)),
        tp(0),
        mutex(),
        refcnt(0),
        terminated(false),
        error(0),
        recv_buf(),
        current_view(),
        prof("gcs_gcomm")
    {
        if (use_prod_cons == false)
        {
            log_debug << "gcomm: disabling prod/cons";
        }
        log_info << "backend: " << net->get_type();
    }

    ~GCommConn()
    {
        delete net;
    }

    const UUID& get_uuid() const { return uuid; }

    static void* run_fn(void* arg)
    {
        static_cast<GCommConn*>(arg)->run();
        return 0;
    }

    void connect(const string& channel)
    {
        if (tp != 0)
        {
            gu_throw_fatal << "backend connection already open";
        }

        uri.set_option("gmcast.group", channel);
        tp = Transport::create(*net, uri);
        gcomm::connect(tp, this);

        string host("");
        string port("");
        try { host = uri.get_host(); } catch (NotSet&) { }
        try { port = uri.get_port(); } catch (NotSet&) { }
        string peer(host != "" ? host + ":" + port : "");

        log_info << "gcomm: connecting to group '" << channel
                 << "', peer '" << peer << "'";
        tp->connect();
        uuid = tp->get_uuid();

        int err;

        if ((err = pthread_create(&thd, 0, &run_fn, this)) != 0)
        {
            gu_throw_error(err);
        }
        log_info << "gcomm: connected";
    }

    void close()
    {
        if (tp == 0)
        {
            log_warn << "gcomm: backend already closed";
            return;
        }
        log_info << "gcomm: terminating thread";
        terminate();
        log_info << "gcomm: joining thread";
        pthread_join(thd, 0);
        log_info << "gcomm: closing backend";
        tp->close(error != 0);
        gcomm::disconnect(tp, this);
        delete tp;
        tp = 0;

        const Message* msg;

        while ((msg = get_next_msg()) != 0)
        {
            return_ack(Message(&msg->get_producer(), 0, -ECONNABORTED));
        }
        log_info << "gcomm: closed";
        log_debug << prof;
    }

    void run();

    void notify() { net->interrupt(); }

    void terminate()
    {
        Lock lock(mutex);
        terminated = true;
        net->interrupt();
    }

    void handle_up     (const void*        id,
                        const Datagram&    dg,
                        const ProtoUpMeta& um);

    void queue_and_wait(const Message& msg, Message* ack);

    RecvBuf&    get_recv_buf()            { return recv_buf; }
    size_t      get_mtu()           const
    {
        if (tp == 0)
        {
            gu_throw_fatal << "GCommConn::get_mtu(): "
                           << "backend connection not open";
        }
        return tp->get_mtu();
    }
    bool        get_use_prod_cons() const { return use_prod_cons; }
    Protonet&   get_pnet()                { return *net; }
    gu::Config& get_conf()                { return conf; }
    int         get_error() const         { return error; }
    class Ref
    {
    public:

        Ref(gcs_backend_t* ptr, bool unset = false) : conn(0)
        {
            if (ptr->conn != 0)
            {
                conn = reinterpret_cast<GCommConn*>(ptr->conn)->ref(unset);

                if (unset == true)
                {
                    ptr->conn = 0;
                }
            }
        }

        ~Ref()
        {
            if (conn != 0)
            {
                conn->unref();
            }
        }

        GCommConn* get() { return conn; }

    private:

        Ref(const Ref&);
        void operator=(const Ref&);

        GCommConn* conn;
    };

private:

    GCommConn(const GCommConn&);
    void operator=(const GCommConn&);

    GCommConn* ref(const bool unsetting)
    {
        return this;
    }

    void unref() { }

    gu::Config& conf;
    UUID uuid;
    pthread_t thd;
    URI uri;
    bool use_prod_cons;
    Protonet* net;
    Transport* tp;
    Mutex mutex;
    size_t refcnt;
    bool terminated;
    int error;
    RecvBuf recv_buf;
    View current_view;
    Profile prof;
};


void GCommConn::handle_up(const void* id, const Datagram& dg, const ProtoUpMeta& um)
{
    if (um.get_errno() != 0)
    {
        error = um.get_errno();
        recv_buf.push_back(RecvBufData(numeric_limits<size_t>::max(), dg, um));
    }
    else if (um.has_view() == true)
    {
        current_view = um.get_view();
        recv_buf.push_back(RecvBufData(numeric_limits<size_t>::max(), dg, um));
        if (current_view.is_empty())
        {
            log_debug << "handle_up: self leave";
        }
    }
    else
    {
        size_t idx(0);
        for (NodeList::const_iterator i = current_view.get_members().begin();
             i != current_view.get_members().end(); ++i)
        {
            if (NodeList::get_key(i) == um.get_source())
            {
                profile_enter(prof);
                recv_buf.push_back(RecvBufData(idx, dg, um));
                profile_leave(prof);
                break;
            }
            ++idx;
        }
        assert(idx < current_view.get_members().size());
    }
}


void GCommConn::queue_and_wait(const Message& msg, Message* ack)
{
    {
        Lock lock(mutex);
        if (terminated == true)
        {
            *ack = Message(&msg.get_producer(), 0, -ECONNABORTED);
            return;
        }
    }
    profile_enter(prof);
    Consumer::queue_and_wait(msg, ack);
    profile_leave(prof);
}


void GCommConn::run()
{
    while (true)
    {
        {
            Lock lock(mutex);

            if (terminated == true)
            {
                if (get_use_prod_cons() == true)
                {
                    const Message* msg;

                    while ((msg = get_next_msg()) != 0)
                    {
                        return_ack(Message(&msg->get_producer(), 0,
                                           -ECONNABORTED));
                    }
                }
                break;
            }
        }

        if (get_use_prod_cons() == true)
        {
            const Message* msg;

            if ((msg = get_next_msg()) != 0)
            {
                const MsgData* md(static_cast<const MsgData*>(msg->get_data()));
                Buffer buf(md->get_data(), md->get_data() + md->get_data_size());
                Datagram dg(buf);
                int err = send_down(dg, ProtoDownMeta(md->get_msg_type(),
                                                      md->get_msg_type() == GCS_MSG_CAUSAL ? O_LOCAL_CAUSAL : O_SAFE));

                return_ack(Message(&msg->get_producer(), 0, err != 0 ?
                                   -err : static_cast<int>(dg.get_len())));
            }
        }

        try
        {
            net->event_loop(Sec);
        }
        catch (gu::Exception& e)
        {
            log_error << "exception from gcomm, backend must be restarted:"
                      << e.what();
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
        catch (...)
        {
            log_error
                << "unknow exception from gcomm, backend must be restarted";
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

    if (conn.get_use_prod_cons() == true)
    {
        Producer prod(conn);
        Message ack;
        MsgData msg_data(reinterpret_cast<const byte_t*>(buf), len, msg_type);
        conn.queue_and_wait(Message(&prod, &msg_data), &ack);
        return ack.get_val();
    }
    else
    {
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
}


static void fill_cmp_msg(const View& view, const UUID& my_uuid,
                         gcs_comp_msg_t* cm) throw (gu::Exception)
{
    size_t n(0);

    for (NodeList::const_iterator i = view.get_members().begin();
         i != view.get_members().end(); ++i)
    {
        const UUID& uuid(NodeList::get_key(i));

        log_debug << "member: " << n << " uuid: " << uuid;

//        (void)snprintf(cm->memb[n].id, GCS_COMP_MEMB_ID_MAX_LEN, "%s",
//                       uuid._str().c_str());
        long ret = gcs_comp_msg_add (cm, uuid._str().c_str());
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

        if (gu_likely(dg.get_len() != 0))
        {
            assert(dg.get_len() > dg.get_offset());

            const byte_t* b(get_begin(dg));
            const ssize_t pload_len(get_available(dg));

            msg->size = pload_len;

            if (gu_likely(pload_len <= msg->buf_len))
            {
                memcpy(msg->buf, b, pload_len);
                msg->type = static_cast<gcs_msg_type_t>(um.get_user_type());
                recv_buf.pop_front();
            }
            else
            {
                msg->type = GCS_MSG_ERROR;
            }
        }
        else if (um.get_errno() != 0)
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

            const View& view(um.get_view());

            assert(view.get_type() == V_PRIM || view.get_type() == V_NON_PRIM);

            gcs_comp_msg_t* cm(gcs_comp_msg_new(view.get_type() == V_PRIM,
                                                view.is_bootstrap(),
                                                view.is_empty() ? -1 : 0,
                                                view.get_members().size()));

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
        conn.connect(channel);
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
            log_warn << "param " << key << " not recognized";
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
        gu::URI uri(std::string("pc://") + socket);
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
