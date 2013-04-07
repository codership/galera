/*
 * Copyright (C) 2009-2012 Codership Oy <info@codership.com>
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
    virtual ~Transport();

    virtual size_t      mtu()           const = 0;
    virtual const UUID& uuid()          const = 0;
    virtual std::string local_addr()    const;
    virtual std::string remote_addr()   const;

    int                 err_no()        const;

    virtual void connect(bool start_prim)
    {
        gu_throw_fatal << "connect(start_prim) not supported";
    }
    virtual void connect() // if not overloaded, will default to connect(bool)
    {
        connect(false);
    }
    virtual void connect(const gu::URI& uri)
    {
        gu_throw_fatal << "connect(URI) not supported";
    }

    virtual void close(bool force = false) = 0;
    virtual void close(const UUID& uuid)
    {
        gu_throw_error(ENOTSUP) << "close(UUID) not supported by "
                                << uri_.get_scheme();
    }

    virtual void        listen();
    virtual std::string listen_addr() const
    {
        gu_throw_fatal << "not supported";
    }
    virtual Transport*  accept();
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

    virtual int  handle_down(Datagram&, const ProtoDownMeta&) = 0;
    virtual void handle_up  (const void*, const Datagram&, const ProtoUpMeta&) = 0;
    virtual void handle_stable_view(const View& view) { }
    Protostack& pstack() { return pstack_; }
    Protonet&   pnet()   { return pnet_; }

    static Transport* create(Protonet&, const std::string&);
    static Transport* create(Protonet&, const gu::URI&);

protected:
    Transport (Protonet&, const gu::URI&);
    Protostack        pstack_;
    Protonet&         pnet_;
    gu::URI           uri_;
    int               error_no_;

private:
    Transport (const Transport&);
    Transport& operator=(const Transport&);
};



#endif // _GCOMM_TRANSPORT_HPP_
