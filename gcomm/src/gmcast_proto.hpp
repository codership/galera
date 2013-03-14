/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "gu_datetime.hpp"
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
        std::ostream& operator<<(std::ostream& os, const Proto& p);
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
    State state() const
    {
        return state_;
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



    Proto (int v,
           SocketPtr          tp,
           const std::string& local_addr,
           const std::string& remote_addr,
           const std::string& mcast_addr,
           const gcomm::UUID& local_uuid,
           uint8_t            local_segment,
           const std::string& group_name)
        :
        version_(v),
        handshake_uuid_   (),
        local_uuid_       (local_uuid),
        remote_uuid_      (),
        local_segment_    (local_segment),
        remote_segment_   (0),
        local_addr_       (local_addr),
        remote_addr_      (remote_addr),
        mcast_addr_       (mcast_addr),
        group_name_       (group_name),
        changed_          (false),
        state_            (S_INIT),
        propagate_remote_ (false),
        tp_               (tp),
        link_map_         (),
        tstamp_           (gu::datetime::Date::now())
    { }

    ~Proto() { tp_->close(); }

    void send_msg(const Message& msg);
    void send_handshake();
    void wait_handshake();
    void handle_handshake(const Message& hs);
    void handle_handshake_response(const Message& hs);
    void handle_ok(const Message& hs);
    void handle_failed(const Message& hs);
    void handle_topology_change(const Message& msg);
    void handle_keepalive(const Message& msg);
    void send_topology_change(LinkMap& um);
    void handle_message(const Message& msg);
    void send_keepalive();

    const gcomm::UUID& handshake_uuid() const { return handshake_uuid_; }
    const gcomm::UUID& local_uuid() const { return local_uuid_; }
    const gcomm::UUID& remote_uuid() const { return remote_uuid_; }
    uint8_t remote_segment() const { return remote_segment_; }

    SocketPtr socket() const { return tp_; }

    const std::string& remote_addr() const { return remote_addr_; }
    const std::string& mcast_addr() const { return mcast_addr_; }
    const LinkMap& link_map() const { return link_map_; }

    bool changed()
    {
        bool ret = changed_;
        changed_ = false;
        return ret;
    }
    int version() const { return version_; }
    void set_tstamp(gu::datetime::Date ts) { tstamp_ = ts; }
    gu::datetime::Date tstamp() const { return tstamp_; }
private:
    friend std::ostream& operator<<(std::ostream&, const Proto&);
    Proto(const Proto&);
    void operator=(const Proto&);

    int version_;
    gcomm::UUID       handshake_uuid_;
    gcomm::UUID       local_uuid_;  // @todo: do we need it here?
    gcomm::UUID       remote_uuid_;
    uint8_t           local_segment_;
    uint8_t           remote_segment_;
    std::string       local_addr_;
    std::string       remote_addr_;
    std::string       mcast_addr_;
    std::string       group_name_;
    bool              changed_;
    State             state_;
    bool              propagate_remote_;
    SocketPtr         tp_;
    LinkMap           link_map_;
    gu::datetime::Date tstamp_;
};


inline std::ostream& gcomm::gmcast::operator<<(std::ostream& os, const Proto& p)
{
    os << "v="  << p.version_ << ","
       << "lu=" << p.local_uuid_ << ","
       << "ru=" << p.remote_uuid_ << ","
       << "ls=" << static_cast<int>(p.local_segment_) << ","
       << "rs=" << static_cast<int>(p.remote_segment_) << ","
       << "la=" << p.local_addr_ << ","
       << "ra=" << p.remote_addr_ << ","
       << "mc=" << p.mcast_addr_ << ","
       << "gn=" << p.group_name_ << ","
       << "ch=" << p.changed_ << ","
       << "st=" << gcomm::gmcast::Proto::to_string(p.state_) << ","
       << "pr=" << p.propagate_remote_ << ","
       << "tp=" << p.tp_ << ","
       << "ts=" << p.tstamp_;
    return os;
}

class gcomm::gmcast::ProtoMap : public Map<const SocketId, Proto*> { };
