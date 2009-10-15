
// EVS backend implementation based on TIPC 

//
// State RECOVERY:
// Input:
//   USER - If message from the current view then queue, update aru 
//          and expected 
//          Output: If from current view, aru changed and flow control
//                  allows send GAP message
//                  Else if source is not known, add to set of known nodes
//
//   GAP  - If INSTALL received update aru and expected
//          Else if GAP message matches to INSTALL message, add source
//               to install_acked
//          Output: If all in install_acked = true,
//
//   JOIN - add to join messages, add source to install acked with false 
//          status, compute consensus
//          Output: 
//          If consensus reached and representative send INSTALL
//          If state was updated, send JOIN
//
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
#include "evs_message2.hpp"

#include "gcomm/conf.hpp"
#include "gcomm/transport.hpp"

using namespace std;
using namespace gcomm;
using namespace gcomm::evs;

/////////////////////////////////////////////////////////////////////////////
// EVS interface
/////////////////////////////////////////////////////////////////////////////

void EVS::handle_up(const int          cid,
                    const ReadBuf*     rb,
                    const size_t       roff, 
                    const ProtoUpMeta* um)
{
    Critical crit(mon);

    if (um->get_view() != 0 && um->get_view()->get_type() == View::V_REG)
    {
        /* Call close gmcast transport for all nodes that left 
         * gracefully */
        for (NodeList::const_iterator i = um->get_view()->get_left().begin();
             i != um->get_view()->get_left().end(); ++i)
        {
            tp->close(NodeList::get_key(i));
        }
    }

    pass_up(rb, roff, um);
}

int EVS::handle_down(WriteBuf* wb, const ProtoDownMeta* dm)
{
    Critical crit(mon);
    return proto->handle_down(wb, dm);
}


void EVS::connect()
{    
    Critical crit(mon);

    URI tp_uri = uri;

    tp_uri._set_scheme(Conf::GMCastScheme);
    tp = Transport::create(tp_uri, event_loop);

    if (tp->supports_uuid() == false)
    {
        gcomm_throw_fatal << "Transport " << tp_uri.get_scheme()
                          <<" does not support UUID";
    }
    
    tp->connect();

    UUID uuid = tp->get_uuid();

    if (uuid == UUID())
    {
        gcomm_throw_fatal << "invalid UUID: " << uuid.to_string();
    }

    string name;

    try
    {
        name = uri.get_option (Conf::NodeQueryName);
    }
    catch (gu::NotFound&)
    {
        name = uuid.to_string();
    }

    proto = new Proto(event_loop, tp, uuid, mon);

    gcomm::connect(tp, proto);
    gcomm::connect(proto, this);
    proto->shift_to(Proto::S_JOINING);
    Time stop(Time::now() + Time(5, 0));
    do 
    {
        /* Send join messages without handling them */
        proto->send_join(false);
        int ret = event_loop->poll(500);
        log_debug << "poll returned " << ret;
    } 
    while (stop >= Time::now() && proto->get_known_size() == 1);
    log_info << "EVS Proto initial state: " << *proto;
    log_info << "EVS Proto sending join request";
    proto->send_join();
    do
    {
        int ret = event_loop->poll(50);
        if (ret < 0)
        {
            log_warn << "poll(): " << ret;
        }
    }
    while (proto->get_state() != Proto::S_OPERATIONAL);
}


void EVS::close()
{
    Critical crit(mon);

    log_info << "EVS Proto leaving";
    proto->shift_to(Proto::S_LEAVING);
    log_info << "EVS Proto sending leave notification";
    proto->send_leave();
    do
    {
        int ret = event_loop->poll(500);
        log_debug << "poll returned " << ret;
    } 
    while (proto->get_state() != Proto::S_CLOSED);
    
    int cnt = 0;
    do
    {
        event_loop->poll(50);
    } 
    while (cnt++ < 10);
    
    tp->close();
    delete tp;
    tp = 0;
    delete proto;
    proto = 0;
}

const UUID& EVS::get_uuid() const
{
    return proto->get_uuid();
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
        UserMessage evsm;
        
        if (tp->get_max_msg_size() < evsm.serial_size())
        {
            gcomm_throw_fatal << "transport max msg size too small: "
                              << tp->get_max_msg_size();
        }
        
        return tp->get_max_msg_size() - evsm.serial_size();
    }
}


EVS::EVS (const URI& uri_,
          EventLoop* event_loop_,
          Monitor*   mon_)
    :
    Transport (uri_, event_loop_, mon_),
    tp        (0),
    proto     (0)
{
    if (uri.get_scheme() != Conf::EvsScheme)
    {
        gcomm_throw_runtime (EINVAL) << "Invalid uri: " + uri.to_string();
    }
}

EVS::~EVS()
{
    if (tp)
    {
        close();
    }
}


