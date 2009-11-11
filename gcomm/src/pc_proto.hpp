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

class PCProto : public Protolay
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

private:

    UUID   const my_uuid;
    bool         start_prim;
    State        state;

    PCInstMap           instances;
    PCInstMap::iterator self_i;

    PCProto (const PCProto&);
    PCProto& operator=(const PCProto&);

public:

    const UUID& get_uuid() const { return my_uuid; }

    bool get_prim() const
    {
        return PCInstMap::get_value(self_i).get_prim();
    }

    void set_prim(const bool val)
    {
        PCInstMap::get_value(self_i).set_prim(val);
    }

    const ViewId& get_last_prim() const
    {
        return PCInstMap::get_value(self_i).get_last_prim();
    }
    
    void set_last_prim(const ViewId& vid)
    {
        gcomm_assert(vid.get_type() == V_PRIM);
        PCInstMap::get_value(self_i).set_last_prim(vid);
    }
    
    uint32_t get_last_seq() const
    {
        return PCInstMap::get_value(self_i).get_last_seq();
    }
    
    void set_last_seq(const uint32_t seq)
    {
        PCInstMap::get_value(self_i).set_last_seq(seq);
    }
    
    int64_t get_to_seq() const
    {
        return PCInstMap::get_value(self_i).get_to_seq();
    }
    
    void set_to_seq(const int64_t seq)
    {
        PCInstMap::get_value(self_i).set_to_seq(seq);
    }
    
    class SMMap : public Map<const UUID, PCMessage> { };

private:

    SMMap      state_msgs;
    View       current_view; /*! EVS view */
    View       pc_view;      /*! PC view */
    std::list<View> views;

public:

    const View& get_current_view() const
    {
        return current_view;
    }

    PCProto(const UUID& uuid_)
        :
        my_uuid      (uuid_),
        start_prim   (),
        state        (S_CLOSED),
        instances    (),
        self_i       (),
        state_msgs   (),
        current_view (V_TRANS),
        pc_view      (V_NON_PRIM),
        views        ()
    {
        self_i = instances.insert_unique(std::make_pair(get_uuid(), PCInst()));
    }
    
    ~PCProto() {}

    std::string self_string() const { return get_uuid().to_string(); }
    
    State       get_state()   const { return state; }

    void shift_to    (State);
    void send_state  ();
    void send_install();
    
    void handle_first_trans (const View&);
    void handle_first_reg   (const View&);
    void handle_trans       (const View&);
    void handle_reg         (const View&);

private:

    bool requires_rtr() const;
    bool is_prim() const;
    void validate_state_msgs() const;
    void cleanup_instances();
    void handle_state(const PCMessage&, const UUID&);
    void handle_install(const PCMessage&, const UUID&);
    void handle_user(const PCMessage&, const gu::net::Datagram&,
                     const ProtoUpMeta&);
    void deliver_view();

public:

    void handle_msg  (const PCMessage&, const gu::net::Datagram&,
                      const ProtoUpMeta&);
    void handle_up   (int, const gu::net::Datagram&,
                      const ProtoUpMeta&);
    int  handle_down (const gu::net::Datagram&, const ProtoDownMeta&);
    
    void connect(bool first) 
    { 
        log_info << self_string() << " start_prim " << first;
        start_prim = first; 
        shift_to(S_JOINING);
    }
    
    void close() { }
    
    void handle_view (const View&);
};

} // namespace gcomm

#endif // PC_PROTO_HPP
