#ifndef TRANSPORT_TCP_HPP
#define TRANSPORT_TCP_HPP

#include "gcomm/common.hpp"
#include "gcomm/transport.hpp"
#include "gcomm/conf.hpp"

#include <cstdlib>
#include <deque>
#include <sys/socket.h>

BEGIN_GCOMM_NAMESPACE

class TCP : public Transport, EventContext
{
    int no_nagle;
    sockaddr sa;
    size_t sa_size;
    size_t max_pending_bytes;
    size_t pending_bytes;
    unsigned char *recv_buf;
    size_t recv_buf_size;
    size_t recv_buf_offset;
    long contention_tries;
    long contention_tout;
    ReadBuf *recv_rb;
    // Used to hold pointer to ReadBuf that has been passed upwards. 
    // If receiver is running in separate thread and thread exits 
    // in handle_up(), reference to pointer is lost.
    ReadBuf *up_rb;

    struct PendingWriteBuf {
        WriteBuf *wb;
        size_t offset;
        PendingWriteBuf(WriteBuf *wb_, size_t offset_) : 
            wb(wb_), offset(offset_)
        {
        }
        PendingWriteBuf(const PendingWriteBuf& p) :
            wb(p.wb),
            offset(p.offset)
        {
        }
        ~PendingWriteBuf() {
        }

    private:

        void operator=(const PendingWriteBuf&);
    };

    std::deque<PendingWriteBuf> pending;
    
    bool non_blocking;
    void set_blocking_mode()
    {
        URIQueryList::const_iterator qli = uri.get_query_list().find(Conf::TcpParamNonBlocking);
        if (qli != uri.get_query_list().end())
        {
            if (read_bool(get_query_value(qli)) == true)
            {
                LOG_DEBUG("using non-blocking mode for " + uri.to_string());
                non_blocking = true;
            }
        }
    }
    
    bool is_non_blocking() const
    {
        return non_blocking;
    }
    
    int recv_nointr();
    int recv_nointr(int);
    ssize_t send_nointr(const void *buf, const size_t buflen, 
			const size_t offset, int flags);
    


    TCP(const TCP&);
    void operator=(const TCP&);
public:
    TCP(const URI& uri_, EventLoop* event_loop_, Monitor* mon) : 
	Transport(uri_, event_loop_, mon), 
        no_nagle(1),
        sa(),
        sa_size(),
        max_pending_bytes(4*1024*1024), 
        pending_bytes(0), 
        recv_buf(),
        recv_buf_size(),
	recv_buf_offset(0), 
        contention_tries(30),
        contention_tout(10),
        recv_rb(0),
        up_rb(0),
        pending(),
        non_blocking(false) 
    {
        /* */
	recv_buf_size = 65536;
	recv_buf = reinterpret_cast<unsigned char*>(::malloc(recv_buf_size));
        
        const URIQueryList& ql(uri.get_query_list());
        URIQueryList::const_iterator i = ql.find(Conf::TcpParamMaxPending);
        if (i != ql.end())
        {
            max_pending_bytes = read_long(get_query_value(i));
            LOG_DEBUG("max_pending_bytes: " + make_int(max_pending_bytes).to_string());
        }
    }

    ~TCP()
    {
	if (fd != -1)
        {
            LOG_WARN("transport not closed before destruction");
            close();
        }
	free(recv_buf);
	if (up_rb)
	    up_rb->release();
    }
    
    size_t get_max_msg_size() const
    {
	return max_pending_bytes;
    }

    std::string get_local_url() const;
    std::string get_remote_url() const;
    std::string get_remote_host() const;
    std::string get_remote_port() const;
    
    void connect();
    void close();
    void listen();
    Transport *accept();
    
    int handle_down(WriteBuf *, const ProtoDownMeta *);
    void handle_up(int, const ReadBuf*, const size_t, const ProtoUpMeta*);
    void handle_event(int, const Event&);
    int handle_pending();
    
    int send(WriteBuf*, const ProtoDownMeta*);
    const ReadBuf *recv();

};

END_GCOMM_NAMESPACE

#endif // TRANSPORT_TCP_HPP
