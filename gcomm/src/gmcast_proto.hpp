/*
 * Copyright (C) 2009-2019 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_GMCAST_PROTO_HPP
#define GCOMM_GMCAST_PROTO_HPP

#include "gu_datetime.hpp"
#include "gcomm/uuid.hpp"
#include "gcomm/util.hpp"
#include "socket.hpp"
#include "gmcast_message.hpp"
#include "gmcast_link.hpp"

namespace gcomm
{
    class GMCast;
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
    /*
     *                         | ----- connect ------> |
     * HANDSHAKE_WAIT          |                       | ---
     *                         |                       |    | accept()
     *                         |                       | <--
     *                         |                       | HANDSHAKE_SENT
     *                         | <---- handshake ----- |
     * HANDSHAKE_RESPONSE_SENT |                       |
     *                         | -- handshake resp --> |
     *                         |                       | OK
     *                         | <------- ok --------- |
     *                      OK |                       |
     */
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



    Proto (GMCast&            gmcast,
           int                version,
           SocketPtr          tp,
           const std::string& local_addr,
           const std::string& remote_addr,
           const std::string& mcast_addr,
           uint8_t            local_segment,
           const std::string& group_name)
        :
        version_          (version),
        handshake_uuid_   (),
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
        send_tstamp_      (gu::datetime::Date::monotonic()),
        recv_tstamp_      (gu::datetime::Date::monotonic()),
        gmcast_           (gmcast)
    { }

    ~Proto() { tp_->close(); }

    void send_msg(const Message& msg, bool ignore_no_buffer_space);
    void send_handshake();
    void wait_handshake();
    /*
     * Validate handshake UUID.
     *
     * Validate UUID of the remote endpoint.
     *
     * @return False if UUID is found to be duplicate
     *         of existing UUIDs and the remote endpoint cannot
     *         be associated with any of the existing connections,
     *         otherwise true is returned.
     * @throw  Throws gu::Exception with ENOTRECOVERABLE errno if
     *         the node should abort due to duplicate UUID.
     */
    bool validate_handshake_uuid();
    void handle_handshake(const Message& hs);
    void handle_handshake_response(const Message& hs);
    void handle_ok(const Message& hs);
    void handle_failed(const Message& hs);
    void handle_topology_change(const Message& msg);
    void handle_keepalive(const Message& msg);
    void send_topology_change(LinkMap& um);
    void handle_message(const Message& msg);
    void send_keepalive();
    void evict();
    /**
     * Send FAIL message to other endpoint with duplicate UUID
     * error status.
     */
    void evict_duplicate_uuid();
    const gcomm::UUID& handshake_uuid() const { return handshake_uuid_; }
    const gcomm::UUID& local_uuid() const;
    const gcomm::UUID& remote_uuid() const { return remote_uuid_; }
    uint8_t remote_segment() const { return remote_segment_; }

    SocketPtr socket() const { return tp_; }

    const std::string& remote_addr() const { return remote_addr_; }
    const std::string& mcast_addr() const { return mcast_addr_; }
    const LinkMap& link_map() const { return link_map_; }

    /**
     * Check if the internal state of the proto entry was changed
     * after the last call and reset the changed state to false.
     *
     * @return True if the state was changed after the last call,
     *         otherwise false.
     */
    bool check_changed_and_reset()
    {
        bool ret = changed_;
        changed_ = false;
        return ret;
    }
    int version() const { return version_; }
    void set_recv_tstamp(gu::datetime::Date ts) { recv_tstamp_ = ts; }
    gu::datetime::Date recv_tstamp() const { return recv_tstamp_; }
    void set_send_tstamp(gu::datetime::Date ts) { send_tstamp_ = ts; }
    gu::datetime::Date send_tstamp() const { return send_tstamp_; }
private:
    friend std::ostream& operator<<(std::ostream&, const Proto&);
    Proto(const Proto&);
    void operator=(const Proto&);

    int version_;
    gcomm::UUID       handshake_uuid_;
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
    gu::datetime::Date send_tstamp_;
    gu::datetime::Date recv_tstamp_;
    GMCast&     gmcast_;
};

class gcomm::gmcast::ProtoMap : public Map<const SocketId, Proto*> { };

#endif // !GCOMM_GMCAST_PROTO_HPP
