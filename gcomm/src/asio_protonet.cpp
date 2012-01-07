/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */


#include "asio_tcp.hpp"
#include "asio_udp.hpp"
#include "asio_addr.hpp"
#include "asio_protonet.hpp"

#include "socket.hpp"

#include "gcomm/util.hpp"
#include "gcomm/conf.hpp"

#include "gu_logger.hpp"

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <fstream>

using namespace std;
using namespace std::rel_ops;
using namespace gu;
using namespace gu::datetime;


#ifdef HAVE_ASIO_SSL_HPP

namespace
{
    static std::string
    get_file(const gu::Config& conf, const std::string& fname)
    {
        try
        {
            return conf.get(fname);
        }
        catch (gu::NotFound& e)
        {
            log_error << "could not find '" << fname << "' from configuration";
            throw;
        }
    }


    static void set_cipher_list(SSL_CTX* ssl_ctx, gu::Config& conf)
    {
        std::string cipher_list(
            conf.get(gcomm::Conf::SocketSslCipherList, "AES128-SHA"));
        if (SSL_CTX_set_cipher_list(ssl_ctx, cipher_list.c_str()) == 0)
        {
            gu_throw_error(EINVAL) << "could not set cipher list, check that "
                                   << "the list is valid: "<< cipher_list;
        }
        conf.set(gcomm::Conf::SocketSslCipherList, cipher_list);
    }

    static void set_compression(gu::Config& conf)
    {
        bool compression(
            conf.get<bool>(gcomm::Conf::SocketSslCompression, true));;
        if (compression == false)
        {
            log_info << "disabling SSL compression";
            sk_SSL_COMP_zero(SSL_COMP_get_compression_methods());
        }
        conf.set(gcomm::Conf::SocketSslCompression, compression);
    }
}


std::string gcomm::AsioProtonet::get_ssl_password() const
{
    std::string   file(get_file(conf_, Conf::SocketSslPasswordFile));
    std::ifstream ifs(file.c_str(), ios_base::in);
    if (ifs.good() == false)
    {
        gu_throw_error(errno) << "could not open password file '" << file
                              << "'";
    }
    std::string ret;
    std::getline(ifs, ret);
    return ret;
}


#endif // HAVE_ASIO_SSL_HPP


gcomm::AsioProtonet::AsioProtonet(gu::Config& conf, int version)
    :
    gcomm::Protonet(conf, "asio", version),
    mutex_(),
    poll_until_(Date::max()),
    io_service_(),
    timer_(io_service_),
#ifdef HAVE_ASIO_SSL_HPP
    ssl_context_(io_service_, asio::ssl::context::sslv23),
#endif // HAVE_ASIO_SSL_HPP
    mtu_(1 << 15),
    checksum_(true)
{
#ifdef HAVE_ASIO_SSL_HPP
    // use ssl if either private key or cert file is specified
    bool use_ssl(conf_.has(Conf::SocketSslPrivateKeyFile)    == true ||
                 conf_.has(Conf::SocketSslCertificateFile)   == true);
    try
    {
        // overrides use_ssl is given explicitly
        use_ssl = conf_.get<bool>(Conf::SocketUseSsl);
    }
    catch (gu::NotFound& nf) { }

    if (use_ssl == true)
    {
        conf_.set(Conf::SocketUseSsl, true);
        log_info << "initializing ssl context";
        set_compression(conf_);
        set_cipher_list(ssl_context_.impl(), conf_);
        ssl_context_.set_verify_mode(asio::ssl::context::verify_peer);
        ssl_context_.set_password_callback(
            boost::bind(&gcomm::AsioProtonet::get_ssl_password, this));

        // private key file (required)
        const std::string private_key_file(
            get_file(conf_, Conf::SocketSslPrivateKeyFile));
        try
        {
            ssl_context_.use_private_key_file(
                private_key_file, asio::ssl::context::pem);
        }
        catch (gu::NotFound& e)
        {
            log_error << "could not load private key file '"
                      << private_key_file << "'";
            throw;
        }
        catch (std::exception& e)
        {
            log_error << "could not use private key file '"
                      << private_key_file
                      << "': " << e.what();
            throw;
        }

        // certificate file (required)
        const std::string certificate_file(
            get_file(conf_, Conf::SocketSslCertificateFile));
        try
        {
            ssl_context_.use_certificate_file(certificate_file,
                                              asio::ssl::context::pem);
        }
        catch (std::exception& e)
        {
            log_error << "could not load certificate file'"
                      << certificate_file
                      << "': " << e.what();
            throw;
        }

        // verify file (optional, defaults to certificate_file)
        const std::string verify_file(
            conf_.get(Conf::SocketSslVerifyFile, certificate_file));
        try
        {
            ssl_context_.load_verify_file(verify_file);
        }
        catch (std::exception& e)
        {
            log_error << "could not load verify file '"
                      << verify_file
                      << "': " << e.what();
            throw;
        }
        conf_.set(Conf::SocketSslVerifyFile, verify_file);
    }
#endif // HAVE_ASIO_SSL_HPP
}

gcomm::AsioProtonet::~AsioProtonet()
{

}

void gcomm::AsioProtonet::enter()
{
    mutex_.lock();
}



void gcomm::AsioProtonet::leave()
{
    mutex_.unlock();
}

gcomm::SocketPtr gcomm::AsioProtonet::socket(const URI& uri)
{
    if (uri.get_scheme() == "tcp" || uri.get_scheme() == "ssl")
    {
        return boost::shared_ptr<AsioTcpSocket>(new AsioTcpSocket(*this, uri));
    }
    else if (uri.get_scheme() == "udp")
    {
        return boost::shared_ptr<AsioUdpSocket>(new AsioUdpSocket(*this, uri));
    }
    else
    {
        gu_throw_fatal << "scheme '" << uri.get_scheme() << "' not implemented";
        throw;
    }
}

gcomm::Acceptor* gcomm::AsioProtonet::acceptor(const URI& uri)
{
    return new AsioTcpAcceptor(*this, uri);
}



Period handle_timers_helper(gcomm::Protonet& pnet, const Period& period)
{
    const Date now(Date::now());
    const Date stop(now + period);

    const Date next_time(pnet.handle_timers());
    const Period sleep_p(min(stop - now, next_time - now));
    return (sleep_p < 0 ? 0 : sleep_p);
}


void gcomm::AsioProtonet::event_loop(const Period& period)
{
    io_service_.reset();
    poll_until_ = Date::now() + period;

    const Period p(handle_timers_helper(*this, period));
    timer_.expires_from_now(boost::posix_time::nanosec(p.get_nsecs()));
    timer_.async_wait(boost::bind(&AsioProtonet::handle_wait, this,
                                  asio::placeholders::error));
    io_service_.run();
}


void gcomm::AsioProtonet::dispatch(const SocketId& id,
                                   const Datagram& dg,
                                   const ProtoUpMeta& um)
{
    for (deque<Protostack*>::iterator i = protos_.begin();
         i != protos_.end(); ++i)
    {
        (*i)->dispatch(id, dg, um);
    }
}


void gcomm::AsioProtonet::interrupt()
{
    io_service_.stop();
}


void gcomm::AsioProtonet::handle_wait(const asio::error_code& ec)
{
    Date now(Date::now());
    const Period p(handle_timers_helper(*this, poll_until_ - now));
    if (ec == asio::error_code() && poll_until_ >= now)
    {
        timer_.expires_from_now(boost::posix_time::nanosec(p.get_nsecs()));
        timer_.async_wait(boost::bind(&AsioProtonet::handle_wait, this,
                                      asio::placeholders::error));
    }
    else
    {
        io_service_.stop();
    }
}

