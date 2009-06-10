extern "C" {
#include "gcs_gcomm.h"
#include "gu_mutex.h"
#include "gu_log.h"
}

// We access data comp msg struct directly
extern "C" {
#define GCS_COMP_MSG_ACCESS 1
#include "gcs_comp_msg.h"
}

#include <gcomm/gcomm.hpp>

#include <limits>
#include <deque>
#include <map>
#include <utility>

using std::deque;
using std::map;
using std::pair;
using std::make_pair;

using namespace gcomm;

struct vs_ev
{
    ReadBuf* rb;
    ProtoUpMeta um;
    View *view;
    size_t msg_size;
    vs_ev(const ReadBuf *r, 
          const ProtoUpMeta* um_,
          const size_t roff, 
          const size_t ms, const View *v) :
	rb(0), 
        um(um_ ? *um_ : ProtoUpMeta()),
        msg_size(ms), 
        view(0) {
	if (r)
	    rb = r->copy(roff);
	if (v)
	    view = new View(*v);
    }
};

struct gcs_gcomm : public Toplay
{
    Transport* vs;
    EventLoop* el;
    deque<vs_ev> eq;
    void *waiter_buf;
    size_t waiter_buf_len;
    gu_mutex_t mutex;
    gu_cond_t cond;
    Monitor monitor;

    gcs_gcomm() : 
        vs(0), 
        el(0), 
        waiter_buf(0), 
        waiter_buf_len(0)
    {
	gu_mutex_init(&mutex, 0);
	gu_cond_init(&cond, 0);
    }
    
    ~gcs_gcomm() 
    {
	for (deque<vs_ev>::iterator i = eq.begin(); i != eq.end(); ++i) 
        {
	    if (i->rb)
		i->rb->release();
            delete i->view;
	}
	gu_cond_destroy(&cond);
	gu_mutex_destroy(&mutex);
    }
    
    void handle_up(const int cid, const ReadBuf *rb, const size_t roff, 
		   const ProtoUpMeta *um) 
    {

	// null rb and um denotes eof (broken connection)
	if (!(rb || um)) 
        {
            gu_warn("gcomm backed thread exit");
	    gu_mutex_lock(&mutex);
	    eq.push_back(vs_ev(0, 0, 0, 0, 0));
	    gu_cond_signal(&cond);
	    gu_mutex_unlock(&mutex);
	    return;
	}
        
	assert(rb || (um->get_view() && 
                      (um->get_view()->get_type() == View::V_PRIM ||
                       um->get_view()->get_type() == View::V_NON_PRIM)));
        
        if (um->get_view() && um->get_view()->is_empty()) 
        {
	    gu_debug("empty view, leaving");
	    eq.push_back(vs_ev(0, 0, 0, 0, um->get_view()));            
	    // Reached the end
	    // Todo: add gu_thread_exit() to gu library
            gu_mutex_lock(&mutex);
            gu_cond_signal(&cond);
            gu_mutex_unlock(&mutex);
            pthread_exit(0);
	}
	gu_mutex_lock(&mutex);
	if (rb && eq.empty() && rb->get_len(roff) <= waiter_buf_len)
        {
	    memcpy(waiter_buf, rb->get_buf(roff), rb->get_len(roff));
	    eq.push_back(vs_ev(0, um, roff, rb->get_len(roff), 0));
	    // Zero pointer/len here to avoid rewriting the buffer if 
	    // waiter does not wake up before next message
	    waiter_buf = 0;
	    waiter_buf_len = 0;
	} 
        else 
        {
	    eq.push_back(vs_ev(rb, um, roff, 0, um->get_view()));
	}
	gu_cond_signal(&cond);
	gu_mutex_unlock(&mutex);
    }
    std::pair<vs_ev, bool> wait_event(void* wb, size_t wb_len) {
	gu_mutex_lock(&mutex);
	while (eq.size() == 0) {
	    waiter_buf = wb;
	    waiter_buf_len = wb_len;
	    gu_cond_wait(&cond, &mutex);
	}
	std::pair<vs_ev, bool> ret(eq.front(), eq.size() ? true : false);
	gu_mutex_unlock(&mutex);
	return ret;
    }
    
    void release_event() {
	assert(eq.size());
	gu_mutex_lock(&mutex);
	vs_ev ev = eq.front();
	eq.pop_front();
	if (ev.rb)
	    ev.rb->release();
	delete ev.view;
	gu_mutex_unlock(&mutex);
    }
};

typedef map<const UUID, long> CompMap;

typedef struct gcs_backend_conn 
{
    string sock;
    string channel;
    size_t last_view_size;
    size_t max_msg_size;
    unsigned long long n_received;
    unsigned long long n_copied;
    gcs_gcomm vs_ctx;
    gu_thread_t thr;
    gcs_comp_msg_t *comp_msg;
    CompMap comp_map;
    gcs_backend_conn() : last_view_size(0), max_msg_size(1 << 20),
			 n_received(0), n_copied(0),
			 comp_msg(0) {}
} conn_t;



static GCS_BACKEND_MSG_SIZE_FN(gcs_gcomm_msg_size)
{
    return backend->conn->max_msg_size;
}

static GCS_BACKEND_SEND_FN(gcs_gcomm_send)
{
    conn_t *conn = backend->conn;

    if (conn == 0)
    {
        gu_warn("-EBADFD");
	return -EBADFD;
    }
    if (conn->vs_ctx.vs == 0)
    {
        gu_warn("-ENOTCONN");
	return -ENOTCONN;
    }
    if (msg_type < 0 || msg_type >= 0xff)
    {
        gu_warn("-EINVAL");
	return -EINVAL;
    }
    int err = 0;
    WriteBuf wb(static_cast<const gcomm::byte_t*>(buf), len);
    try {
	ProtoDownMeta vdm(msg_type);
	err = conn->vs_ctx.pass_down(&wb, &vdm);
    } catch (Exception e) {
	return -ENOTCONN;
    }

    if (err != 0)
    {
        gu_warn("pass_down(): %s", strerror(err));
    }
    return err == 0 ? len : -err;
}



static void fill_comp(gcs_comp_msg_t *msg,
		      CompMap *comp_map,
		      const NodeList& members, 
                      const UUID& self)
{
    size_t n = 0;
    // TODO: 
    assert(msg != 0 && 
           static_cast<size_t>(msg->memb_num) == members.length() &&
           comp_map != 0);
    
    comp_map->clear();
    for (NodeList::const_iterator i = members.begin(); i != members.end(); ++i)
    {
        const UUID& pid = get_uuid(i);
	if (snprintf(msg->memb[n].id, sizeof(msg->memb[n].id), "%s",
                     pid.to_string().c_str()) != pid.to_string().size())
        {
            gu_fatal("PID string does not fit into comp msg buffer");
            abort();
        }
	if (pid == self)
        {
	    msg->my_idx = n;
        }
	if (comp_map)
        {
	    comp_map->insert(make_pair(pid, n));
        }
	n++;	    
    }
}


static GCS_BACKEND_RECV_FN(gcs_gcomm_recv)
{
    long ret = 0;
    long cpy = 0;
    conn_t *conn = backend->conn;
    if (conn == 0)
    {
        gu_warn("gcs_gcomm_recv: -EBADFD");
	return -EBADFD;
    }
retry:
    std::pair<vs_ev, bool> wr(conn->vs_ctx.wait_event(buf, len));
    if (wr.second == false)
    {
        gu_warn("gcs_gcomm_recv: -ENOTCONN");
	return -ENOTCONN;
    }
    vs_ev& ev(wr.first);

    if (!(ev.rb || ev.msg_size || ev.view))
    {
        gu_warn("gcs_gcomm_recv: -ENOTCONN");
	return -ENOTCONN;
    }
    
    assert(ev.rb || ev.msg_size|| ev.view);
    
    if (ev.rb || ev.msg_size) 
    {
	*msg_type = static_cast<gcs_msg_type_t>(ev.um.get_user_type());
	CompMap::const_iterator i = conn->comp_map.find(ev.um.get_source());
	assert(i != conn->comp_map.end());
	*sender_id = i->second;
	if (ev.rb) {
	    ret = ev.rb->get_len();
	    if (ret <= len) {
		memcpy(buf, ev.rb->get_buf(), ret);
		conn->n_copied++;
	    }
	} else {
	    assert(ev.msg_size > 0);
	    ret = ev.msg_size;
	}
    } 
    else 
    {
	gcs_comp_msg_t *new_comp;

        if (ev.view->is_empty() == false)
        {
            new_comp =
                gcs_comp_msg_new(ev.view->get_type() == View::V_PRIM, 0, 
                                 ev.view->get_members().length());
        }        
        else
        {
            return -ENOTCONN;
            // new_comp = gcs_comp_msg_leave();
        }
        
        if (!new_comp) 
        {
            gu_fatal ("Failed to allocate new component message.");
            return -ENOMEM;
        }
        
	fill_comp(new_comp, &conn->comp_map, ev.view->get_members(), 
                  conn->vs_ctx.vs->get_uuid());
	if (conn->comp_msg) 
        {
            gcs_comp_msg_delete(conn->comp_msg);
        }
	conn->comp_msg = new_comp;
	cpy = std::min(static_cast<size_t>(gcs_comp_msg_size(conn->comp_msg)), len);
	ret = std::max(static_cast<size_t>(gcs_comp_msg_size(conn->comp_msg)), len);
	memcpy(buf, conn->comp_msg, cpy);
	*msg_type = GCS_MSG_COMPONENT;
    }
    if (static_cast<size_t>(ret) <= len) 
    {
	conn->vs_ctx.release_event();
	conn->n_received++;
    }
    return ret;
}

static GCS_BACKEND_NAME_FN(gcs_gcomm_name)
{
    static const char *name = "gcomm";
    return name;
}

static void *conn_run(void *arg)
{
    conn_t *conn = reinterpret_cast<conn_t*>(arg);
    try {

	while (true) {
	    int err = conn->vs_ctx.el->poll(std::numeric_limits<int>::max());
	    if (err < 0) {
		gu_fatal("unrecoverable error: '%s'", strerror(err));
		abort();
	    }
	}
    } catch (Exception e) {
	gu_error("poll error: '%s', thread exiting", e.what());
	conn->vs_ctx.handle_up(-1, 0, 0, 0);
    }
    return 0;
}

static GCS_BACKEND_OPEN_FN(gcs_gcomm_open)
{
    conn_t *conn = backend->conn;

    if (!conn) return -EBADFD;

    try {
        conn->channel = channel;
        string uri_str = string("gcomm+pc://");
        uri_str += conn->sock;
        if (conn->sock.find_first_of('?') == string::npos)
        {
            uri_str += "?";
        }
        else
        {
            uri_str += "&";
        }

        uri_str += "gmcast.group=" + conn->channel;
        gu_debug("uri: %s", uri_str.c_str());
	conn->vs_ctx.vs = Transport::create(uri_str, conn->vs_ctx.el);
        gcomm::connect(conn->vs_ctx.vs, &conn->vs_ctx);
	conn->vs_ctx.vs->connect();

	int err = gu_thread_create(&conn->thr, 0, &conn_run, conn);
	if (err != 0)
	    return -err;
    } catch (Exception e) {
	return -EINVAL;
    }
    
    return 0;
}

static GCS_BACKEND_CLOSE_FN(gcs_gcomm_close)
{
    conn_t *conn = backend->conn;
    if (conn == 0)
	return -EBADFD;
    gu_debug("closing gcomm backend");
    conn->vs_ctx.vs->close();
    delete conn->vs_ctx.vs;
    conn->vs_ctx.vs = 0;
    gu_debug("joining backend recv thread");
    gu_thread_join(conn->thr, 0);
    gu_debug("close done");
    return 0;
}

static GCS_BACKEND_DESTROY_FN(gcs_gcomm_destroy)
{
    conn_t *conn = backend->conn;
    if (conn == 0)
	return -EBADFD;
    backend->conn = 0;
    
    delete conn->vs_ctx.el;
    if (conn->comp_msg) gcs_comp_msg_delete (conn->comp_msg);

    gu_debug("received %llu copied %llu", conn->n_received, conn->n_copied);

    delete conn;
    
    gu_debug("gcs_gcomm_close(): return 0");
    return 0;
}

static const char* vs_default_socket = "gcomm+vs://127.0.0.1:4567?gmcast.group=local";

GCS_BACKEND_CREATE_FN(gcs_gcomm_create)
{
    conn_t *conn = 0;
    const char* sock = socket;
    
    if (NULL == sock || strlen(sock) == 0)
        sock = vs_default_socket;

    gu_debug ("Opening connection to '%s'", sock);

    try {
	conn = new gcs_backend_conn;	
    } catch (std::bad_alloc e) {
	return -ENOMEM;
    }
    
    try {
	conn->vs_ctx.el = new EventLoop();
        conn->sock = sock;
    } catch (Exception e) {
	delete conn;
	return -EINVAL;
    }
    conn->comp_msg = 0;
    
    backend->open     = &gcs_gcomm_open;
    backend->close    = &gcs_gcomm_close;
    backend->destroy  = &gcs_gcomm_destroy;
    backend->send     = &gcs_gcomm_send;
    backend->recv     = &gcs_gcomm_recv;
    backend->name     = &gcs_gcomm_name;
    backend->msg_size = &gcs_gcomm_msg_size;
    backend->conn     = conn;
    
    return 0;
}

