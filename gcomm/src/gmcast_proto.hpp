/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "gcomm/uuid.hpp"
#include "gcomm/util.hpp"
#include "socket.hpp"
#include "gmcast_message.hpp"
#include "gmcast_link.hpp"

namespace gcomm
{

    namespace gmcast
    {
        class Proto;
        class ProtoMap;
    }
}


class gcomm::gmcast::Proto 
{
public:
    
    enum State 
    {
        S_INIT,
        S_HANDSHAKE_SENT,
        S_HANDSHAKE_WAIT,
        S_HANDSHAKE_RESPONSE_SENT,
        S_OK,
        S_FAILED,
        S_CLOSED
    };

    
public:

    void set_state(State new_state);    
    State get_state() const 
    {
        return state;
    }
    
    static std::string to_string (State s) 
    {
        switch (s)
        {
        case S_INIT:                    return "INIT";
        case S_HANDSHAKE_SENT:          return "HANDSHAKE_SENT";
        case S_HANDSHAKE_WAIT:          return "HANDSHAKE_WAIT";
        case S_HANDSHAKE_RESPONSE_SENT: return "HANDSHAKE_RESPONSE_SENT";
        case S_OK:                      return "OK";
        case S_FAILED:                  return "FAILED";
        case S_CLOSED:                  return "CLOSED";
        default: return "UNKNOWN";
        }
    }
    
    
    
    Proto (SocketPtr          tp_, 
           const std::string& local_addr_, 
           const std::string& remote_addr_, 
           const std::string& mcast_addr_,
           const gcomm::UUID& local_uuid_, 
           const std::string& group_name_)
        : 
        handshake_uuid   (),
        local_uuid       (local_uuid_),
        remote_uuid      (),
        local_addr       (local_addr_),
        remote_addr      (remote_addr_),
        mcast_addr       (mcast_addr_),
        group_name       (group_name_),
        changed          (false),
        state            (S_INIT),
        propagate_remote (false),
        tp               (tp_),
        link_map         ()
    { }
    
    ~Proto() { }
    
    void send_msg(const Message& msg);
    void send_handshake();    
    void wait_handshake();
    void handle_handshake(const Message& hs);
    void handle_handshake_response(const Message& hs);
    void handle_ok(const Message& hs);
    void handle_failed(const Message& hs);
    void handle_topology_change(const Message& msg);
    void send_topology_change(LinkMap& um);
    void handle_message(const Message& msg);
    
    const gcomm::UUID& get_handshake_uuid() const { return handshake_uuid; }
    const gcomm::UUID& get_local_uuid() const { return local_uuid; }
    const gcomm::UUID& get_remote_uuid() const { return remote_uuid; }

    SocketPtr get_socket() const { return tp; }
    
    const std::string& get_remote_addr() const { return remote_addr; }
    const std::string& get_mcast_addr() const { return mcast_addr; }
    const LinkMap& get_link_map() const { return link_map; }
    
    bool get_changed()
    {
        bool ret = changed;
        changed = false;
        return ret;
    }

private:
    
    Proto(const Proto&);
    void operator=(const Proto&);
    
    gcomm::UUID       handshake_uuid;
    gcomm::UUID       local_uuid;  // @todo: do we need it here?
    gcomm::UUID       remote_uuid;
    std::string       local_addr;
    std::string       remote_addr;
    std::string       mcast_addr;
    std::string       group_name;
    bool              changed;
    State             state;
    bool              propagate_remote;
    SocketPtr         tp;
    LinkMap           link_map;
};


class gcomm::gmcast::ProtoMap : public Map<const SocketId, Proto*> { };
