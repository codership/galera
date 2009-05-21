
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

#include "gcomm/conf.hpp"
#include "gcomm/transport.hpp"

BEGIN_GCOMM_NAMESPACE

/////////////////////////////////////////////////////////////////////////////
// EVS interface
/////////////////////////////////////////////////////////////////////////////

void EVS::handle_up(const int cid, const ReadBuf* rb, const size_t roff, 
                    const ProtoUpMeta* um)
{
    Critical crit(mon);
    pass_up(rb, roff, um);
}

int EVS::handle_down(WriteBuf* wb, const ProtoDownMeta* dm)
{
    Critical crit(mon);
    return proto->handle_down(wb, dm);
}


void EVS::connect()
{
    
    URI tp_uri = uri;
    tp_uri.set_scheme(Conf::GMCastScheme);
    tp = Transport::create(tp_uri, event_loop);
    if (tp->supports_uuid() == false)
    {
        LOG_FATAL("Transport " + tp_uri.get_scheme() + " does not support UUID");
        throw FatalException("UUID not supported by transport");
    }
    
    tp->connect();
    UUID uuid = tp->get_uuid();
    if (uuid == UUID())
    {
        LOG_FATAL("invalid UUID: " + uuid.to_string());
        throw FatalException("invalid UUID");
    }
    string name;
    URIQueryList::const_iterator i = 
        uri.get_query_list().find(Conf::NodeQueryName);
    if (i == uri.get_query_list().end())
    {
        name = uuid.to_string();
    }
    else
    {
        name = get_query_value(i);
    }
    proto = new EVSProto(event_loop, tp, uuid, name, mon);
    tp->set_up_context(proto);
    proto->set_down_context(tp);
    proto->set_up_context(this);
    this->set_down_context(proto);
    proto->shift_to(EVSProto::JOINING);
    Time stop(Time::now() + Time(5, 0));
    do 
    {
        /* Send join messages without handling them */
        proto->send_join(false);
        int ret = event_loop->poll(500);
        LOG_DEBUG(string("poll returned ") + Int(ret).to_string());
    } 
    while (stop >= Time::now() && proto->known.size() == 1);
    LOG_INFO("EVS Proto initial state: " + proto->to_string());
    LOG_INFO("EVS Proto sending join request");
    proto->send_join();
}


void EVS::close()
{
    LOG_INFO("EVS Proto leaving");
    proto->shift_to(EVSProto::LEAVING);
    LOG_INFO("EVS Proto sending leave notification");
    proto->send_leave();
    do
    {
        int ret = event_loop->poll(500);
        LOG_DEBUG(string("poll returned ") + Int(ret).to_string());
    } while (proto->get_state() == EVSProto::LEAVING);

    int cnt = 0;
    do
    {
        event_loop->poll(50);
    } while (cnt++ < 50);
    
    tp->close();
    delete tp;
    tp = 0;
    delete proto;
    proto = 0;
}

const UUID& EVS::get_uuid() const
{
    return proto->my_addr;
}

bool EVS::supports_uuid() const
{
    return true;
}

size_t EVS::get_max_msg_size() const
{
    if (tp == 0)
    {
        return 1024;
    }
    else
    {
        EVSUserMessage evsm(UUID(0, 0), 0xff, SAFE, 0, 0, 0, ViewId(UUID(), 0), 0);
        if (tp->get_max_msg_size() < evsm.size())
        {
            LOG_FATAL("transport max msg size too small: " +
                      Size(tp->get_max_msg_size()).to_string());
            throw FatalException("");
        }
        return tp->get_max_msg_size() - evsm.size();
    }
}


EVS::EVS(const URI& uri_, EventLoop* event_loop_, Monitor* mon_) :
    Transport(uri_, event_loop_, mon_),
    tp(0),
    proto(0)
{
    
    if (uri.get_scheme() != Conf::EvsScheme)
    {
        LOG_FATAL("invalid uri: " + uri.to_string());
        throw FatalException("invalid uri");
    }


}

EVS::~EVS()
{
    if (tp)
    {
        close();
    }
}

END_GCOMM_NAMESPACE
