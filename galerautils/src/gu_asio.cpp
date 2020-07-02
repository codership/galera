//
// Copyright (C) 2014-2020 Codership Oy <info@codership.com>
//

#include "gu_config.hpp"
#include "gu_asio.hpp"
#include "gu_datetime.hpp"

#ifdef ASIO_HPP
#error "asio.hpp is already included before gu_asio.hpp, can't customize asio.hpp"
#endif // ASIO_HPP

#include "asio/version.hpp"

// ASIO does not interact well with kqueue before ASIO 1.10.5, see
// https://readlist.com/lists/freebsd.org/freebsd-current/23/119264.html
// http://think-async.com/Asio/asio-1.10.6/doc/asio/history.html#asio.history.asio_1_10_5
#if ASIO_VERSION < 101005
# define ASIO_DISABLE_KQUEUE
#endif // ASIO_VERSION < 101005

#define GU_ASIO_IMPL
#include "gu_asio_datagram.hpp"
#include "gu_asio_debug.hpp"
#include "gu_asio_error_category.hpp"
#include "gu_asio_io_service_impl.hpp"
#include "gu_asio_ip_address_impl.hpp"
#include "gu_asio_stream_react.hpp"
#include "gu_asio_utils.hpp"

#ifndef ASIO_HAS_BOOST_BIND
#define ASIO_HAS_BOOST_BIND
#endif // ASIO_HAS_BOOST_BIND
#include "asio/placeholders.hpp"

#ifdef GALERA_HAVE_SSL
#include "asio/ssl.hpp"
#endif // GALERA_HAVE_SSL

#if (__GNUC__ == 4 && __GNUC_MINOR__ == 4)
#include "asio/deadline_timer.hpp"
#else
#include "asio/steady_timer.hpp"
#endif // #if (__GNUC__ == 4 && __GNUC_MINOR__ == 4)

#include <boost/bind.hpp>

#include <fstream>
#include <mutex>

//
// AsioIpAddress wrapper
//

//
// IPv4
//

gu::AsioIpAddressV4::AsioIpAddressV4()
    : impl_(std::unique_ptr<Impl>(new Impl))
{ }

gu::AsioIpAddressV4::AsioIpAddressV4(const AsioIpAddressV4& other)
    : impl_(std::unique_ptr<Impl>(new Impl(*other.impl_)))
{ }

gu::AsioIpAddressV4& gu::AsioIpAddressV4::operator=(AsioIpAddressV4 other)
{
    std::swap(this->impl_, other.impl_);
    return *this;
}

gu::AsioIpAddressV4::~AsioIpAddressV4()
{ }

bool gu::AsioIpAddressV4::is_multicast() const
{
    return impl_->native().is_multicast();
}

gu::AsioIpAddressV4::Impl& gu::AsioIpAddressV4::impl()
{
    return *impl_;
}

const gu::AsioIpAddressV4::Impl& gu::AsioIpAddressV4::impl() const
{
    return *impl_;
}

//
// IPv6
//

gu::AsioIpAddressV6::AsioIpAddressV6()
    : impl_(std::unique_ptr<Impl>(new Impl))
{ }

gu::AsioIpAddressV6::AsioIpAddressV6(const AsioIpAddressV6& other)
    : impl_(std::unique_ptr<Impl>(new Impl(*other.impl_)))
{ }

gu::AsioIpAddressV6& gu::AsioIpAddressV6::operator=(AsioIpAddressV6 other)
{
    std::swap(this->impl_, other.impl_);
    return *this;
}

gu::AsioIpAddressV6::~AsioIpAddressV6()
{ }


bool gu::AsioIpAddressV6::is_link_local() const
{
    return impl_->native().is_link_local();
}

unsigned long gu::AsioIpAddressV6::scope_id() const
{
    return impl_->native().scope_id();
}

bool gu::AsioIpAddressV6::is_multicast() const
{
    return impl_->native().is_multicast();
}

gu::AsioIpAddressV6::Impl& gu::AsioIpAddressV6::impl()
{
    return *impl_;
}

const gu::AsioIpAddressV6::Impl& gu::AsioIpAddressV6::impl() const
{
    return *impl_;
}

//
// Generic Ip address wrapper
//

gu::AsioIpAddress::AsioIpAddress()
    : impl_(std::unique_ptr<Impl>(new Impl))
{ }

gu::AsioIpAddress::AsioIpAddress(const AsioIpAddress& other)
    : impl_(std::unique_ptr<Impl>(new Impl(*other.impl_)))
{ }

gu::AsioIpAddress& gu::AsioIpAddress::operator=(AsioIpAddress other)
{
    std::swap(this->impl_, other.impl_);
    return *this;
}

gu::AsioIpAddress::~AsioIpAddress()
{ }


gu::AsioIpAddress::Impl& gu::AsioIpAddress::impl()
{
    return *impl_;
}

const gu::AsioIpAddress::Impl& gu::AsioIpAddress::impl() const
{
    return *impl_;
}


bool gu::AsioIpAddress::is_v4() const
{
    return impl_->native().is_v4();
}

bool gu::AsioIpAddress::is_v6() const
{
    return impl_->native().is_v6();
}

gu::AsioIpAddressV4 gu::AsioIpAddress::to_v4() const
{
    gu::AsioIpAddressV4 ret;
    ret.impl().native() = impl_->native().to_v4();
    return ret;
}

gu::AsioIpAddressV6 gu::AsioIpAddress::to_v6() const
{
    gu::AsioIpAddressV6 ret;
    ret.impl().native() = impl_->native().to_v6();
    return ret;
}

//
// Asio Error Code
//

gu::AsioErrorCategory gu_asio_system_category(asio::error::get_system_category());
gu::AsioErrorCategory gu_asio_misc_category(asio::error::get_misc_category());
#ifdef GALERA_HAVE_SSL
gu::AsioErrorCategory gu_asio_ssl_category(asio::error::get_ssl_category());
#endif // GALERA_HAVE_SSL

gu::AsioErrorCode::AsioErrorCode()
    : value_()
    , category_(&gu_asio_system_category)
    , wsrep_category_()
{ }

gu::AsioErrorCode::AsioErrorCode(int err)
    : value_(err)
    , category_(&gu_asio_system_category)
    , wsrep_category_()
{ }

std::string gu::AsioErrorCode::message() const
{
    if (category_)
    {
        return asio::error_code(value_, category_->native()).message();
    }
    else
    {
        std::ostringstream oss;
        oss << ::strerror(value_);
        return oss.str();
    }
}

std::ostream& gu::operator<<(std::ostream& os, const gu::AsioErrorCode& ec)
{
    if (ec.category())
    {
        return (os << asio::error_code(ec.value(), ec.category()->native()));
    }
    else
    {
        return (os << ::strerror(ec.value()));
    }
}

bool gu::AsioErrorCode::is_eof() const
{
    return (category_ &&
            *category_ == gu_asio_misc_category &&
            value_ == asio::error::misc_errors::eof);
}

bool gu::AsioErrorCode::is_wsrep() const
{
    return wsrep_category_;
}

bool gu::AsioErrorCode::is_system() const
{
    return (not (category_ || wsrep_category_) ||
            (category_ &&
             *category_ == gu_asio_system_category));
}

//
// Utility methods
//


std::string gu::any_addr(const gu::AsioIpAddress& addr)
{
    return ::any_addr(addr.impl().native());
}

std::string gu::unescape_addr(const std::string& addr)
{
    std::string ret(addr);
    size_t pos(ret.find('['));
    if (pos != std::string::npos) ret.erase(pos, 1);
    pos = ret.find(']');
    if (pos != std::string::npos) ret.erase(pos, 1);
    return ret;
}

gu::AsioIpAddress gu::make_address(const std::string& addr)
{
    gu::AsioIpAddress ret;
    ret.impl().native() = ::make_address(addr);
    return ret;
}

//
// SSL/TLS
//
//

#ifdef GALERA_HAVE_SSL

namespace
{
    // Callback for reading SSL key protection password from file
    class SSLPasswordCallback
    {
    public:
        SSLPasswordCallback(const gu::Config& conf) : conf_(conf) { }

        std::string get_password() const
        {
            std::string   file(conf_.get(gu::conf::ssl_password_file));
            std::ifstream ifs(file.c_str(), std::ios_base::in);

            if (ifs.good() == false)
            {
                gu_throw_error(errno) <<
                    "could not open password file '" << file << "'";
            }

            std::string ret;
            std::getline(ifs, ret);
            return ret;
        }
    private:
        const gu::Config& conf_;
    };
}

static void throw_last_SSL_error(const std::string& msg)
{
    unsigned long const err(ERR_peek_last_error());
    char errstr[120] = {0, };
    ERR_error_string_n(err, errstr, sizeof(errstr));
    gu_throw_error(EINVAL) << msg << ": "
                           << err << ": '" << errstr << "'";
}

// Exclude some errors which are generated by the SSL library.
bool exclude_ssl_error(const asio::error_code& ec)
{
    switch (ERR_GET_REASON(ec.value()))
    {
#ifdef SSL_R_SHORT_READ
    case SSL_R_SHORT_READ:
        // Short read error seems to be generated quite frequently
        // by SSL library, probably because broken connections.
        return true;
#endif /* SSL_R_SHORT_READ */
    default:
        return false;
    }
}

// Return low level error info for asio::error_code if available.
std::string extra_error_info(const asio::error_code& ec)
{
    std::ostringstream os;
    if (ec.category() == asio::error::get_ssl_category())
    {
        char errstr[120] = {0, };
        ERR_error_string_n(ec.value(), errstr, sizeof(errstr));
        os << ec.value() << ": '" << errstr << "'";
    }
    return os.str();
}

std::string gu::extra_error_info(const gu::AsioErrorCode& ec)
{
    if (ec.category())
        return ::extra_error_info(asio::error_code(ec.value(),
                                                   ec.category()->native()));
    else
        return "";
}

static SSL_CTX* native_ssl_ctx(asio::ssl::context& context)
{
#if ASIO_VERSION < 101601
    return context.impl();
#else
    return context.native_handle();
#endif
}

static void ssl_prepare_context(const gu::Config& conf, asio::ssl::context& ctx,
                                bool verify_peer_cert = true)
{
    ctx.set_verify_mode(asio::ssl::context::verify_peer |
                        (verify_peer_cert == true ?
                         asio::ssl::context::verify_fail_if_no_peer_cert : 0));
    SSLPasswordCallback cb(conf);
    ctx.set_password_callback(
        boost::bind(&SSLPasswordCallback::get_password, &cb));

    std::string param;

    try
    {
        // In some older OpenSSL versions ECDH engines must be enabled
        // explicitly. Here we use SSL_CTX_set_ecdh_auto() or
        // SSL_CTX_set_tmp_ecdh() if present.
#if defined(OPENSSL_HAS_SET_ECDH_AUTO)
        if (!SSL_CTX_set_ecdh_auto(native_ssl_ctx(ctx), 1))
        {
            throw_last_SSL_error("SSL_CTX_set_ecdh_auto() failed");
        }
#elif defined(OPENSSL_HAS_SET_TMP_ECDH)
        {
            EC_KEY* const ecdh(EC_KEY_new_by_curve_name(NID_X9_62_prime256v1));
            if (ecdh == NULL)
            {
                throw_last_SSL_error("EC_KEY_new_by_curve_name() failed");
            }
            if (!SSL_CTX_set_tmp_ecdh(native_ssl_ctx(ctx),ecdh))
            {
                throw_last_SSL_error("SSL_CTX_set_tmp_ecdh() failed");
            }
            EC_KEY_free(ecdh);
        }
#endif /* OPENSSL_HAS_SET_ECDH_AUTO | OPENSSL_HAS_SET_TMP_ECDH */
        param = gu::conf::ssl_key;
        ctx.use_private_key_file(conf.get(param), asio::ssl::context::pem);
        param = gu::conf::ssl_cert;
        ctx.use_certificate_file(conf.get(param), asio::ssl::context::pem);
        param = gu::conf::ssl_ca;
        ctx.load_verify_file(conf.get(param, conf.get(gu::conf::ssl_cert)));
        param = gu::conf::ssl_cipher;
        std::string const value(conf.get(param));
        if (!value.empty())
        {
            if (!SSL_CTX_set_cipher_list(native_ssl_ctx(ctx), value.c_str()))
            {
                throw_last_SSL_error("Error setting SSL cipher list to '"
                                      + value + "'");
            }
            else
            {
                log_info << "SSL cipher list set to '" << value << '\'';
            }
        }
        ctx.set_options(asio::ssl::context::no_sslv2 |
                        asio::ssl::context::no_sslv3 |
                        asio::ssl::context::no_tlsv1);
    }
    catch (asio::system_error& ec)
    {
        gu_throw_error(EINVAL) << "Bad value '" << conf.get(param, "")
                               << "' for SSL parameter '" << param
                               << "': " << ::extra_error_info(ec.code());
    }
    catch (gu::NotSet& ec)
    {
        gu_throw_error(EINVAL) << "Missing required value for SSL parameter '"
                               << param << "'";
    }
}

/* checks if all mandatory SSL options are set */
static bool ssl_check_conf(const gu::Config& conf)
{
    using namespace gu;

    bool explicit_ssl(false);

    if (conf.is_set(conf::use_ssl))
    {
        if  (conf.get<bool>(conf::use_ssl) == false)
        {
            return false; // SSL is explicitly disabled
        }
        else
        {
            explicit_ssl = true;
        }
    }

    int count(0);

    count += conf.is_set(conf::ssl_key);
    count += conf.is_set(conf::ssl_cert);

    bool const use_ssl(explicit_ssl || count > 0);

    if (use_ssl && count < 2)
    {
        gu_throw_error(EINVAL) << "To enable SSL at least both of '"
                               << conf::ssl_key << "' and '" << conf::ssl_cert
                               << "' must be set";
    }

    return use_ssl;
}

static void init_use_ssl(gu::Config& conf)
{
    // use ssl if either private key or cert file is specified
    bool use_ssl(conf.is_set(gu::conf::ssl_key)  == true ||
                 conf.is_set(gu::conf::ssl_cert) == true);
    try
    {
        // overrides use_ssl if set explicitly
        use_ssl = conf.get<bool>(gu::conf::use_ssl);
    }
    catch (gu::NotSet& nf) {}

    if (use_ssl == true)
    {
        conf.set(gu::conf::use_ssl, true);
    }
}

void gu::ssl_register_params(gu::Config& conf)
{
    // register SSL config parameters
    conf.add(gu::conf::use_ssl);
    conf.add(gu::conf::ssl_cipher);
    conf.add(gu::conf::ssl_compression);
    conf.add(gu::conf::ssl_key);
    conf.add(gu::conf::ssl_cert);
    conf.add(gu::conf::ssl_ca);
    conf.add(gu::conf::ssl_password_file);
}

void gu::ssl_init_options(gu::Config& conf)
{
    init_use_ssl(conf);

    bool use_ssl(ssl_check_conf(conf));

    if (use_ssl == true)
    {
        // set defaults

        // cipher list
        const std::string cipher_list(conf.get(conf::ssl_cipher, ""));
        conf.set(conf::ssl_cipher, cipher_list);

        // compression
        bool compression(conf.get(conf::ssl_compression, true));
        if (compression == false)
        {
            log_info << "disabling SSL compression";
            sk_SSL_COMP_zero(SSL_COMP_get_compression_methods());
        }
        conf.set(conf::ssl_compression, compression);


        // verify that asio::ssl::context can be initialized with provided
        // values
        try
        {
#if ASIO_VERSION < 101601
            asio::io_service io_service;
            asio::ssl::context ctx(io_service, asio::ssl::context::sslv23);
#else
            asio::ssl::context ctx(asio::ssl::context::sslv23);
#endif
            ssl_prepare_context(conf, ctx);
        }
        catch (asio::system_error& ec)
        {
            gu_throw_error(EINVAL) << "Initializing SSL context failed: "
                                   << ::extra_error_info(ec.code());
        }
    }
}

#endif // GALERA_HAVE_SSL

bool gu::is_verbose_error(const gu::AsioErrorCode& ec)
{
    // Suppress system error which occur frequently during configuration
    // changes and are not likely caused by programming errors.
    if (ec.is_system())
    {
        switch (ec.value())
        {
        case ECANCELED:  // Socket close
        case EPIPE:      // Writing while remote end closed connection
        case ECONNRESET: // Remote end closed connection
        case EBADF:      // Socket closed before completion/read handler exec
            return true;
        default:
            return false;
        }
    }
    // EOF errors happen all the time when cluster configuration changes.
    if (ec.is_eof())
        return true;
#ifdef GALERA_HAVE_SSL
    // Suppress certain SSL errors.
    return (not ec.category() ||
            *ec.category() != gu_asio_ssl_category ||
            exclude_ssl_error(asio::error_code(
                                  ec.value(), ec.category()->native())));
#else
    return false;
#endif // GALERA_HAVE_SSL
}

//
// IO Service wrapper
//

gu::AsioIoService::AsioIoService(const gu::Config& conf)
    : impl_(std::unique_ptr<Impl>(new Impl))
    , conf_(conf)
{
#ifdef GALERA_HAVE_SSL
    if (conf.has(conf::use_ssl) && conf.get<bool>(conf::use_ssl, false))
    {
        load_crypto_context();
    }
#endif // GALERA_HAVE_SSL
}

gu::AsioIoService::~AsioIoService() = default;

void gu::AsioIoService::load_crypto_context()
{
#ifdef GALERA_HAVE_SSL
    if (not impl_->ssl_context_)
    {
        impl_->ssl_context_ = std::unique_ptr<asio::ssl::context>(
            new asio::ssl::context(asio::ssl::context::sslv23));
    }

    ssl_prepare_context(conf_, *impl_->ssl_context_);
#endif // GALERA_HAVE_SSL
}

void gu::AsioIoService::run_one()
{
    impl_->native().run_one();
}

void gu::AsioIoService::run()
{
    impl_->native().run();
}

void gu::AsioIoService::post(std::function<void()> fun)
{
    impl_->native().post(fun);
}

void gu::AsioIoService::stop()
{
    impl_->native().stop();
}

void gu::AsioIoService::reset()
{
    impl_->native().reset();
}

gu::AsioIoService::Impl& gu::AsioIoService::impl()
{
    return *impl_;
}

std::shared_ptr<gu::AsioSocket>
gu::AsioIoService::make_socket(
    const gu::URI& uri,
    const std::shared_ptr<gu::AsioStreamEngine>& engine)
{
    return std::make_shared<AsioStreamReact>(*this, uri.get_scheme(), engine);
}

std::shared_ptr<gu::AsioDatagramSocket> gu::AsioIoService::make_datagram_socket(
    const gu::URI& uri)
{
    if (uri.get_scheme() == gu::scheme::udp)
        return std::make_shared<AsioUdpSocket>(*this);
    gu_throw_error(EINVAL) << "Datagram socket scheme " << uri.get_scheme()
                           << " not supported";
    return std::shared_ptr<AsioDatagramSocket>();
}

std::shared_ptr<gu::AsioAcceptor> gu::AsioIoService::make_acceptor(
    const gu::URI& uri)
{
    return std::make_shared<AsioAcceptorReact>(*this, uri.get_scheme());
}

//
// Steady timer
//

class gu::AsioSteadyTimer::Impl
{
public:
#if (__GNUC__ == 4 && __GNUC_MINOR__ == 4)
    typedef asio::deadline_timer native_timer_type;
#else
    typedef asio::steady_timer native_timer_type;
#endif /* #if (__GNUC__ == 4 && __GNUC_MINOR__ == 4) */

    Impl(asio::io_service& io_service) : timer_(io_service) { }
    native_timer_type& native() { return timer_; }
    void handle_wait(const std::shared_ptr<AsioSteadyTimerHandler>& handler,
                     const asio::error_code& ec)
    {
        handler->handle_wait(AsioErrorCode(ec.value(), ec.category()));
    }
private:
    native_timer_type timer_;
};

#if (__GNUC__ == 4 && __GNUC_MINOR__ == 4)
static inline boost::posix_time::time_duration
to_native_duration(const gu::AsioClock::duration& duration)
{
    return boost::posix_time::nanosec(
        std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
}
#else
static inline std::chrono::steady_clock::duration
to_native_duration(const gu::AsioClock::duration& duration)
{
    return duration;
}
#endif

gu::AsioSteadyTimer::AsioSteadyTimer(
    AsioIoService& io_service)
    : impl_(new Impl(io_service.impl().native()))
{ }

gu::AsioSteadyTimer::~AsioSteadyTimer()
{ }

void gu::AsioSteadyTimer::expires_from_now(
    const AsioClock::duration& duration)
{
    impl_->native().expires_from_now(to_native_duration(duration));
}

void gu::AsioSteadyTimer::async_wait(
    const std::shared_ptr<AsioSteadyTimerHandler>& handler)
{
    impl_->native().async_wait(boost::bind(&Impl::handle_wait,
                                           impl_.get(), handler,
                                           asio::placeholders::error));
}

void gu::AsioSteadyTimer::cancel()
{
    impl_->native().cancel();
}
