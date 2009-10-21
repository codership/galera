// Copyright (C) 2009 Codership Oy <info@codership.com>

#include <limits>
#include <deque>
#include <map>
#include <utility>

#include <galerautils.hpp>

#include <gcomm/gcomm.hpp>

extern "C" {
#include "gcs_gcomm.h"
#include "gu_mutex.h"
}

// We access data comp msg struct directly
extern "C" {
#define GCS_COMP_MSG_ACCESS 1
#include "gcs_comp_msg.h"
}

using std::string;
using std::deque;
using std::map;
using std::pair;
using std::make_pair;

namespace gcomm {

struct vs_ev
{
    ReadBuf*     rb;
    ProtoUpMeta um;
    const View*        view;
    size_t       msg_size;

    vs_ev(const ReadBuf *r, 
          const ProtoUpMeta& um_,
          const size_t roff, 
          const size_t ms, const View *v) :
	rb(0), 
        um(um_),
        view(0),
        msg_size(ms) 
    {
	if (r)
	    rb = r->copy(roff);
	if (v)
	    view = new View(*v);
    }

    // looks like we need a shallow copy here to go through queue
    vs_ev (const vs_ev& ev) :
        rb       (ev.rb),
        um       (ev.um),
        view     (ev.view),
        msg_size (ev.msg_size)
    {}

    ~vs_ev () {}

    void release ()
    {
        if (rb)   { rb->release(); rb   = 0; }
        if (view) { delete view;   view = 0; }
    }

private:

    vs_ev& operator= (const vs_ev&);
};

struct gcs_gcomm : public Toplay
{
    Transport*   vs;
    EventLoop*   el;
    deque<vs_ev> eq;
    void*        waiter_buf;
    size_t       waiter_buf_len;
    gu::Mutex    mutex;
    gu::Cond     cond;

    gcs_gcomm() :
        vs(0),
        el(0),
        eq(),
        waiter_buf(0), 
        waiter_buf_len(0),
        mutex(),
        cond()
    {}
    
    void release_event()
    {
	assert(eq.size());
        gu::Lock lock(mutex);

	eq.front().release();
	eq.pop_front();
    }

    ~gcs_gcomm() 
    {
        while (eq.size()) { release_event(); }
    }
    
    void handle_up(int cid, const ReadBuf *rb, size_t roff, 
		   const ProtoUpMeta& um) 
    {

	assert(rb || (um.has_view() == true && 
                      (um.get_view().get_type() == V_PRIM ||
                       um.get_view().get_type() == V_NON_PRIM)));
        
        if (um.has_view() == true && um.get_view().is_empty() == true)
        {
	    log_debug << "empty view, leaving";
	    eq.push_back(vs_ev(0, um, 0, 0, &um.get_view()));
	    // Reached the end
            {
                gu::Lock lock(mutex);
                cond.signal();
            }

            el->interrupt();
	}

        // log_info << "[DEBUG] Got message: type: " << um->get_user_type()
        //      << ", sender: " << um->get_source().to_string();

        gu::Lock lock(mutex);

	if (rb && eq.empty() && rb->get_len(roff) <= waiter_buf_len)
        {
// @todo: it seems like the only thing to be done in this block is cond.signal()
//        and eq.push_back() sould be done unconditionally.
	    memcpy(waiter_buf, rb->get_buf(roff), rb->get_len(roff));
	    eq.push_back(vs_ev(0, um, roff, rb->get_len(roff), 0));
	    // Zero pointer/len here to avoid rewriting the buffer if 
	    // waiter does not wake up before next message
	    waiter_buf     = 0;
	    waiter_buf_len = 0;
	} 
        else
        {
	    eq.push_back(vs_ev(rb, um, roff, 0, 
                               um.has_view() ? &um.get_view() : 0));
	}
        
	cond.signal();
    }

    std::pair<vs_ev, bool> wait_event (void* wb, size_t wb_len)
    {
        gu::Lock lock(mutex);
        
	while (eq.size() == 0)
        {
	    waiter_buf     = wb;
	    waiter_buf_len = wb_len;
	    lock.wait(cond);
	}

// @todo: it does not look like we can have eq.size() == 0 here.
//        So there's no point to return a pair?
	std::pair<vs_ev, bool> ret (eq.front(), eq.size() != 0);

	return ret;
    }

private:

    gcs_gcomm (const gcs_gcomm&);
    gcs_gcomm& operator= (const gcs_gcomm&);
};

typedef map<const UUID, long> CompMap;

struct gcomm_ctx
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
    volatile bool terminate;

    gcomm_ctx()
        :
        sock(),
        channel(),
        last_view_size(0), 
        max_msg_size(1 << 20),
        n_received(0), 
        n_copied(0),
        vs_ctx(),
        thr(0),
        comp_msg(0),
        comp_map(),
        terminate(false)
    {}

private:

    gcomm_ctx (const gcomm_ctx&);
    gcomm_ctx& operator= (const gcomm_ctx&);
};

static GCS_BACKEND_MSG_SIZE_FN(gcs_gcomm_msg_size)
{
    return reinterpret_cast<gcomm_ctx*>(backend->conn)->max_msg_size;
}

static GCS_BACKEND_SEND_FN(gcs_gcomm_send)
{
    gcomm_ctx* conn = reinterpret_cast<gcomm_ctx*>(backend->conn);

    if (conn == 0)
    {
        log_warn << "-EBADFD";
	return -EBADFD;
    }

    if (conn->vs_ctx.vs == 0)
    {
        log_warn << "-ENOTCONN";
	return -ENOTCONN;
    }

    if (msg_type < 0 || msg_type >= 0xff)
    {
        log_warn << "-EINVAL";
	return -EINVAL;
    }

    int err = 0;
    WriteBuf wb (static_cast<const gcomm::byte_t*>(buf), len);

    try
    {
	err = conn->vs_ctx.pass_down(&wb, ProtoDownMeta(msg_type));
    }
    catch (Exception& e)
    {
        log_error << e.what();
	return -e.get_errno();
    }

    if (err != 0)
    {
        log_error << "pass_down(): " << strerror(err);
    }
    return err == 0 ? len : -err;
}



static void fill_comp(gcs_comp_msg_t* msg,
		      CompMap*        comp_map,
		      const NodeList& members, 
                      const UUID&     self)
{
    // TODO: 
    assert(msg != 0 && 
           static_cast<size_t>(msg->memb_num) == members.size() &&
           comp_map != 0);
    
    comp_map->clear();

    size_t n = 0;

    for (NodeList::const_iterator i = members.begin(); i != members.end(); ++i)
    {
        const UUID& pid = NodeList::get_key(i);

	if (snprintf(msg->memb[n].id, sizeof(msg->memb[n].id), "%s",
                     pid.to_string().c_str())
            >= static_cast<ssize_t>(sizeof(msg->memb[n].id)))
        {
            log_fatal << "PID string does not fit into comp msg buffer";
            abort();
        }

	if (pid == self) msg->my_idx = n;

	if (comp_map)
        {
            log_info << "[DEBUG] comp_map += (" << pid << ", "
                     << n<< ")";
            
            if (!(comp_map->insert (make_pair (pid, n))).second)
                gu_throw_fatal << "Could not insert into map: ("
                               << pid.to_string() << ", " << n << ")\n"
                               << "NodeList: " << members;
        }

	n++;
    }
}


static GCS_BACKEND_RECV_FN(gcs_gcomm_recv)
{
    long ret = 0;
    long cpy = 0;

    gcomm_ctx* conn = reinterpret_cast<gcomm_ctx*>(backend->conn);

    if (conn == 0)
    {
        log_warn << "gcs_gcomm_recv: -EBADFD";
	return -EBADFD;
    }

    if (conn->terminate == true)
    {
        return -ENOTCONN;
    }

    std::pair<vs_ev, bool> wr (conn->vs_ctx.wait_event(buf, len));

    if (wr.second == false)
    {
        // as pointed out above wait_event() does not seem to be able to return
        // false in wr.second
        log_warn << "-ENOTCONN";
	return -ENOTCONN;
    }

    vs_ev& ev(wr.first);

    if (!(ev.rb || ev.msg_size || ev.view))
    {
        log_warn << "-ENOTCONN";
	return -ENOTCONN;
    }
    
    assert(ev.rb || ev.msg_size|| ev.view);
    
    if (ev.rb || ev.msg_size) 
    {
	*msg_type = static_cast<gcs_msg_type_t>(ev.um.get_user_type());

	CompMap::const_iterator i = conn->comp_map.find(ev.um.get_source());

	assert(i != conn->comp_map.end());

	*sender_idx = i->second;

        // log_info << "[DEBUG] Got user message: type: " << *msg_type
        //         << ", sender: (" << ev.um.get_source().to_string()
        //         << ", index: " << *sender_idx << ")";

	if (ev.rb)
        {
	    ret = ev.rb->get_len();

	    if (ret <= static_cast<ssize_t>(len))
            {
		memcpy(buf, ev.rb->get_buf(), ret);
		conn->n_copied++;
	    }
	}
        else
        {
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
                gcs_comp_msg_new(ev.view->get_type() == V_PRIM, 0, 
                                 ev.view->get_members().size());
        }        
        else
        {
            new_comp = gcs_comp_msg_leave();
        }
        
        if (!new_comp) 
        {
            log_fatal << "Failed to allocate new component message.";
            return -ENOMEM;
        }
        
	fill_comp(new_comp, &conn->comp_map, ev.view->get_members(), 
                  conn->vs_ctx.vs->get_uuid());

	if (conn->comp_msg) 
        {
            gcs_comp_msg_delete(conn->comp_msg);
        }

	conn->comp_msg = new_comp;

	cpy = std::min(static_cast<size_t>(gcs_comp_msg_size(conn->comp_msg)),
                       len);
	ret = std::max(static_cast<size_t>(gcs_comp_msg_size(conn->comp_msg)),
                       len);

	memcpy(buf, conn->comp_msg, cpy);
	*msg_type = GCS_MSG_COMPONENT;
    }

    if (ret <= static_cast<ssize_t>(len)) 
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
    gcomm_ctx* conn = reinterpret_cast<gcomm_ctx*>(arg);
    while (conn->terminate == false) 
    {
        int err = conn->vs_ctx.el->poll(200);
        if (err < 0 && conn->vs_ctx.el->is_interrupted() == true) 
        {
            log_info << "event loop interrupted";
            break;
        }
        else if (err < 0)
        {
            log_fatal << "unrecoverable error: " << err
                      << " (" << strerror(err) << ')';
            abort();
        }
    }
    
    if (conn->vs_ctx.el->is_interrupted() == false)
    {
        conn->vs_ctx.vs->close();
    }

    return 0;
}

static GCS_BACKEND_OPEN_FN(gcs_gcomm_open)
{
    gcomm_ctx* conn = reinterpret_cast<gcomm_ctx*>(backend->conn);

    if (!conn) return -EBADFD;

    conn->channel = channel;

    string uri_str("gcomm+pc://");

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
    log_debug << "uri: " << uri_str;

    try
    {
	conn->vs_ctx.vs = Transport::create(uri_str, conn->vs_ctx.el);
        gcomm::connect(conn->vs_ctx.vs, &conn->vs_ctx);
	conn->vs_ctx.vs->connect();

	int err = gu_thread_create(&conn->thr, 0, &conn_run, conn);

	if (err != 0) return -err;
    }
    catch (Exception& e)
    {
        log_error << e.what();
	return -e.get_errno();
    }
    
    return 0;
}

static GCS_BACKEND_CLOSE_FN(gcs_gcomm_close)
{
    gcomm_ctx* conn = reinterpret_cast<gcomm_ctx*>(backend->conn);

    if (conn == 0) return -EBADFD;

    log_debug << "closing gcomm backend";
    conn->terminate = true;
    log_debug << "joining backend recv thread";
    gu_thread_join(conn->thr, 0);
    delete conn->vs_ctx.vs;
    log_debug << "close done";

    return 0;
}

static GCS_BACKEND_DESTROY_FN(gcs_gcomm_destroy)
{
    gcomm_ctx* conn = reinterpret_cast<gcomm_ctx*>(backend->conn);
    if (conn == 0)
	return -EBADFD;
    backend->conn = 0;
    
    delete conn->vs_ctx.el;
    if (conn->comp_msg) gcs_comp_msg_delete (conn->comp_msg);
    
    log_debug << "received: " << conn->n_received
              << ", copied: " << conn->n_copied;

    delete conn;
    
    log_debug << "gcs_gcomm_close(): return 0";
    return 0;
}


GCS_BACKEND_CREATE_FN(gcs_gcomm_create)
{
    gcomm_ctx*  conn = 0;
    const char* sock = socket;
    
    log_debug << "Opening connection to '" << sock << '\'';

    try
    {
	conn = new gcomm_ctx;	
    }
    catch (std::bad_alloc& e)
    {
	return -ENOMEM;
    }
    
    try
    {
	conn->vs_ctx.el = new EventLoop();
        conn->sock = sock;
    }
    catch (Exception& e)
    {
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
    backend->conn     = reinterpret_cast<gcs_backend_conn_t*>(conn);
    
    return 0;
}

} // namespace gcomm

GCS_BACKEND_CREATE_FN(gcs_gcomm_create)
{
    return gcomm::gcs_gcomm_create (backend, socket);
}
