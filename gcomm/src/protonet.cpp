/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
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
#ifdef HAVE_ASIO_HPP
    static const std::string& default_backend("asio");
#endif // HAVE_ASIO_HPP
    const std::string backend(conf.get(Conf::ProtonetBackend, default_backend));
    conf.set(Conf::ProtonetBackend, backend);

    const int version(conf.get<int>(Conf::ProtonetVersion, 0));
    conf.set(Conf::ProtonetVersion, version);

    if (version > max_version_)
    {
        gu_throw_error(EINVAL) << "invalid protonet version: " << version;
    }

    log_info << "protonet " << backend << " version " << version;
#ifdef HAVE_ASIO_HPP
    if (backend == "asio")
        return new AsioProtonet(conf, version);
#else
#error "No protonet backends defined"
#endif /* HAVE_ASIO_HPP */
    gu_throw_fatal << Conf::ProtonetBackend << " '" << backend
                   << "' not supported"; throw;
}
