/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_PC_PROTO_HPP
#define GCOMM_PC_PROTO_HPP

#include <list>
#include <ostream>

#include "gcomm/uuid.hpp"
#include "gcomm/protolay.hpp"
#include "gcomm/conf.hpp"
#include "pc_message.hpp"
#include "defaults.hpp"

#include "gu_uri.hpp"

#ifndef GCOMM_PC_MAX_VERSION
#define GCOMM_PC_MAX_VERSION 0
#endif // GCOMM_PC_MAX_VERSION

namespace gcomm
{
    namespace pc
    {
        class Proto;
        std::ostream& operator<<(std::ostream& os, const Proto& p);
    }
}


class gcomm::pc::Proto : public Protolay
{
public:

    enum State
    {
        S_CLOSED,
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
        case S_STATES_EXCH: return "STATES_EXCH";
        case S_INSTALL:     return "INSTALL";
        case S_TRANS:       return "TRANS";
        case S_PRIM:        return "PRIM";
        case S_NON_PRIM:    return "NON_PRIM";
        default:
            gu_throw_fatal << "Invalid state";
        }
    }


    Proto(gu::Config&    conf,
          const UUID&    uuid,
          SegmentId      segment,
          const gu::URI& uri = gu::URI("pc://"))
        :
        Protolay(conf),
        version_(
            check_range(Conf::PcVersion,
                        param<int>(conf, uri, Conf::PcVersion,
                                   Defaults::PcVersion),
                        0, max_version_ + 1)),
        my_uuid_       (uuid),
        start_prim_    (),
        npvo_          (param<bool>(conf, uri, Conf::PcNpvo, Defaults::PcNpvo)),
        ignore_quorum_ (param<bool>(conf, uri, Conf::PcIgnoreQuorum,
                                    Defaults::PcIgnoreQuorum)),
        ignore_sb_     (param<bool>(conf, uri, Conf::PcIgnoreSb,
                                    gu::to_string(ignore_quorum_))),
        closing_       (false),
        state_         (S_CLOSED),
        last_sent_seq_ (0),
        checksum_      (param<bool>(conf, uri, Conf::PcChecksum,
                                    Defaults::PcChecksum)),
        instances_     (),
        self_i_        (instances_.insert_unique(std::make_pair(uuid, Node()))),
        state_msgs_    (),
        current_view_  (V_NONE),
        pc_view_       (V_NON_PRIM),
        views_         (),
        mtu_           (std::numeric_limits<int32_t>::max()),
        weight_        (check_range(Conf::PcWeight,
                                    param<int>(conf, uri, Conf::PcWeight,
                                               Defaults::PcWeight),
                                    0, 0xff))
    {
        log_info << "PC version " << version_;
        set_weight(weight_);
        NodeMap::value(self_i_).set_segment(segment);

        conf.set(Conf::PcVersion,      gu::to_string(version_));
        conf.set(Conf::PcNpvo,         gu::to_string(npvo_));
        conf.set(Conf::PcIgnoreQuorum, gu::to_string(ignore_quorum_));
        conf.set(Conf::PcIgnoreSb,     gu::to_string(ignore_sb_));
        conf.set(Conf::PcChecksum,     gu::to_string(checksum_));
        conf.set(Conf::PcWeight,       gu::to_string(weight_));
    }

    ~Proto() { }

    const UUID& uuid() const { return my_uuid_; }

    bool prim() const { return NodeMap::value(self_i_).prim(); }

    void set_prim(const bool val) { NodeMap::value(self_i_).set_prim(val); }

    void mark_non_prim();


    const ViewId& last_prim() const
    { return NodeMap::value(self_i_).last_prim(); }

    void set_last_prim(const ViewId& vid)
    {
        gcomm_assert(vid.type() == V_PRIM);
        NodeMap::value(self_i_).set_last_prim(vid);
    }

    uint32_t last_seq() const
    { return NodeMap::value(self_i_).last_seq(); }

    void set_last_seq(const uint32_t seq)
    { NodeMap::value(self_i_).set_last_seq(seq); }

    int64_t to_seq() const
    { return NodeMap::value(self_i_).to_seq(); }

    void set_to_seq(const int64_t seq)
    { NodeMap::value(self_i_).set_to_seq(seq); }

    void set_weight(int weight)
    { NodeMap::value(self_i_).set_weight(weight); }


    class SMMap : public Map<const UUID, Message> { };

    const View& current_view() const { return current_view_; }

    const UUID& self_id() const { return my_uuid_; }

    State       state()   const { return state_; }

    void shift_to    (State);
    void send_state  ();
    void send_install(bool bootstrap, int weight = -1);

    void handle_first_trans (const View&);
    void handle_trans       (const View&);
    void handle_reg         (const View&);

    void handle_msg  (const Message&, const Datagram&,
                      const ProtoUpMeta&);
    void handle_up   (const void*, const Datagram&,
                      const ProtoUpMeta&);
    int  handle_down (Datagram&, const ProtoDownMeta&);

    void connect(bool first)
    {
        log_debug << self_id() << " start_prim " << first;
        start_prim_ = first;
        closing_    = false;
        shift_to(S_NON_PRIM);
    }

    void close(bool force = false) { closing_ = true; }

    void handle_view (const View&);

    bool set_param(const std::string& key, const std::string& val);
    void set_mtu(size_t mtu) { mtu_ = mtu; }
    size_t mtu() const { return mtu_; }
private:
    friend std::ostream& operator<<(std::ostream& os, const Proto& p);
    Proto (const Proto&);
    Proto& operator=(const Proto&);

    bool requires_rtr() const;
    bool is_prim() const;
    bool have_quorum(const View&, const View&) const;
    bool have_split_brain(const View&) const;
    void validate_state_msgs() const;
    void cleanup_instances();
    void handle_state(const Message&, const UUID&);
    void handle_install(const Message&, const UUID&);
    void handle_trans_install(const Message&, const UUID&);
    void handle_user(const Message&, const Datagram&,
                     const ProtoUpMeta&);
    void deliver_view(bool bootstrap = false);

    int               version_;
    static const int  max_version_ = GCOMM_PC_MAX_VERSION;
    UUID   const      my_uuid_;       // Node uuid
    bool              start_prim_;    // Is allowed to start in prim comp
    bool              npvo_;          // Newer prim view overrides
    bool              ignore_quorum_; // Ignore lack of quorum
    bool              ignore_sb_;     // Ignore split-brain condition
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
    size_t            mtu_;           // Maximum transmission unit
    int               weight_;        // Node weight in voting
};


#endif // PC_PROTO_HPP
