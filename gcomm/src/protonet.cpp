/*
 * Copyright (C) 2009-2014 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifdef HAVE_ASIO_HPP
#include "asio_protonet.hpp"
#endif // HAVE_ASIO_HPP

#include "gcomm/util.hpp"
#include "gcomm/conf.hpp"

void gcomm::Protonet::insert(Protostack* pstack)
{
    log_debug << "insert pstack " << pstack;
    if (find(protos_.begin(), protos_.end(), pstack) != protos_.end())
    {
        gu_throw_fatal;
    }
    protos_.push_back(pstack);
}

void gcomm::Protonet::erase(Protostack* pstack)
{
    log_debug << "erase pstack " << pstack;
    std::deque<Protostack*>::iterator i;
    if ((i = find(protos_.begin(), protos_.end(), pstack)) == protos_.end())
    {
        gu_throw_fatal;
    }
    protos_.erase(i);
}

gu::datetime::Date gcomm::Protonet::handle_timers()
{
    Critical<Protonet> crit(*this);
    gu::datetime::Date next_time(gu::datetime::Date::max());
    {
        for (std::deque<Protostack*>::iterator i = protos_.begin();
             i != protos_.end();
             ++i)
        {
            next_time = std::min(next_time, (*i)->handle_timers());
        }
    }
    return next_time;
}

bool gcomm::Protonet::set_param(const std::string& key, const std::string& val)
{
    bool ret(false);
    for (std::deque<Protostack*>::iterator i(protos_.begin());
         i != protos_.end(); ++i)
    {
        ret |= (*i)->set_param(key, val);
    }
    return ret;
}


gcomm::Protonet* gcomm::Protonet::create(gu::Config& conf)
{
    const std::string backend(conf.get(Conf::ProtonetBackend));
    const int version(conf.get<int>(Conf::ProtonetVersion));

    if (version > max_version_)
    {
        gu_throw_error(EINVAL) << "invalid protonet version: " << version;
    }

    log_info << "protonet " << backend << " version " << version;

    if (backend == "asio")
        return new AsioProtonet(conf, version);

    gu_throw_fatal << Conf::ProtonetBackend << " '" << backend
                   << "' not supported"; throw;

    return 0; // keep compiler happy
}
