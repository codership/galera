/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

/*!
 * @file transport.hpp
 *
 * @brief Transport interface.
 */

#ifndef _GCOMM_TRANSPORT_HPP_
#define _GCOMM_TRANSPORT_HPP_

#include "gcomm/uuid.hpp"
#include "gcomm/protolay.hpp"
#include "gcomm/protostack.hpp"
#include "gcomm/protonet.hpp"

#include "gu_uri.hpp"

namespace gcomm
{
    /*!
     * @class Transport
     *
     * @brief Transport interface
     */
    class Transport;
}

/*!
 * 
 */
class gcomm::Transport : public Protolay
{
public:
    typedef enum {
        S_CLOSED,
        S_CONNECTING,
        S_CONNECTED,
        S_CLOSING,
        S_LISTENING,
        S_FAILED
    } State;
    
    virtual ~Transport();
    
    virtual size_t      get_mtu()          const = 0;
    virtual bool        supports_uuid()    const;
    virtual const UUID& get_uuid()         const;
    virtual std::string get_local_addr()   const;
    virtual std::string get_remote_addr()  const;
    
    virtual State        get_state() const;
    int                  get_errno() const;
    
    virtual void connect() = 0;
    virtual void close()                     = 0;
    virtual void close(const UUID& uuid)
    {        
        gu_throw_error(ENOTSUP) << "close(UUID) not supported by "
                                << uri_.get_scheme();
    }
    
    virtual void       listen();
    virtual Transport* accept();
    virtual void handle_accept(Transport*)
    {
        gu_throw_error(ENOTSUP) << "handle_accept() not supported by" 
                                << uri_.get_scheme();
    }
    virtual void handle_connect()
    {
        gu_throw_error(ENOTSUP) << "handle_connect() not supported by"
                                << uri_.get_scheme();
    }
    
    virtual int  handle_down(gu::Datagram&, const ProtoDownMeta&) = 0;
    virtual void handle_up  (const void*, const gu::Datagram&, const ProtoUpMeta&) = 0;
    virtual void handle_stable_view(const View& view) { }
    Protostack& get_pstack() { return pstack_; }
    Protonet&   get_pnet()   { return pnet_; }
    
    static Transport* create(Protonet&, const std::string&);
    static Transport* create(Protonet&, const gu::URI&);
protected:
    Transport (Protonet&, const gu::URI&);
    void              set_state(State);
    Protostack        pstack_;
    Protonet&         pnet_;
    gu::URI           uri_;

    State             state_;
    int               error_no_;
    
private:
    Transport (const Transport&);
    Transport& operator=(const Transport&);
};



#endif // _GCOMM_TRANSPORT_HPP_
