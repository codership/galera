

#include "gu.hpp"

#ifdef GALERA_USE_BOOST_ASIO
#include "asio.hpp"
#endif // GALERA_USE_BOOST_ASIO

#include "gcomm/util.hpp"

using namespace std;
using namespace gu;
using namespace gu::datetime;
using namespace gcomm;

void gcomm::Protonet::insert(Protostack* pstack) 
{
    log_info << "insert pstack " << pstack;
    if (find(protos_.begin(), protos_.end(), pstack) != protos_.end())
    {
        gu_throw_fatal;
    }
    protos_.push_back(pstack); 
}

void gcomm::Protonet::erase(Protostack* pstack)
{
    log_info << "erase pstack " << pstack;
    std::deque<Protostack*>::iterator i;
    if ((i = find(protos_.begin(), protos_.end(), pstack)) == protos_.end())
    {
        gu_throw_fatal;
    }
    protos_.erase(i);
}

Date gcomm::Protonet::handle_timers()
{
    Critical<Protonet> crit(*this);
    Date next_time(Date::max());
    {
        for (deque<Protostack*>::iterator i = protos_.begin(); 
             i != protos_.end();
             ++i)
        {
            next_time = min(next_time, (*i)->handle_timers());
        }
    }
    return next_time;
}

gcomm::Protonet* gcomm::Protonet::create(const std::string conf)
{
    if (conf == "gu")
        return new GuProtonet();
#ifdef GALERA_USE_BOOST_ASIO
    else if (conf == "asio")
        return new asio::Protonet();
#endif // GALERA_USE_BOOST_ASIO
    gu_throw_fatal << "protonet " << conf << " not supported";
    throw;
}
