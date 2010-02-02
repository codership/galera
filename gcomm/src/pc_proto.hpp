/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_PC_PROTO_HPP
#define GCOMM_PC_PROTO_HPP

#include <list>

#include "gcomm/uuid.hpp"
#include "gcomm/protolay.hpp"
#include "pc_message.hpp"


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
    
    
    Proto(const UUID& uuid)
        :
        my_uuid_       (uuid),
        start_prim_    (),
        state_         (S_CLOSED),
        last_sent_seq_ (0),
        instances_     (),
        self_i_        (),
        state_msgs_    (),
        current_view_  (V_TRANS),
        pc_view_       (V_NON_PRIM),
        views_         ()
    {
        self_i_ = instances_.insert_unique(std::make_pair(get_uuid(), Node()));
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
    
    void handle_msg  (const Message&, const gu::net::Datagram&,
                      const ProtoUpMeta&);
    void handle_up   (int, const gu::net::Datagram&,
                      const ProtoUpMeta&);
    int  handle_down (gu::net::Datagram&, const ProtoDownMeta&);
    
    void connect(bool first) 
    { 
        log_debug << self_id() << " start_prim " << first;
        start_prim_ = first; 
        shift_to(S_JOINING);
    }
    
    void close() { }
    
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
    void handle_user(const Message&, const gu::net::Datagram&,
                     const ProtoUpMeta&);
    void deliver_view();

    UUID   const      my_uuid_;       // Node uuid
    bool              start_prim_;    // Is allowed to start in prim comp
    State             state_;         // State
    uint32_t          last_sent_seq_; // Msg seqno of last sent message
    
    NodeMap           instances_;     // Map of known node instances
    NodeMap::iterator self_i_;        // Iterator pointing to self node instance
    
    SMMap             state_msgs_;    // Map of received state messages
    View              current_view_;  // EVS view
    View              pc_view_;       // PC view
    std::list<View>   views_;         // List of seen views
};


#endif // PC_PROTO_HPP
