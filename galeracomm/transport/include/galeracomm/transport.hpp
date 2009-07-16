#ifndef TRANSPORT_HPP
#define TRANSPORT_HPP

#include <galeracomm/poll.hpp>
#include <galeracomm/protolay.hpp>
#include <galeracomm/sockaddr.hpp>


#include <vector>

typedef enum {
    TRANSPORT_NONE,
    TRANSPORT_TCP
} TransportType;

typedef enum {
    TRANSPORT_S_CLOSED,
    TRANSPORT_S_CONNECTING,
    TRANSPORT_S_CONNECTED,
    TRANSPORT_S_CLOSING,
    TRANSPORT_S_LISTENING,
    TRANSPORT_S_FAILED
} TransportState;

typedef enum {
    TRANSPORT_N_SOURCE,     // Source notification
    TRANSPORT_N_FAILURE,    // Failure notification
    TRANSPORT_N_SUBSCRIBED, // Subscription notification
    TRANSPORT_N_WITHDRAWN   // Withdrawal notification
} TransportNotificationType;




struct TransportNotification : public ProtoUpMeta {
    const char* type;
    TransportState state;
    TransportNotificationType ntype;
    size_t sa_size;
    sockaddr local_sa;
    sockaddr source_sa;
};


class Transport : public Bottomlay {
protected:
    bool synchronous;
    TransportState state;
    int error_no;
    unsigned long contention_tout;
    unsigned long contention_tries;
    size_t max_pending_bytes;
    Transport() : synchronous(false),
		  state(TRANSPORT_S_CLOSED), error_no(0),
		  contention_tout(0), contention_tries(0),
		  max_pending_bytes(1024*1024*10) {
    }
    
public:

    virtual ~Transport() {
    }

    void set_synchronous() {
	synchronous = true;
    }
    bool is_synchronous() const {
	return synchronous;
    }
    
    void set_contention_params(unsigned long tout, unsigned long tries) {
	contention_tout = tout;
	contention_tries = tries;
    }
    void set_max_pending_bytes(size_t bytes) {
	max_pending_bytes = bytes;
    }
    
    virtual size_t get_max_msg_size() const = 0;

    
    TransportState get_state() const {
	return state;
    }
    int get_errno() const {
	return error_no;
    }
    
    virtual void connect(const char *addr) = 0;
    virtual void close() = 0;
    // Override for transports that support multiple connect/bind operations
    virtual void close(const char *addr) {
	throw FatalException("Transport::close(const char*): Not supported");
    }
    virtual void listen(const char *addr) = 0;
    virtual Transport* accept(Poll *poll, Protolay *up_ctx) = 0;
    Transport *accept(Poll *p) {
	return accept(p, 0);
    }
    virtual int handle_down(WriteBuf *, const ProtoDownMeta *) = 0;
    
    
    // Send and receive methods are here as placeholders only until
    // there is real need. This is to make clear distinction between 
    // send()/recv() and handle_down()/handle_up() which have different
    // semantics (synchronous vs. asynchronous)
    virtual int send(WriteBuf *wb, const ProtoDownMeta *dm) {
	throw DException("Not implemented");
    }
    virtual const ReadBuf *recv() {
	throw DException("Not implemented");
    }
    
    
    static Transport *create(const char *, Poll *, Protolay *);
    static Transport *create(const char *, Poll *);

};


#endif // TRANSPORT_HPP
