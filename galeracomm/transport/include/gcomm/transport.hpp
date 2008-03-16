#ifndef TRANSPORT_HPP
#define TRANSPORT_HPP

#include <gcomm/poll.hpp>
#include <gcomm/protolay.hpp>

typedef enum {
    TRANSPORT_NONE,
    TRANSPORT_TCP
} TransportType;

typedef enum {
    TRANSPORT_S_CLOSED,
    TRANSPORT_S_CONNECTING,
    TRANSPORT_S_CONNECTED,
    TRANSPORT_S_LISTENING,
    TRANSPORT_S_FAILED
} TransportState;

class Transport : public Bottomlay {
protected:
    TransportState state;
    int error_no;
    unsigned long contention_tout;
    unsigned long contention_tries;
    size_t max_pending_bytes;
    Transport() : state(TRANSPORT_S_CLOSED), error_no(0),
		  contention_tout(0), contention_tries(0),
		  max_pending_bytes(1024*1024*10) {
    }
    
public:
    void set_contention_params(unsigned long tout, unsigned long tries) {
	contention_tout = tout;
	contention_tries = tries;
    }
    void set_max_pending_bytes(size_t bytes) {
	max_pending_bytes = bytes;
    }

    virtual ~Transport() {
    }
    
    TransportState get_state() const {
	return state;
    }
    int get_errno() const {
	return error_no;
    }

    virtual void connect(const char *addr) = 0;
    virtual void close() = 0;
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
