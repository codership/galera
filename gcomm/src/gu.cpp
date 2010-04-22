
// gu::Network based implementation

#include "gu.hpp"
#include "gcomm/protostack.hpp"

using namespace std;
using namespace gu;
using namespace gu::net;
using namespace gu::datetime;
using namespace std::rel_ops;



gcomm::SocketPtr gcomm::GuProtonet::socket(const gu::URI& uri)
{
    return SocketPtr(new GuSocket(*this, uri));
}

gcomm::Acceptor* gcomm::GuProtonet::acceptor(const gu::URI& uri)
{
    return new GuAcceptor(*this, uri);
}



void gcomm::GuProtonet::event_loop(const Period& p)
{
    Date stop = Date::now() + p;
    do
    {
        
        Date next_time(handle_timers());
        Period sleep_p(min(stop - Date::now(), next_time - Date::now()));
        
        if (sleep_p < 0)
            sleep_p = 0;
        
        NetworkEvent ev(net.wait_event(sleep_p, false));
        if ((ev.get_event_mask() & E_OUT) != 0)
        {
            Lock lock(mutex);
            ev.get_socket()->send();
        }
        else if ((ev.get_event_mask() & E_EMPTY) == 0)
        {
            const int mask(ev.get_event_mask());
            gu::net::Socket& s(*ev.get_socket());
            const Datagram* dg(0);
            if (s.get_state() == gu::net::Socket::S_CONNECTED &&
                (mask & E_IN))
            {
                dg = s.recv();
                gcomm_assert(dg != 0 
                             || s.get_state() == gu::net::Socket::S_CLOSED
                             || s.get_state() == gu::net::Socket::S_FAILED);
            }
            
            Lock lock(mutex);
            ProtoUpMeta up_um(s.get_errno());
            Datagram up_dg(dg != 0 ? *dg : Datagram());
            for (deque<Protostack*>::iterator i = protos_.begin();
                 i != protos_.end(); ++i)
            {
                (*i)->dispatch(ev.get_socket(), up_dg, up_um);
            }
        }
        Lock lock(mutex);
        if (interrupted == true)
        {
            interrupted = false;
            break;
        }
    }
    while (stop >= Date::now());
}
