
#include "pc.hpp"

#include "pc_proto.hpp"
#include "evs_proto.hpp"

#include "gcomm/conf.hpp"
#include "gcomm/time.hpp"

using namespace std;
using namespace gcomm;


void PC::handle_up(const int cid, const ReadBuf* rb, const size_t roff, 
                   const ProtoUpMeta* um)
{
    Critical crit(mon);

    if (um->get_view() != 0 && um->get_view()->get_type() == View::V_PRIM)
    {
        /* Call close gmcast transport for all nodes that have left 
         * or partitioned */
        for (NodeList::const_iterator i = um->get_view()->get_left().begin();
             i != um->get_view()->get_left().end(); ++i)
        {
            tp->close(NodeList::get_key(i));
        }

        for (NodeList::const_iterator i =
                 um->get_view()->get_partitioned().begin();
             i != um->get_view()->get_partitioned().end(); ++i)
        {
            tp->close(NodeList::get_key(i));
        }
    }

    pass_up(rb, roff, um);
}

int PC::handle_down(WriteBuf* wb, const ProtoDownMeta* dm)
{
    Critical crit(mon);

    return pass_down(wb, dm);
}

size_t PC::get_max_msg_size() const
{
    // TODO: 
    if (tp == 0) gcomm_throw_fatal << "not open";

    evs::UserMessage evsm;
    PCUserMessage  pcm(0);

    if (tp->get_max_msg_size() < evsm.serial_size() + pcm.serial_size())
    {
        gcomm_throw_fatal << "transport max msg size too small: "
                          << tp->get_max_msg_size();
    }

    return tp->get_max_msg_size() - evsm.serial_size() - pcm.serial_size();
}

bool PC::supports_uuid() const
{
    if (tp->supports_uuid() == false)
    {
        // alex: what is the meaning of this ?
        gcomm_throw_fatal << "transport does not support UUID";
    }
    return true;
}

const UUID& PC::get_uuid() const
{
    return tp->get_uuid();
}

void PC::connect()
{
    Critical crit(mon);

    URI tp_uri = uri;

    tp_uri._set_scheme(Conf::GMCastScheme); // why do we need this?

    tp = Transport::create(tp_uri, event_loop);

    if (tp->supports_uuid() == false)
    {
        gcomm_throw_fatal << "Transport " << tp_uri.get_scheme()
                          << " does not support UUID";
    }
    
    gu_trace (tp->connect());

    UUID uuid = tp->get_uuid();

    if (uuid == UUID::nil())
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

    evs = new evs::Proto(event_loop, tp, uuid, mon);

    gcomm::connect (tp, evs);

    const bool start_prim = host_is_any (uri.get_host());

    evs->shift_to(evs::Proto::S_JOINING);

    do
    {
        /* Send join messages without handling them */
        evs->send_join(false);

        int ret;
        gu_trace(ret = event_loop->poll(500));

        log_debug << "poll returned: " << ret;
    }
    while (start_prim == false && evs->get_known_size() == 1);

    log_info << "PC/EVS Proto initial state: " << *evs;
    
    pc = new PCProto (uuid, event_loop, mon, start_prim);

    gcomm::connect (evs, pc);
    gcomm::connect (pc, this);
    
    pc->shift_to(PCProto::S_JOINING);

    log_info << "PC/EVS Proto sending join request";

    evs->send_join();

    do
    {
        int ret;

        gu_trace(ret = event_loop->poll(50));

        if (ret < 0)
        {
            log_warn << "poll(): " << ret;
        }
    }
    while (pc->get_state() != PCProto::S_PRIM);
}

void PC::close()
{
    Critical crit(mon);

    log_info << "PC/EVS Proto leaving";
    evs->shift_to(evs::Proto::S_LEAVING);
    evs->send_leave();

    do
    {
        int ret = event_loop->poll(500);

        log_debug << "poll returned " << ret;
    } 
    while (evs->get_state() != evs::Proto::S_CLOSED);

    if (pc->get_state() != PCProto::S_CLOSED)
    {
        log_warn << "PCProto didn't reach closed state";
    }

    int cnt = 0;
    do
    {
        event_loop->poll(10);
    }
    while (++cnt < 15); // what is that number?

    tp->close();

    delete tp;
    tp = 0;

    delete evs;
    evs = 0;

    delete pc;
    pc = 0;
}


PC::PC(const URI& uri_, EventLoop* el_, Monitor* mon_) :
    Transport(uri_, el_, mon_),
    tp(0),
    evs(0),
    pc(0)
{
    if (uri.get_scheme() != Conf::PcScheme)
    {
        log_fatal << "invalid uri: " << uri.to_string();
    }
}

PC::~PC()
{
    if (tp)
    {
        close();
    }
}


