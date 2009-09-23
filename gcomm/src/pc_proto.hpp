#ifndef PC_PROTO_HPP
#define PC_PROTO_HPP

#include <list>

#include "gcomm/uuid.hpp"
#include "gcomm/event.hpp"
#include "pc_message.hpp"

using std::list;

BEGIN_GCOMM_NAMESPACE

class PCProto : public Protolay
{
public:

    enum State 
    {
        S_CLOSED, 
        S_JOINING, 
        S_STATES_EXCH, 
        S_RTR, 
        S_PRIM,
        S_TRANS,
        S_NON_PRIM,
        S_MAX
    };
    
    static string to_string(const State s)
    {
        switch (s)
        {
        case S_CLOSED:      return "CLOSED";
        case S_JOINING:     return "JOINING";
        case S_STATES_EXCH: return "STATES_EXCH";
        case S_RTR:         return "RTR";
        case S_TRANS:       return "TRANS";
        case S_PRIM:        return "PRIM";
        case S_NON_PRIM:    return "NON_PRIM";
        default:
            gcomm_throw_fatal << "Invalid state"; throw;
        }
    }

private:

    UUID       uuid;
    EventLoop* el;
    Monitor*   mon;
    bool       start_prim;
    State      state;

    PCInstMap           instances;
    PCInstMap::iterator self_i;

    PCProto (const PCProto&);
    PCProto& operator=(const PCProto&);

public:

    bool get_prim() const
    {
        return PCInstMap::get_instance(self_i).get_prim();
    }

    void set_prim(const bool val)
    {
        PCInstMap::get_instance(self_i).set_prim(val);
    }

    const ViewId& get_last_prim() const
    {
        return PCInstMap::get_instance(self_i).get_last_prim();
    }

    void set_last_prim(const ViewId& vid)
    {
        PCInstMap::get_instance(self_i).set_last_prim(vid);
    }
    
    uint32_t get_last_seq() const
    {
        return PCInstMap::get_instance(self_i).get_last_seq();
    }
    
    void set_last_seq(const uint32_t seq)
    {
        PCInstMap::get_instance(self_i).set_last_seq(seq);
    }

    int64_t get_to_seq() const
    {
        return PCInstMap::get_instance(self_i).get_to_seq();
    }
    
    void set_to_seq(const int64_t seq)
    {
        PCInstMap::get_instance(self_i).set_to_seq(seq);
    }

    typedef InstMap<PCMessage> SMMap;

private:

    SMMap      state_msgs;
    View       current_view;
    list<View> views;

public:

    const View& get_current_view() const
    {
        return current_view;
    }

    PCProto(const UUID& uuid_, 
            EventLoop*  el_, 
            Monitor*    mon_, 
            const bool  start_prim_)
        :
        uuid         (uuid_),
        el           (el_),
        mon          (mon_),
        start_prim   (start_prim_),
        state        (S_CLOSED),
        instances    (),
        self_i       (),
        state_msgs   (),
        current_view (),
        views        ()
    {
        std::pair<PCInstMap::iterator, bool> iret;

        if ((iret = instances.insert(
                 std::make_pair(uuid, PCInst()))).second == false)
        {
            gcomm_throw_fatal << "Failed to insert myself into instance map";
        }

        self_i = iret.first;
    }
    
    ~PCProto() {}

    string self_string() const
    {
        return uuid.to_string();
    }
    
    void shift_to(State);
    
    State get_state() const
    {
        return state;
    }

    void send_state();
    void send_install();
    
    void handle_first_trans(const View&);
    void handle_first_reg(const View&);
    void handle_trans(const View&);
    void handle_reg(const View&);

private:
    bool requires_rtr() const;
    bool is_prim() const;
    void validate_state_msgs() const;
    void handle_state(const PCMessage&, const UUID&);
    void handle_install(const PCMessage&, const UUID&);
    void handle_user(const PCMessage&, const ReadBuf*, const size_t, const ProtoUpMeta*);
    void deliver_view();
public:
    void handle_msg(const PCMessage&, const ReadBuf*, const size_t, const ProtoUpMeta*);

    void handle_up(const int, const ReadBuf*, const size_t, const ProtoUpMeta*);
    int handle_down(WriteBuf*, const ProtoDownMeta*);
    
    void handle_view(const View&);
};

END_GCOMM_NAMESPACE

#endif // PC_PROTO_HPP
