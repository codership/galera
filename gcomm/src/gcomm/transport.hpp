#ifndef TRANSPORT_HPP
#define TRANSPORT_HPP

#include <gcomm/common.hpp>
#include <gcomm/uri.hpp>
#include <gcomm/uuid.hpp>
#include <gcomm/event.hpp>

#include <vector>

BEGIN_GCOMM_NAMESPACE

typedef enum {
    S_CLOSED,
    S_CONNECTING,
    S_CONNECTED,
    S_CLOSING,
    S_LISTENING,
    S_FAILED
} TransportState;

class Transport : public Protolay
{
protected:
    URI uri;
    TransportState state;
    int error_no;
    EventLoop* event_loop;
    int fd;
    void set_state(TransportState);
    Transport(const URI& uri_, EventLoop* event_loop_);
public:
    virtual ~Transport();

    virtual size_t get_max_msg_size() const = 0;
    virtual bool supports_uuid() const;
    virtual const UUID& get_uuid() const;
    TransportState get_state() const;
    int get_errno() const;
    int get_fd() const;
    
    virtual void connect() = 0;
    virtual void close() = 0;
    virtual void listen();
    virtual Transport* accept();
    
    virtual int handle_down(WriteBuf*, const ProtoDownMeta*) = 0;
    virtual void handle_up(int, const ReadBuf*, const size_t, const ProtoUpMeta*) = 0;
    
    virtual int send(WriteBuf*, const ProtoDownMeta*);
    virtual const ReadBuf* recv();
    
    static Transport* create(const URI&, EventLoop*);
    static Transport* create(const string&, EventLoop*);
};

END_GCOMM_NAMESPACE


#endif // TRANSPORT_HPP
