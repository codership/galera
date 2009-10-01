#ifndef _GCOMM_TRANSPORT_HPP_
#define _GCOMM_TRANSPORT_HPP_

#include <vector>

#include <gcomm/common.hpp>
#include <gcomm/uri.hpp>
#include <gcomm/uuid.hpp>
#include <gcomm/event.hpp>

BEGIN_GCOMM_NAMESPACE

class Transport : public Protolay
{
    Transport (const Transport&);
    Transport& operator=(const Transport&);

public:

    typedef enum {
        S_CLOSED,
        S_CONNECTING,
        S_CONNECTED,
        S_CLOSING,
        S_LISTENING,
        S_FAILED
    } State;

protected:

    Monitor*       mon;
    URI            uri;
    State          state;
    int            error_no;
    EventLoop*     event_loop;
    int            fd;
    void           set_state(State);

    Transport (const URI& uri_, EventLoop* event_loop_, Monitor*);

public:

    virtual ~Transport();
    
    virtual size_t      get_max_msg_size() const = 0;
    virtual bool        supports_uuid()    const;
    virtual const UUID& get_uuid()         const;
    virtual std::string get_remote_url()   const;
    virtual std::string get_remote_host()  const;
    virtual std::string get_remote_port()  const;

    State        get_state() const;
    int          get_errno() const;
    int          get_fd()    const;
    
    virtual void connect() = 0;
    virtual void close()   = 0;
    virtual void close(const UUID& uuid)
    {        
        gcomm_throw_runtime(ENOTSUP) << "close(UUID) not supported by "
                                      << uri.get_scheme().c_str();
    }

    virtual void       listen();
    virtual Transport* accept();
    
    virtual int  handle_down(WriteBuf*, const ProtoDownMeta*) = 0;
    virtual void handle_up  (int, const ReadBuf*, const size_t,
                             const ProtoUpMeta*) = 0;
    
    virtual int send(WriteBuf*, const ProtoDownMeta*);
    virtual const ReadBuf* recv();

    static Transport* create(const URI&, EventLoop*);
    static Transport* create(const std::string&, EventLoop*);
};

END_GCOMM_NAMESPACE


#endif // _GCOMM_TRANSPORT_HPP_
