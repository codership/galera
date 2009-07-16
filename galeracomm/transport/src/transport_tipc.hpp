
#include "galeracomm/transport.hpp"
#include "galeracomm/exception.hpp"

class TIPCTransport : public Transport, PollContext {
    int fd;
    int tsfd; // Topology server
    sockaddr sa; // TIPC name
    sockaddr local_sa; // Local portid
    size_t sa_size;
    size_t max_msg_size;
    unsigned char* recv_buf;
    Poll *poll;
public:
    TIPCTransport(Poll* p);
    ~TIPCTransport();
    
    size_t get_max_msg_size() const;

    void connect(const char* addr);
    void close(const char* addr);
    void close();
    void listen(const char* addr) {
	throw FatalException("tipc: listen() not supported");
    }
    Transport *accept(Poll *, Protolay *) {
	throw FatalException("tipc: accept() not supported");
    }
    
    void handle(int, PollEnum);
    int handle_down(WriteBuf*, const ProtoDownMeta*);
    
};
