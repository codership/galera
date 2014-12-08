//
// Copyright (C) 2014 Codership Oy <info@codership.com>
//

#include "gu_config.hpp"
#include "gu_asio.hpp"


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
    // SSL is turned on implicitly if key or cert file is defined
    bool use_ssl(conf.is_set(conf::ssl_key) ||
                 conf.is_set(conf::ssl_cert));

    // However, it can be turned off explicitly using socket.use_ssl
    if (conf.is_set(conf::use_ssl))
    {
        use_ssl = conf.get<bool>(conf::use_ssl);
    }

    if (use_ssl == true)
    {
        // set defaults

        // cipher list
        const std::string cipher_list(conf.get(conf::ssl_cipher, "AES128-SHA"));
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
            gu_throw_error(EINVAL) << "Initializing SSL options failed: "
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
                    "could not open password file '" << file
                                                     << "'";
            }

            std::string ret;
            std::getline(ifs, ret);
            return ret;
        }
    private:
        const gu::Config& conf_;
    };
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
    ctx.use_private_key_file(conf.get(conf::ssl_key), asio::ssl::context::pem);
    ctx.use_certificate_file(conf.get(conf::ssl_cert), asio::ssl::context::pem);
    ctx.load_verify_file(conf.get(conf::ssl_ca, conf.get(conf::ssl_cert)));
    SSL_CTX_set_cipher_list(ctx.impl(), conf.get(conf::ssl_cipher).c_str());
}
