
#include "pc.hpp"

#include "pc_proto.hpp"
#include "evs_proto.hpp"

#include "gcomm/conf.hpp"
#include "gcomm/time.hpp"

BEGIN_GCOMM_NAMESPACE

void PC::handle_up(const int cid, const ReadBuf* rb, const size_t roff, 
                   const ProtoUpMeta* um)
{
    Critical crit(mon);
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
    if (tp == 0)
    {
        throw FatalException("not open");
    }
    EVSUserMessage evsm(UUID(), 0xff, SAFE, 0, 0, 0, ViewId(), 0);
    PCUserMessage pcm(0);
    if (tp->get_max_msg_size() < evsm.size() + pcm.size())
    {
        LOG_FATAL("transport max msg size too small: "
                  + make_int(tp->get_max_msg_size()).to_string());
        throw FatalException("transport max msg size too small");
    }
    return tp->get_max_msg_size() - evsm.size() - pcm.size();
}

bool PC::supports_uuid() const
{
    if (tp->supports_uuid() == false)
    {
        throw FatalException("transport does not support UUID");
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
    evs = new EVSProto(event_loop, tp, uuid, name, mon);


    gcomm::connect(tp, evs);

    evs->shift_to(EVSProto::JOINING);
    Time stop(Time::now() + Time(5, 0));
    do
    {
        /* Send join messages without handling them */
        evs->send_join(false);
        int ret = event_loop->poll(500);
        LOG_DEBUG(string("poll returned: ") + make_int(ret).to_string());
    }
    while (stop >= Time::now() && evs->get_known_size() == 1);
    LOG_INFO("PC/EVS Proto initial state: " + evs->to_string());
    
    pc = new PCProto(uuid, event_loop, mon, evs->get_known_size() == 1);
    gcomm::connect(evs, pc);
    gcomm::connect(pc, this);
    
    pc->shift_to(PCProto::S_JOINING);
    LOG_INFO("PC/EVS Proto sending join request");
    evs->send_join();
    do
    {
        int ret = event_loop->poll(50);
        if (ret < 0)
        {
            LOG_WARN("poll(): " + make_int(ret).to_string());
        }
    }
    while (pc->get_state() != PCProto::S_PRIM);
}

void PC::close()
{
    Critical crit(mon);
    LOG_INFO("PC/EVS Proto leaving");
    evs->shift_to(EVSProto::LEAVING);
    evs->send_leave();

    do
    {
        int ret = event_loop->poll(500);
        LOG_DEBUG(string("poll returned ") + make_int(ret).to_string());
    } 
    while (evs->get_state() != EVSProto::CLOSED);

    if (pc->get_state() != PCProto::S_CLOSED)
    {
        LOG_WARN("PCProto didn't reach closed state");
    }


    int cnt = 0;
    do
    {
        event_loop->poll(10);
    }
    while (++cnt < 15);

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
        LOG_FATAL("invalid uri: " + uri.to_string());
    }
}

PC::~PC()
{
    if (tp)
    {
        close();
    }
}

END_GCOMM_NAMESPACE
