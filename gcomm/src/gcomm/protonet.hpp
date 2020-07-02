//
// Copyright (C) 2009 Codership Oy <info@codership.com>
//

//!
// @file protonet.hpp
//
// This file defines protonet interface used by gcomm.
//


#ifndef GCOMM_PROTONET_HPP
#define GCOMM_PROTONET_HPP

#include "gu_uri.hpp"
#include "gu_datetime.hpp"
#include "protostack.hpp"
#include "gu_config.hpp"

#include "socket.hpp"

#include <vector>
#include <deque>

#ifndef GCOMM_PROTONET_MAX_VERSION
#define GCOMM_PROTONET_MAX_VERSION 0
#endif // GCOMM_PROTONET_MAX_VERSION

namespace gcomm
{
    // Forward declarations
    class Protonet;
}

//!
// Abstract Protonet interface class
//
class gcomm::Protonet
{
public:
    Protonet(gu::Config& conf, const std::string& type, int version)
        :
        protos_ (),
        version_(version),
        conf_   (conf),
        type_   (type)
    { }

    virtual ~Protonet() { }

    //!
    // Insert Protostack to be handled by Protonet
    //
    // @param pstack Pointer to Protostack
    //
    void insert(Protostack* pstack);

    //!
    // Erase Protostack from Protonet to stop dispatching events
    // to Protostack
    //
    // @param pstack Pointer to Protostack
    //
    void erase(Protostack* pstack);

    //!
    // Create new Socket
    //
    // @param uri URI to specify Socket type
    //
    // @return Socket
    //
    virtual gcomm::SocketPtr socket(const gu::URI& uri) = 0;

    //!
    // Create new Acceptor
    //
    // @param uri URI to specify Acceptor type
    //
    // @return Acceptor
    //
    virtual std::shared_ptr<Acceptor> acceptor(const gu::URI& uri) = 0;

    //!
    // Dispatch events until period p has passed or event
    // loop is interrupted.
    //
    // @param p Period to run event_loop(), negative value means forever
    //
    virtual void event_loop(const gu::datetime::Period& p) = 0;

    //!
    // Iterate over Protostacks and handle timers
    //
    // @return Time of next known timer expiration
    //
    gu::datetime::Date handle_timers();

    //!
    // Interrupt event loop
    //
    virtual void interrupt() = 0;

    //!
    // Enter Protonet critical section
    //
    virtual void enter() = 0;

    //!
    // Leave Protonet critical section
    //
    virtual void leave() = 0;

    bool set_param(const std::string& key, const std::string& val,
                  Protolay::sync_param_cb_t& sync_param_cb);

    gu::Config& conf() { return conf_; }

    //!
    // Factory method for creating Protonets
    //
    static Protonet* create(gu::Config& conf);

    const std::string& type() const { return type_; }

    virtual size_t mtu() const = 0;

protected:

    std::deque<Protostack*> protos_;
    int version_;
    static const int max_version_ = GCOMM_PROTONET_MAX_VERSION;
    gu::Config& conf_;
private:
    std::string type_;
};

#endif // GCOMM_PROTONET_HPP
