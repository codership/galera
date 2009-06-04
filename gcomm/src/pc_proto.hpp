#ifndef PC_PROTO_HPP
#define PC_PROTO_HPP

#include "pc_message.hpp"

#include "gcomm/uuid.hpp"
#include "gcomm/event.hpp"

BEGIN_GCOMM_NAMESPACE

class PCProto : public Protolay
{
public:
    enum State {
        S_CLOSED, 
        S_JOINING, 
        S_STATES_EXCH, 
        S_RTR, 
        S_PRIM,
        S_NON_PRIM
    };
    
private:
    UUID uuid;
    EventLoop* el;
    Monitor* mon;
    bool start_prim;
    State state;
    
    uint16_t last_sent;
    
    
    PCInstMap instances;

public:
    PCProto(const UUID& uuid_, 
            EventLoop* el_, 
            Monitor* mon_, 
            const bool start_prim_) :
        uuid(uuid),
        el(el_),
        mon(mon_),
        start_prim(start_prim),
        state(S_CLOSED)
    {
    }
    
    ~PCProto() 
    {
    }
    
    void shift_to(State);
    
    State get_state() const
    {
        return state;
    }

    void handle_up(const int, const ReadBuf*, const size_t, const ProtoUpMeta*);
    int handle_down(WriteBuf*, const ProtoDownMeta*);

    void handle_view(const View&);
    
    
};

END_GCOMM_NAMESPACE

#endif // PC_PROTO_HPP
