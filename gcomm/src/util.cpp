
#include "gcomm/util.hpp"
#include "gu_datetime.hpp"

using namespace std;
using namespace std::rel_ops;
using namespace gcomm;
using namespace gu;
using namespace gu::datetime;
using namespace gu::net;


void gcomm::event_loop(Network& el, vector<Protostack>& protos, 
                       const string tstr)
{
    Period p(tstr);
    Date stop = Date::now() + p;
    do
    {
        Date next_time(Date::max());
        for (vector<Protostack>::iterator i = protos.begin(); i != protos.end();
             ++i)
        {
            next_time = min(next_time, i->handle_timers());
        }
        Period sleep_p(min(p, stop - next_time));
        if (sleep_p < 0)
            sleep_p = 0;
        log_info << sleep_p;
        NetworkEvent ev(el.wait_event(static_cast<int>(p.get_nsecs()/MSec), false));
        log_info << ev.get_event_mask();
        if ((ev.get_event_mask() & NetworkEvent::E_EMPTY) == 0)
        {
            const int mask(ev.get_event_mask());
            Socket& s(*ev.get_socket());
            const Datagram* dg(0);
            if (s.get_state() == Socket::S_CONNECTED &&
                mask & NetworkEvent::E_IN)
            {
                dg = s.recv();
                gcomm_assert(dg != 0);
            }
            
            for (vector<Protostack>::iterator i = protos.begin();
                 i != protos.end(); ++i)
            {
                i->dispatch(ev, dg != 0 ? *dg : Datagram());
            }
        }
    }
    while (stop >= Date::now());
}
