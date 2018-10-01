//
// Copyright (C) 2014-2015 Codership Oy <info@codership.com>
//

#include "gu_config.hpp"
#include "gu_asio.hpp"

#include <boost/bind.hpp>

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

void gu::ssl_init_options(gu::Config& conf)
{
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
            asio::io_service io_service;
            asio::ssl::context ctx(io_service, asio::ssl::context::sslv23);
            ssl_prepare_context(conf, ctx);
        }
        catch (asio::system_error& ec)
        {
            gu_throw_error(EINVAL) << "Initializing SSL context failed: "
                                   << extra_error_info(ec.code());
        }
    }
}

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

void gu::ssl_prepare_context(const gu::Config& conf, asio::ssl::context& ctx,
                             bool verify_peer_cert)
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
        if (!SSL_CTX_set_ecdh_auto(ctx.impl(), 1))
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
            if (!SSL_CTX_set_tmp_ecdh(ctx.impl(),ecdh))
            {
                throw_last_SSL_error("SSL_CTX_set_tmp_ecdh() failed");
            }
            EC_KEY_free(ecdh);
        }
#endif /* OPENSSL_HAS_SET_ECDH_AUTO | OPENSSL_HAS_SET_TMP_ECDH */
        param = conf::ssl_key;
        ctx.use_private_key_file(conf.get(param), asio::ssl::context::pem);
        param = conf::ssl_cert;
        ctx.use_certificate_file(conf.get(param), asio::ssl::context::pem);
        param = conf::ssl_ca;
        ctx.load_verify_file(conf.get(param, conf.get(conf::ssl_cert)));
        param = conf::ssl_cipher;
        std::string const value(conf.get(param));
        if (!value.empty())
        {
            if (!SSL_CTX_set_cipher_list(ctx.impl(), value.c_str()))
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
                               << "': " << extra_error_info(ec.code());
    }
    catch (gu::NotSet& ec)
    {
        gu_throw_error(EINVAL) << "Missing required value for SSL parameter '"
                               << param << "'";
    }
}
