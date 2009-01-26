
// EVS backend implementation based on TIPC 

//
// State RECOVERY:
// Input:
//   USER - If message from the current view then queue, update aru 
//          and expected 
//          Output: If from current view, aru changed and flow control
//                  allows send GAP message
//                  Else if source is not known, add to set of known nodes
//   GAP  - If INSTALL received update aru and expected
//          Else if GAP message matches to INSTALL message, add source
//               to install_acked
//          Output: If all in install_acked = true, 
//   JOIN - add to join messages, add source to install acked with false 
//          status, compute consensus
//          Output: 
//          If consensus reached and representative send INSTALL
//          If state was updated, send JOIN
//   INSTALL - Output:
//          If message state matches to current send GAP message
//          Else send JOIN message
//
// INSTALL message carries 
// - seqno 0
// - UUID for the new view
// - low_aru and high_aru
// - 
// 
//
//
//

#include "evs.hpp"
#include "evs_proto.hpp"

/////////////////////////////////////////////////////////////////////////////
// EVS interface
/////////////////////////////////////////////////////////////////////////////

void EVS::handle_up(const int cid, const ReadBuf* rb, const size_t roff, 
                    const ProtoUpMeta* um)
{
    EVSMessage msg;
    
    if (rb == 0 && um == 0)
        throw FatalException("EVS::handle_up(): Invalid input rb == 0 && um == 0");
    const TransportNotification* tn = 
        static_cast<const TransportNotification*>(um);
    
    if (rb == 0 && tn) {
    // if (proto->my_addr == ADDRESS_INVALID)
    //   proto->my_addr = tp->get_sockaddr();
        proto->handle_notification(tn);
        return;
    }
    
    if (msg.read(rb->get_buf(), rb->get_len(), roff) == 0) {
        LOG_WARN("EVS::handle_up(): Invalid message");
        return;
    }
    
    EVSPid source(msg.get_source());
    
    switch (msg.get_type()) {
    case EVSMessage::USER:
        proto->handle_user(msg, source, rb, roff);
        break;
    case EVSMessage::DELEGATE:
        proto->handle_delegate(msg, source, rb, roff);
        break;
    case EVSMessage::GAP:
        proto->handle_gap(msg, source);
        break;
    case EVSMessage::JOIN:
        proto->handle_join(msg, source);
        break;
    case EVSMessage::LEAVE:
        proto->handle_leave(msg, source);
        break;
    case EVSMessage::INSTALL:
        proto->handle_install(msg, source);
        break;
    default:
        LOG_WARN(std::string("EVS::handle_up(): Invalid message type: ") 
                 + to_string(msg.get_type()));
    }    
}

int EVS::handle_down(WriteBuf* wb, const ProtoDownMeta* dm)
{
    return proto->handle_down(wb, dm);
}

void EVS::join(const ServiceId sid, Protolay *up)
{
    proto->set_up_context(up);
    proto->shift_to(EVSProto::JOINING);
}

void EVS::leave(const ServiceId sid)
{
    proto->shift_to(EVSProto::LEAVING);
}

void EVS::connect(const char* addr)
{
    tp->connect(addr);
    proto = new EVSProto(tp, ADDRESS_INVALID);
}

void EVS::close()
{
    tp->close();
}


EVS* EVS::create(const char *addr, Poll *p)
{
    if (strncmp(addr, "tipc:", strlen("tipc:")) != 0)
        throw FatalException("EVS: Only TIPC transport is currently supported");
    
    EVS *evs = new EVS();
    evs->tp = Transport::create(addr, p, evs);
    evs->set_down_context(evs->tp);
    return evs;
}
