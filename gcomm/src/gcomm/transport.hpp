#ifndef _GCOMM_TRANSPORT_HPP_
#define _GCOMM_TRANSPORT_HPP_

#include "gu_uri.hpp"

#include "gcomm/common.hpp"
#include "gcomm/uuid.hpp"
#include "gcomm/protolay.hpp"
#include "gcomm/protostack.hpp"

namespace gcomm
{
    class Transport;
}


class gcomm::Transport : public Protolay
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
    
    Protostack        pstack;
    Protonet&         pnet;
    gu::URI           uri;
    State             state;
    int               error_no;
    void              set_state(State);
    
protected:    
    Transport (Protonet&, const gu::URI& uri_);
    
public:
    
    virtual ~Transport();
    
    virtual size_t      get_mtu()          const = 0;
    virtual bool        supports_uuid()    const;
    virtual const UUID& get_uuid()         const;
    virtual std::string get_local_addr()   const;
    virtual std::string get_remote_addr()  const;
    
    virtual State        get_state() const;
    int          get_errno() const;
    virtual int          get_fd()    const;
    
    virtual void connect() = 0;
    virtual void close()   = 0;
    virtual void close(const UUID& uuid)
    {        
        gcomm_throw_runtime(ENOTSUP) << "close(UUID) not supported by "
                                     << uri.get_scheme().c_str();
    }
    
    virtual void       listen();
    virtual Transport* accept();
    
    virtual int  handle_down(const gu::net::Datagram&, const ProtoDownMeta&) = 0;
    virtual void handle_up  (int, const gu::net::Datagram&, const ProtoUpMeta&) = 0;

    Protostack& get_pstack() { return pstack; }
    Protonet& get_pnet() { return pnet; }
    
    static Transport* create(Protonet&, const std::string&);
};



#endif // _GCOMM_TRANSPORT_HPP_
