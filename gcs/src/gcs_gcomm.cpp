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


#include "gcomm/transport.hpp"
#include "gcomm/util.hpp"
#include "gu_prodcons.hpp"

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
using namespace gu::net;
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
public:
    RecvBuf() : mutex(), cond(), queue(), waiting(false) { }
    
    void push_back(const RecvBufData& p)
    {
        Lock lock(mutex);
        queue.push_back(p);
        if (waiting == true)
        {
            cond.signal();
        }
    }
    
    const RecvBufData& front()
    {
        Lock lock(mutex);
        if (queue.empty() == true)
        {
            waiting = true;
            lock.wait(cond);
            waiting = false;
        }
        assert(queue.empty() == false);
        return queue.front();
    }
    
    void pop_front()
    {
        Lock lock(mutex);
        assert(queue.empty() == false);
        queue.pop_front();
    }
    
public:
    class DummyMutex
    {
    public:
        void lock() { }
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

    GCommConn(const string& uri_base_) :
        uuid(),
        thd(),
        uri_base("pc://" + uri_base_),
        use_prod_cons(false),
        net(Protonet::create(URI(uri_base).get_option("gcomm.protonet_backend", "gu"))),
        tp(0),
        mutex(),
        refcnt(0),
        terminated(false),
        recv_buf(),
        current_view(),
        prof("gcs_gcomm")
    { 
        try
        {
            use_prod_cons = from_string<bool>(URI(uri_base).get_option("gcomm.use_prod_cons"));
        } catch (NotFound&) { }

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
        
        char delim(uri_base.find_first_of('?') == string::npos ? '?' : '&');
        
        tp = Transport::create(*net, uri_base + delim + "gmcast.group=" + channel);
        gcomm::connect(tp, this);
        
        URI uri(uri_base);
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
        tp->close();

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
    
    void handle_up     (const void* id, const Datagram& dg, const ProtoUpMeta& um);
    void queue_and_wait(const Message& msg, Message* ack);
    
    RecvBuf&  get_recv_buf()            { return recv_buf; }
    size_t    get_mtu()           const { return tp->get_mtu(); }
    bool      get_use_prod_cons() const { return use_prod_cons; }
    Protonet& get_pnet()                { return *net; }
    
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
    
    UUID uuid;
    pthread_t thd;
    string uri_base;
    bool use_prod_cons;
    Protonet* net;
    Transport* tp;
    Mutex mutex;
    size_t refcnt;
    bool terminated;
    RecvBuf recv_buf;
    View current_view;
    Profile prof;
};


void GCommConn::handle_up(const void* id, const Datagram& dg, const ProtoUpMeta& um)
{
    if (um.has_view() == true)
    {
        current_view = um.get_view();
        recv_buf.push_back(RecvBufData(numeric_limits<size_t>::max(),
                                       dg, um));
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
                int err = send_down(dg, ProtoDownMeta(md->get_msg_type()));

                return_ack(Message(&msg->get_producer(), 0, err != 0 ?
                                   -err : static_cast<int>(dg.get_len())));
            }
        }
        
        net->event_loop(Sec);
    }
}


////////////////////////////////////////////////////////////////////////////
//
//                  Backend interface implementation
//
////////////////////////////////////////////////////////////////////////////


static GCS_BACKEND_MSG_SIZE_FN(gcs_gcomm_msg_size)
{
    GCommConn::Ref ref(backend);
    if (ref.get() == 0)
    {
        return -1;
    }
    return ref.get()->get_mtu();
}


static GCS_BACKEND_SEND_FN(gcs_gcomm_send)
{
    GCommConn::Ref ref(backend);

    if (ref.get() == 0)
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
        Critical<Protonet> crit(conn.get_pnet());
        int err = conn.send_down(
            dg,
            ProtoDownMeta(msg_type));
        return (err == 0 ? len : -err);
    }
}


static void fill_cmp_msg(const View& view, const UUID& my_uuid, 
                         gcs_comp_msg_t* cm)
{
    size_t n(0);

    for (NodeList::const_iterator i = view.get_members().begin();
         i != view.get_members().end(); ++i)
    {
        const UUID& uuid(NodeList::get_key(i));

        log_debug << "member: " << n << " uuid: " << uuid;

        (void)snprintf(cm->memb[n].id, GCS_COMP_MEMB_ID_MAX_LEN, "%s", 
                       uuid._str().c_str());

        if (uuid == my_uuid)
        {
            log_debug << "my index " << n;
            cm->my_idx = n;
        }

        ++n;
    }
}

static GCS_BACKEND_RECV_FN(gcs_gcomm_recv)
{
    GCommConn::Ref ref(backend);

    if (ref.get() == 0)
    {
        return -EBADFD;
    }

    GCommConn& conn(*ref.get());

    RecvBuf& recv_buf(conn.get_recv_buf());
    
    const RecvBufData& d(recv_buf.front());
    
    *sender_idx = d.get_source_idx();

    const Datagram&    dg(d.get_dgram());
    const ProtoUpMeta& um(d.get_um());
    
    if (dg.get_len() == 0)
    {
        assert(um.has_view() == true);

        const View& view(um.get_view());

        assert(view.get_type() == V_PRIM || view.get_type() == V_NON_PRIM);

        gcs_comp_msg_t* cm(gcs_comp_msg_new(view.get_type() == V_PRIM, 
                                            view.is_empty() ? -1 : 0, 
                                            view.get_members().size()));

        const size_t cm_size(gcs_comp_msg_size(cm));

        if (cm->my_idx == -1)
        {
            log_debug << "gcomm recv: self leave";
        }

        if (cm_size > len)
        {
            gcs_comp_msg_delete(cm);
            *msg_type = GCS_MSG_ERROR;
            return cm_size;
        }

        fill_cmp_msg(view, conn.get_uuid(), cm);
        memcpy(buf, cm, cm_size);
        gcs_comp_msg_delete(cm);
        recv_buf.pop_front();
        *msg_type = GCS_MSG_COMPONENT;
        return cm_size;
    }
    else
    {
        assert(dg.get_len() > dg.get_offset());

        const byte_t* b(get_begin(dg));
        const size_t pload_len(get_available(dg));
        
        if (pload_len > len)
        {
            *msg_type = GCS_MSG_ERROR;
            return pload_len;
        }
        
        memcpy(buf, b, pload_len);
        *msg_type = static_cast<gcs_msg_type_t>(um.get_user_type());
        recv_buf.pop_front();
        return pload_len;
    }
}


static GCS_BACKEND_NAME_FN(gcs_gcomm_name)
{
    static const char *name = "gcomm";
    return name;
}


static GCS_BACKEND_OPEN_FN(gcs_gcomm_open)
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


static GCS_BACKEND_CLOSE_FN(gcs_gcomm_close)
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
        return -e.get_errno();
    }

    return 0;
}


static GCS_BACKEND_DESTROY_FN(gcs_gcomm_destroy)
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


GCS_BACKEND_CREATE_FN(gcs_gcomm_create)
{
    GCommConn* conn(0);

    try
    {
        conn = new GCommConn(socket);
    }
    catch (Exception& e)
    {
        log_error << "failed to create gcomm backend connection: "
                  << e.get_errno() << ": "
                  << e.what();
        return -e.get_errno();
    }

    backend->open     = &gcs_gcomm_open;
    backend->close    = &gcs_gcomm_close;
    backend->destroy  = &gcs_gcomm_destroy;
    backend->send     = &gcs_gcomm_send;
    backend->recv     = &gcs_gcomm_recv;
    backend->name     = &gcs_gcomm_name;
    backend->msg_size = &gcs_gcomm_msg_size;
    backend->conn     = reinterpret_cast<gcs_backend_conn_t*>(conn);
    
    return 0;
}
