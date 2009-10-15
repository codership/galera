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
    int      no_nagle;
    sockaddr sa;
    socklen_t   sa_size;
    size_t   recv_buf_size;
    byte_t*  recv_buf;
    size_t   recv_buf_offset;
    size_t   max_pending_bytes;
    size_t   pending_bytes;
    int     contention_tries;
    int     contention_tout;
    ReadBuf* recv_rb;

    // Used to hold pointer to ReadBuf that has been passed upwards. 
    // If receiver is running in separate thread and thread exits 
    // in handle_up(), reference to pointer is lost.
    ReadBuf* up_rb;

    struct PendingWriteBuf
    {
        WriteBuf* wb;
        size_t    offset;

        PendingWriteBuf(WriteBuf *wb_, size_t offset_) : 
            wb    (wb_),
            offset(offset_)
        {}

        PendingWriteBuf(const PendingWriteBuf& p) :
            wb    (p.wb),
            offset(p.offset)
        {}

        ~PendingWriteBuf() {}

    private:

        PendingWriteBuf& operator=(const PendingWriteBuf&);
    };

    std::deque<PendingWriteBuf> pending;

    bool                        non_blocking;

#if 0 // there seems to be no point in this function, this must be done in
      // constructor
    void set_blocking_mode() throw (RuntimeException)
    {

        URIQueryList::const_iterator qli =
            uri.get_query_list().find(Conf::TcpParamNonBlocking);

        if (qli != uri.get_query_list().end())
        {
            if (read_bool(get_query_value(qli)) == true)
            {
                log_debug << "using non-blocking mode for " << uri.to_string();
                non_blocking = true;
            }
        }
    }
#endif

    bool    is_non_blocking() const { return non_blocking; }
    
    int     recv_nointr();
    int     recv_nointr(int);
    ssize_t send_nointr(const void*  buf,
                        const size_t buflen, 
			const size_t offset,
                        int          flags);

    TCP (const TCP&);
    TCP& operator=(const TCP&);

public:

    TCP (const URI& uri_,
         EventLoop* event_loop_,
         Monitor*   mon)         throw (RuntimeException);

    ~TCP ()
    {
	if (fd != -1)
        {
            log_warn << "transport not closed before destruction";
            close();
        }

	free(recv_buf);

	if (up_rb) up_rb->release();
    }
    
    size_t get_max_msg_size() const { return max_pending_bytes; }

    std::string get_local_url()   const;
    std::string get_remote_url()  const;
    std::string get_remote_host() const;
    std::string get_remote_port() const;
    
    void connect();
    void close();
    void listen();
    Transport *accept();
    
    int  handle_down   (WriteBuf *, const ProtoDownMeta&);
    void handle_up     (int, const ReadBuf*, size_t, const ProtoUpMeta&);
    void handle_event  (int, const Event&);
    int  handle_pending();
    
    int            send(WriteBuf*, const ProtoDownMeta&);
    const ReadBuf* recv();
};

END_GCOMM_NAMESPACE

#endif // TRANSPORT_TCP_HPP
