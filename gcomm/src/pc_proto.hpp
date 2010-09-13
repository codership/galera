/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_PC_PROTO_HPP
#define GCOMM_PC_PROTO_HPP

#include <list>

#include "gcomm/uuid.hpp"
#include "gcomm/protolay.hpp"
#include "gcomm/conf.hpp"
#include "pc_message.hpp"

#include "gu_uri.hpp"

#ifndef GCOMM_PC_MAX_VERSION
#define GCOMM_PC_MAX_VERSION 0
#endif // GCOMM_PC_MAX_VERSION

namespace gcomm
{
    namespace pc
    {
        class Proto;
    }
}


class gcomm::pc::Proto : public Protolay
{
public:

    enum State
    {
        S_CLOSED,
        S_JOINING,
        S_STATES_EXCH,
        S_INSTALL,
        S_PRIM,
        S_TRANS,
        S_NON_PRIM,
        S_MAX
    };

    static std::string to_string(const State s)
    {
        switch (s)
        {
        case S_CLOSED:      return "CLOSED";
        case S_JOINING:     return "JOINING";
        case S_STATES_EXCH: return "STATES_EXCH";
        case S_INSTALL:     return "INSTALL";
        case S_TRANS:       return "TRANS";
        case S_PRIM:        return "PRIM";
        case S_NON_PRIM:    return "NON_PRIM";
        default:
            gu_throw_fatal << "Invalid state"; throw;
        }
    }


    Proto(const UUID& uuid, const gu::URI& uri = gu::URI("pc://"))
        :
        version_(gu::from_string<int>(uri.get_option(Conf::PcVersion, "0"))),
        my_uuid_       (uuid),
        start_prim_    (),
        npvo_          (gu::from_string<bool>(uri.get_option(Conf::PcNpvo, "false"))),
        allow_sb_      (gu::from_string<bool>(uri.get_option(Conf::PcAllowSb, "true"))),
                        closing_       (false),
        state_         (S_CLOSED),
        last_sent_seq_ (0),
        checksum_      (gu::from_string<bool>(uri.get_option(Conf::PcChecksum, "true"))),
        instances_     (),
        self_i_        (instances_.insert_unique(std::make_pair(uuid, Node()))),
        state_msgs_    (),
        current_view_  (V_TRANS),
        pc_view_       (V_NON_PRIM),
        views_         ()
    {
        if (version_ > max_version_)
        {
            gu_throw_error(EINVAL) << "invalid pc version " << version_;
        }
        log_info << "PC version " << version_;
    }

    ~Proto() { }

    const UUID& get_uuid() const { return my_uuid_; }

    bool get_prim() const { return NodeMap::get_value(self_i_).get_prim(); }

    void set_prim(const bool val) { NodeMap::get_value(self_i_).set_prim(val); }

    const ViewId& get_last_prim() const
    { return NodeMap::get_value(self_i_).get_last_prim(); }

    void set_last_prim(const ViewId& vid)
    {
        gcomm_assert(vid.get_type() == V_PRIM);
        NodeMap::get_value(self_i_).set_last_prim(vid);
    }

    uint32_t get_last_seq() const
    { return NodeMap::get_value(self_i_).get_last_seq(); }

    void set_last_seq(const uint32_t seq)
    { NodeMap::get_value(self_i_).set_last_seq(seq); }

    int64_t get_to_seq() const
    { return NodeMap::get_value(self_i_).get_to_seq(); }

    void set_to_seq(const int64_t seq)
    { NodeMap::get_value(self_i_).set_to_seq(seq); }

    class SMMap : public Map<const UUID, Message> { };

    const View& get_current_view() const { return current_view_; }

    const UUID& self_id() const { return my_uuid_; }

    State       get_state()   const { return state_; }

    void shift_to    (State);
    void send_state  ();
    void send_install();

    void handle_first_trans (const View&);
    void handle_first_reg   (const View&);
    void handle_trans       (const View&);
    void handle_reg         (const View&);

    void handle_msg  (const Message&, const gu::Datagram&,
                      const ProtoUpMeta&);
    void handle_up   (const void*, const gu::Datagram&,
                      const ProtoUpMeta&);
    int  handle_down (gu::Datagram&, const ProtoDownMeta&);

    void connect(bool first)
    {
        log_debug << self_id() << " start_prim " << first;
        start_prim_ = first;
        closing_    = false;
        shift_to(S_JOINING);
    }

    void close() { closing_ = true; }

    void handle_view (const View&);
private:

    Proto (const Proto&);
    Proto& operator=(const Proto&);

    bool requires_rtr() const;
    bool is_prim() const;
    void validate_state_msgs() const;
    void cleanup_instances();
    void handle_state(const Message&, const UUID&);
    void handle_install(const Message&, const UUID&);
    void handle_user(const Message&, const gu::Datagram&,
                     const ProtoUpMeta&);
    void deliver_view();

    int               version_;
    static const int  max_version_ = GCOMM_PC_MAX_VERSION;
    UUID   const      my_uuid_;       // Node uuid
    bool              start_prim_;    // Is allowed to start in prim comp
    bool              npvo_;          // Newer prim view overrides
    bool              allow_sb_;      // Split-brain condition is allowed
    bool              closing_;       // Protocol is in closing stage
    State             state_;         // State
    uint32_t          last_sent_seq_; // Msg seqno of last sent message
    bool              checksum_;      // Enable message checksumming
    NodeMap           instances_;     // Map of known node instances
    NodeMap::iterator self_i_;        // Iterator pointing to self node instance

    SMMap             state_msgs_;    // Map of received state messages
    View              current_view_;  // EVS view
    View              pc_view_;       // PC view
    std::list<View>   views_;         // List of seen views
};


#endif // PC_PROTO_HPP
