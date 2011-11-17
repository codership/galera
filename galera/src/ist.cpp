//
// Copyright (C) 2011 Codership Oy <info@codership.com>
//

#include "ist.hpp"

#include "gu_logger.hpp"
#include "gu_uri.hpp"

#include "GCache.hpp"

#include "serialization.hpp"
#include "trx_handle.hpp"
#include <boost/bind.hpp>
#include <fstream>

namespace
{
    static std::string const CONF_SSL_KEY       ("socket.ssl_key");
    static std::string const CONF_SSL_CERT      ("socket.ssl_cert");
    static std::string const CONF_SSL_CA        ("socket.ssl_ca");
    static std::string const CONF_SSL_PSWD_FILE ("socket.ssl_password_file");

    static inline std::string unescape_addr(const std::string& addr)
    {
        std::string ret(addr);
        remove(ret.begin(), ret.end(), '[');
        remove(ret.begin(), ret.end(), ']');
        return addr;
    }


    class SSLPasswordCallback
    {
    public:
        SSLPasswordCallback(const gu::Config& conf) : conf_(conf) { }
        std::string get_password() const
        {
            std::string   file(conf_.get(CONF_SSL_PSWD_FILE));
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

    static void prepare_ssl_ctx(const gu::Config& conf, asio::ssl::context& ctx)
    {
        // Here we blindly assume that ssl globals have been initialized
        // by gcomm.
        ctx.set_verify_mode(asio::ssl::context::verify_peer);
        SSLPasswordCallback cb(conf);
        ctx.set_password_callback(
            boost::bind(&SSLPasswordCallback::get_password, &cb));
        ctx.use_private_key_file(conf.get(CONF_SSL_KEY),
                                 asio::ssl::context::pem);
        ctx.use_certificate_file(conf.get(CONF_SSL_CERT),
                                 asio::ssl::context::pem);
        ctx.load_verify_file(conf.get(CONF_SSL_CA,
                                      conf.get(CONF_SSL_CERT)));
    }
}

//
// Sender                            Receiver
// connect()                 ----->  accept()
//                          <-----   send_handshake()
// send_handshake_response() ----->
//                          <-----   send_ctrl(OK)
// send_trx()                ----->
//                           ----->
// send_ctrl(EOF)            ----->
//                          <-----   close()
// close()


namespace galera
{
    namespace ist
    {
        class Proto
        {
        public:
            class Message
            {
            public:
                typedef enum
                {
                    T_NONE = 0,
                    T_HANDSHAKE = 1,
                    T_HANDSHAKE_RESPONSE = 2,
                    T_CTRL = 3,
                    T_TRX = 4
                } Type;
                Message(int version = -1, Type type = T_NONE,
                        int16_t ctrl = 0, uint64_t len = 0)
                    :
                    version_(version),
                    type_   (type   ),
                    ctrl_   (ctrl   ),
                    len_    (len    )
                { }
                int  version() const { return version_; }
                Type    type() const { return type_   ; }
                int16_t ctrl() const { return ctrl_   ; }
                uint64_t len() const { return len_    ; }

                static inline size_t serial_size(const Message& msg)
                {
                    // header: version 1 byte, type 1 byte, ctrl field 2 bytes
                    return 4 + sizeof(msg.len_);
                }

                static inline size_t serialize(const Message& msg,
                                               gu::byte_t* buf,
                                               size_t buflen, size_t offset)
                {
                    offset = galera::serialize(static_cast<uint8_t>(msg.version_),
                                               buf, buflen, offset);
                    offset = galera::serialize(static_cast<uint8_t>(msg.type_),
                                               buf, buflen, offset);
                    offset = galera::serialize(msg.ctrl_, buf, buflen, offset);
                    offset = galera::serialize(msg.len(), buf, buflen, offset);
                    return offset;
                }

                static inline size_t unserialize(const gu::byte_t* buf,
                                                 size_t buflen,
                                                 size_t offset,
                                                 Message& msg)
                {
                    uint8_t u8;
                    offset = galera::unserialize(buf, buflen, offset, u8);
                    msg.version_ = u8;
                    offset = galera::unserialize(buf, buflen, offset, u8);
                    msg.type_ = static_cast<Proto::Message::Type>(u8);
                    offset = galera::unserialize(buf, buflen, offset, msg.ctrl_);
                    offset = galera::unserialize(buf, buflen, offset, msg.len_);
                    return offset;
                }

            private:

                int      version_;
                Type     type_;
                int16_t  ctrl_;
                uint64_t len_;
            };
            class Handshake : public Message
            {
            public:
                Handshake(int version = -1)
                    :
                    Message(version, Message::T_HANDSHAKE, 0, 0)
                { }
            };
            class HandshakeResponse : public Message
            {
            public:
                HandshakeResponse(int version = -1)
                    :
                    Message(version, Message::T_HANDSHAKE_RESPONSE, 0, 0)
                { }
            };
            class Ctrl : public Message
            {
            public:
                enum
                {
                    // negative values reserved for error codes
                    C_OK = 0,
                    C_EOF = 1
                };
                Ctrl(int version = -1, int16_t code = 0)
                    :
                    Message(version, Message::T_CTRL, code, 0)
                { }
            };
            class Trx : public Message
            {
            public:
                Trx(int version = -1, uint64_t len = 0)
                    :
                    Message(version, Message::T_TRX, 0, len)
                { }
            };

            Proto(int version) : version_(version) { }

            template <class ST>
            void send_handshake(ST& socket)
            {
                Handshake hs(version_);
                gu::Buffer buf(serial_size(hs));
                size_t offset(serialize(hs, &buf[0], buf.size(), 0));
                size_t n(asio::write(socket, asio::buffer(&buf[0], buf.size())));
                if (n != offset)
                {
                    gu_throw_error(EPROTO) << "error sending handshake";
                }
            }

            template <class ST>
            void recv_handshake(ST& socket)
            {
                Message msg;
                gu::Buffer buf(serial_size(msg));
                size_t n(asio::read(socket, asio::buffer(&buf[0], buf.size())));
                if (n != buf.size())
                {
                    gu_throw_error(EPROTO) << "error receiving handshake";
                }
                (void)unserialize(&buf[0], buf.size(), 0, msg);
                log_debug << "handshake msg: " << msg.version() << " "
                          << msg.type() << " " << msg.len();
                switch (msg.type())
                {
                case Message::T_HANDSHAKE:
                    break;
                case Message::T_CTRL:
                    switch (msg.ctrl())
                    {
                    case Ctrl::C_EOF:
                        gu_throw_error(EINTR);
                        throw;
                    default:
                        gu_throw_error(EPROTO) << "unexpected ctrl code: " <<
                            msg.ctrl();
                    }
                    break;
                default:
                    gu_throw_error(EPROTO)
                        << "unexpected message type: " << msg.type();
                    throw;
                }
                if (msg.version() != version_)
                {
                    gu_throw_error(EPROTO) << "mismatching protocol version: "
                                           << msg.version()
                                           << " required: "
                                           << version_;
                }
                // TODO: Figure out protocol versions to use
            }

            template <class ST>
            void send_handshake_response(ST& socket)
            {
                HandshakeResponse hsr(version_);
                gu::Buffer buf(serial_size(hsr));
                size_t offset(serialize(hsr, &buf[0], buf.size(), 0));
                size_t n(asio::write(socket, asio::buffer(&buf[0], buf.size())));
                if (n != offset)
                {
                    gu_throw_error(EPROTO)
                        << "error sending handshake response";
                }
            }

            template <class ST>
            void recv_handshake_response(ST& socket)
            {
                Message msg;
                gu::Buffer buf(serial_size(msg));
                size_t n(asio::read(socket, asio::buffer(&buf[0], buf.size())));
                if (n != buf.size())
                {
                    gu_throw_error(EPROTO) << "error receiving handshake";
                }
                (void)unserialize(&buf[0], buf.size(), 0, msg);

                log_debug << "handshake response msg: " << msg.version()
                          << " " << msg.type()
                          << " " << msg.len();
                switch (msg.type())
                {
                case Message::T_HANDSHAKE_RESPONSE:
                    break;
                case Message::T_CTRL:
                    switch (msg.ctrl())
                    {
                    case Ctrl::C_EOF:
                        gu_throw_error(EINTR) << "interrupted by ctrl";
                        throw;
                    default:
                        gu_throw_error(EPROTO) << "unexpected ctrl code: "
                                               << msg.ctrl();
                        throw;
                    }
                default:
                    gu_throw_error(EINVAL) << "unexpected message type: "
                                           << msg.type();
                    throw;
                }
            }

            template <class ST>
            void send_ctrl(ST& socket, int16_t code)
            {
                Ctrl ctrl(version_, code);
                gu::Buffer buf(serial_size(ctrl));
                size_t offset(serialize(ctrl, &buf[0], buf.size(), 0));
                size_t n(asio::write(socket, asio::buffer(&buf[0], buf.size())));
                if (n != offset)
                {
                    gu_throw_error(EPROTO) << "error sending ctrl message";
                }
            }

            template <class ST>
            int16_t recv_ctrl(ST& socket)
            {
                Message msg;
                gu::Buffer buf(serial_size(msg));
                size_t n(asio::read(socket, asio::buffer(&buf[0], buf.size())));
                if (n != buf.size())
                {
                    gu_throw_error(EPROTO) << "error receiving handshake";
                }
                (void)unserialize(&buf[0], buf.size(), 0, msg);
                log_debug << "msg: " << msg.version() << " " << msg.type()
                          << " " << msg.len();
                switch (msg.type())
                {
                case Message::T_CTRL:
                    break;
                default:
                    gu_throw_error(EPROTO) << "unexpected message type: "
                                           << msg.type();
                    throw;
                }
                return msg.ctrl();
            }


            template <class ST>
            void send_trx(ST&        socket,
                          const gcache::GCache::Buffer& buffer)
            {
                const size_t trx_meta_size(galera::serial_size(buffer.seqno_g())
                                           + galera::serial_size(buffer.seqno_d()));
                const bool rolled_back(buffer.seqno_d() == -1);
                const size_t trx_size(rolled_back == true ? 0 : buffer.size());
                Trx trx(version_, trx_size + trx_meta_size);
                gu::Buffer buf(serial_size(trx) + trx_meta_size);
                size_t offset(serialize(trx, &buf[0], buf.size(), 0));
                offset = galera::serialize(buffer.seqno_g(), &buf[0], buf.size(), offset);
                offset = galera::serialize(buffer.seqno_d(), &buf[0], buf.size(), offset);
                assert(offset = buf.size());
                size_t n;
                if (rolled_back == true)
                {
                    n = asio::write(socket, asio::buffer(&buf[0], buf.size()));
                }
                else
                {
                    boost::array<asio::const_buffer, 2> cbs;
                    cbs[0] = asio::const_buffer(&buf[0], buf.size());
                    cbs[1] = asio::const_buffer(buffer.ptr(), buffer.size());
                    n = asio::write(socket, cbs);
                }
                log_debug << "sent " << n << " bytes";
            }


            template <class ST>
            galera::TrxHandle*
            recv_trx(ST& socket)
            {
                Message msg;
                gu::Buffer buf(serial_size(msg));
                size_t n(asio::read(socket, asio::buffer(&buf[0], buf.size())));
                if (n != buf.size())
                {
                    gu_throw_error(EPROTO) << "error receiving trx header";
                }
                (void)unserialize(&buf[0], buf.size(), 0, msg);
                log_debug << "received header: " << n << " bytes, type "
                          << msg.type() << " len " << msg.len();
                switch (msg.type())
                {
                case Message::T_TRX:
                {
                    buf.resize(msg.len());
                    n = asio::read(socket, asio::buffer(&buf[0], buf.size()));
                    if (n != buf.size())
                    {
                        gu_throw_error(EPROTO) << "error reading trx data";
                    }
                    wsrep_seqno_t seqno_g, seqno_d;
                    galera::TrxHandle* trx(new galera::TrxHandle);
                    size_t offset(galera::unserialize(&buf[0], buf.size(), 0, seqno_g));
                    offset = galera::unserialize(&buf[0], buf.size(), offset, seqno_d);
                    if (seqno_d == -1)
                    {
                        if (offset != msg.len())
                        {
                            gu_throw_error(EINVAL)
                                << "message size "
                                << msg.len()
                                << " does not match expected size "
                                << offset;
                        }
                    }
                    else
                    {
                        offset = unserialize(&buf[0], buf.size(), offset, *trx);
                        trx->append_write_set(&buf[0] + offset, buf.size() - offset);
                    }
                    trx->set_received(0, -1, seqno_g);
                    trx->set_depends_seqno(seqno_d);
                    trx->mark_certified();

                    log_debug << "received trx body: " << *trx;
                    return trx;
                }
                case Message::T_CTRL:
                    switch (msg.ctrl())
                    {
                    case Ctrl::C_EOF:
                        return 0;
                    default:
                        if (msg.ctrl() >= 0)
                        {
                            gu_throw_error(EPROTO)
                                << "unexpected ctrl code: " << msg.ctrl();
                            throw;
                        }
                        else
                        {
                            gu_throw_error(-msg.ctrl()) << "peer reported error";
                            throw;
                        }
                    }
                default:
                    gu_throw_error(EPROTO) << "unexpected message type: "
                                           << msg.type();
                    throw;
                }
                gu_throw_fatal;
                throw;
            }

        private:
            int version_;
        };


        class AsyncSender : public Sender
        {
        public:
            AsyncSender(const gu::Config& conf,
                        const std::string& peer,
                        wsrep_seqno_t first,
                        wsrep_seqno_t last,
                        AsyncSenderMap& asmap,
                        int version)
                :
                Sender(conf, asmap.gcache(), peer, version),
                conf_(conf),
                peer_(peer),
                first_(first),
                last_(last),
                asmap_(asmap),
                thread_()
            { }
            const gu::Config& conf()  { return conf_; }
            const std::string& peer() { return peer_; }
            wsrep_seqno_t first()     { return first_; }
            wsrep_seqno_t last()      { return last_; }
            AsyncSenderMap& asmap()   { return asmap_; }
            pthread_t       thread()  { return thread_; }
        private:
            friend class AsyncSenderMap;
            const gu::Config&  conf_;
            const std::string  peer_;
            wsrep_seqno_t      first_;
            wsrep_seqno_t      last_;
            AsyncSenderMap&    asmap_;
            pthread_t          thread_;
        };
    }
}


std::string const
galera::ist::Receiver::RECV_ADDR("ist.recv_addr");

galera::ist::Receiver::Receiver(gu::Config& conf, const char* addr)
    :
    conf_      (conf),
    io_service_(),
    acceptor_  (io_service_),
    ssl_ctx_   (io_service_, asio::ssl::context::sslv23),
    thread_(),
    mutex_(),
    cond_(),
    consumers_(),
    running_(false),
    ready_(false),
    error_code_(0),
    current_seqno_(-1),
    last_seqno_(-1),
    use_ssl_(false),
    version_(-1)
{
    std::string recv_addr;

    try /* check if receive address is explicitly set */
    {
        recv_addr = conf_.get(RECV_ADDR);
    }
    catch (gu::NotFound&) /* if not, check the alternative.
                             TODO: try to find from system. */
    {
        if (!addr)
            gu_throw_error(EINVAL) << "IST receive address was not configured";

        recv_addr = gu::URI(std::string("tcp://") + addr).get_host();
    }

    conf_.set(RECV_ADDR, recv_addr);

#if REMOVE
    if (addr != 0)
    {
        std::string recv_addr("tcp://");
        recv_addr += gu::URI(recv_addr + addr).get_host();
        recv_addr += ":4568";
        conf_.set(RECV_ADDR, recv_addr);
    }
#endif
}


galera::ist::Receiver::~Receiver()
{ }


extern "C" void* run_receiver_thread(void* arg)
{
    galera::ist::Receiver* receiver(reinterpret_cast<galera::ist::Receiver*>(arg));
    receiver->run();
    return 0;
}

static std::string
IST_determine_recv_addr (const gu::Config& conf)
{
    std::string recv_addr;

    try
    {
        recv_addr = conf.get (galera::ist::Receiver::RECV_ADDR);
    }
    catch (gu::NotFound&)
    {
        gu_throw_error(EINVAL) << '\'' <<  galera::ist::Receiver::RECV_ADDR
                               << '\'' << "not found.";
    }

    /* check if explicit scheme is present */
    if (recv_addr.find("://") == std::string::npos)
    {
        bool ssl(false);

        try
        {
            std::string ssl_key = conf.get(CONF_SSL_KEY);
            if (ssl_key.length() != 0) ssl = true;
        }
        catch (gu::NotFound&) {}

        if (ssl)
            recv_addr.insert(0, "ssl://");
        else
            recv_addr.insert(0, "tcp://");
    }

    try /* check for explicit port,
           TODO: make it possible to use any free port (explicit 0?) */
    {
        gu::URI(recv_addr).get_port();
    }
    catch (gu::NotSet&) /* use gmcast listen port + 1 */
    {
        int port(0);

        try
        {
            port = gu::from_string<uint16_t>(
                gu::URI(conf.get("gmcast.listen_addr")).get_port()) + 1;

        }
        catch (...)
        {
            port = 4568;
        }

        recv_addr += ":" + gu::to_string(port);
    }

    return recv_addr;
}

std::string
galera::ist::Receiver::prepare(wsrep_seqno_t first_seqno,
                               wsrep_seqno_t last_seqno,
                               int           version)
{
    ready_ = false;
    version_ = version;
    recv_addr_ = IST_determine_recv_addr(conf_);
    gu::URI     const uri(recv_addr_);
    try
    {
        if (uri.get_scheme() == "ssl")
        {
            log_info << "IST receiver using ssl";
            use_ssl_ = true;
            prepare_ssl_ctx(conf_, ssl_ctx_);
        }

        asio::ip::tcp::resolver resolver(io_service_);
        asio::ip::tcp::resolver::query query(unescape_addr(uri.get_host()),
                                             uri.get_port());
        asio::ip::tcp::resolver::iterator i(resolver.resolve(query));
        acceptor_.open(i->endpoint().protocol());
        acceptor_.set_option(asio::ip::tcp::socket::reuse_address(true));
        acceptor_.bind(*i);
        acceptor_.listen();
    }
    catch (asio::system_error& e)
    {
        gu_throw_error(e.code().value()) << "failed to open ist listener to "
                                         << uri.to_string();
    }

    current_seqno_ = first_seqno;
    last_seqno_    = last_seqno;
    int err;
    if ((err = pthread_create(&thread_, 0, &run_receiver_thread, this)) != 0)
    {
        gu_throw_error(err) << "unable to create receiver thread";
    }

    running_ = true;

    return recv_addr_;
}


void galera::ist::Receiver::run()
{
    asio::ip::tcp::socket socket(io_service_);
    asio::ssl::context ssl_ctx(io_service_, asio::ssl::context::sslv23);
    asio::ssl::stream<asio::ip::tcp::socket> ssl_stream(io_service_, ssl_ctx_);
    try
    {
        if (use_ssl_ == true)
        {
            acceptor_.accept(ssl_stream.lowest_layer());
            ssl_stream.handshake(asio::ssl::stream<asio::ip::tcp::socket>::server);
        }
        else
        {
            acceptor_.accept(socket);
        }
    }
    catch (asio::system_error& e)
    {
        gu_throw_error(e.code().value()) << "accept() failed";
    }
    acceptor_.close();
    int ec(0);
    try
    {
        Proto p(version_);
        if (use_ssl_ == true)
        {
            p.send_handshake(ssl_stream);
            p.recv_handshake_response(ssl_stream);
            p.send_ctrl(ssl_stream, Proto::Ctrl::C_OK);
        }
        else
        {
            p.send_handshake(socket);
            p.recv_handshake_response(socket);
            p.send_ctrl(socket, Proto::Ctrl::C_OK);
        }
        while (true)
        {
            TrxHandle* trx;
            if (use_ssl_ == true)
            {
                trx = p.recv_trx(ssl_stream);
            }
            else
            {
                trx = p.recv_trx(socket);
            }
            if (trx != 0)
            {
                if (trx->global_seqno() != current_seqno_)
                {
                    log_error << "unexpected trx seqno: " << trx->global_seqno()
                              << " expected: " << current_seqno_;
                    ec = EINVAL;
                    goto err;
                }
                ++current_seqno_;
            }
            gu::Lock lock(mutex_);
            while (ready_ == false || consumers_.empty())
            {
                lock.wait(cond_);
            }
            Consumer* cons(consumers_.top());
            consumers_.pop();
            cons->trx(trx);
            cons->cond().signal();
            if (trx == 0)
            {
                log_debug << "eof received, closing socket";
                break;
            }
        }
    }
    catch (asio::system_error& e)
    {
        log_error << "got error while reading ist stream: " << e.code();
        ec = e.code().value();
    }
    catch (gu::Exception& e)
    {
        ec = e.get_errno();
        if (ec != EINTR)
        {
            log_error << "got exception while reading ist stream: " << e.what();
        }
    }

err:
    gu::Lock lock(mutex_);
    if (use_ssl_ == true)
    {
        ssl_stream.lowest_layer().close();
        // ssl_stream.shutdown();
    }
    else
    {
        socket.close();
    }

    running_ = false;
    if (ec != EINTR && current_seqno_ - 1 != last_seqno_)
    {
        log_error << "IST didn't contain all write sets, expected last: "
                  << last_seqno_ << " last received: " << current_seqno_ - 1;
        ec = EPROTO;
    }
    if (ec != EINTR)
    {
        error_code_ = ec;
    }
    while (consumers_.empty() == false)
    {
        consumers_.top()->cond().signal();
        consumers_.pop();
    }
}


void galera::ist::Receiver::ready()
{
    gu::Lock lock(mutex_);
    ready_ = true;
    cond_.signal();
}

int galera::ist::Receiver::recv(TrxHandle** trx)
{
    Consumer cons;
    gu::Lock lock(mutex_);
    if (running_ == false)
    {
        if (error_code_ != 0)
        {
            gu_throw_error(error_code_) << "IST receiver reported error";
        }
        return EINTR;
    }
    consumers_.push(&cons);
    cond_.signal();
    lock.wait(cons.cond());
    if (cons.trx() == 0)
    {
        if (error_code_ != 0)
        {
            gu_throw_error(error_code_) << "IST receiver reported error";
        }
        return EINTR;
    }
    *trx = cons.trx();
    return 0;
}


void galera::ist::Receiver::finished()
{
    int err;
    interrupt();
    if ((err = pthread_join(thread_, 0)) != 0)
    {
        log_warn << "pthread_join() failed: " << err;
    }
    acceptor_.close();
    gu::Lock lock(mutex_);
    running_ = false;
    while (consumers_.empty() == false)
    {
        consumers_.top()->cond().signal();
        consumers_.pop();
    }
}


void galera::ist::Receiver::interrupt()
{
    gu::URI uri(recv_addr_);
    asio::ip::tcp::resolver resolver(io_service_);
    asio::ip::tcp::resolver::query query(unescape_addr(uri.get_host()),
                                         uri.get_port());
    asio::ip::tcp::resolver::iterator i(resolver.resolve(query));
    try
    {
        if (use_ssl_ == true)
        {
            asio::ssl::stream<asio::ip::tcp::socket>
                ssl_stream(io_service_, ssl_ctx_);
            ssl_stream.lowest_layer().connect(*i);
            ssl_stream.handshake(asio::ssl::stream<asio::ip::tcp::socket>::client);
            Proto p(version_);
            p.recv_handshake(ssl_stream);
            p.send_ctrl(ssl_stream, Proto::Ctrl::C_EOF);
            p.recv_ctrl(ssl_stream);
        }
        else
        {
            asio::ip::tcp::socket socket(io_service_);
            socket.connect(*i);
            Proto p(version_);
            p.recv_handshake(socket);
            p.send_ctrl(socket, Proto::Ctrl::C_EOF);
            p.recv_ctrl(socket);
        }
    }
    catch (asio::system_error& e)
    {
        // ignore
    }
}


galera::ist::Sender::Sender(const gu::Config& conf,
                            gcache::GCache&    gcache,
                            const std::string& peer,
                            int version)
    :
    io_service_(),
    socket_(io_service_),
    ssl_ctx_(io_service_, asio::ssl::context::sslv23),
    ssl_stream_(io_service_, ssl_ctx_),
    use_ssl_(false),
    gcache_(gcache),
    version_(version)
{
    gu::URI uri(peer);
    asio::ip::tcp::resolver resolver(io_service_);
    asio::ip::tcp::resolver::query query(unescape_addr(uri.get_host()),
                                         uri.get_port());
    asio::ip::tcp::resolver::iterator i(resolver.resolve(query));
    if (uri.get_scheme() == "ssl")
    {
        use_ssl_ = true;
    }
    if (use_ssl_ == true)
    {
        log_info << "IST sender using ssl";
        prepare_ssl_ctx(conf, ssl_ctx_);
        ssl_stream_.lowest_layer().connect(*i);
        ssl_stream_.handshake(asio::ssl::stream<asio::ip::tcp::socket>::client);
    }
    else
    {
        socket_.connect(*i);
    }
}


galera::ist::Sender::~Sender()
{
    if (use_ssl_ == true)
    {
        ssl_stream_.lowest_layer().close();
    }
    else
    {
        socket_.close();
    }
    gcache_.seqno_release();
}

void galera::ist::Sender::send(wsrep_seqno_t first, wsrep_seqno_t last)
{
    try
    {
        Proto p(version_);
        int32_t ctrl;
        if (use_ssl_ == true)
        {
            p.recv_handshake(ssl_stream_);
            p.send_handshake_response(ssl_stream_);
            ctrl = p.recv_ctrl(ssl_stream_);
        }
        else
        {
            p.recv_handshake(socket_);
            p.send_handshake_response(socket_);
            ctrl = p.recv_ctrl(socket_);
        }
        if (ctrl < 0)
        {
            gu_throw_error(EPROTO)
                << "ist send failed, peer reported error: " << ctrl;
        }

        std::vector<gcache::GCache::Buffer> buf_vec(1);

        ssize_t n_read;
        while ((n_read = gcache_.seqno_get_buffers(buf_vec, first)) > 0)
        {
            // log_info << "read " << first << " + " << n_read << " from gcache";
            for (wsrep_seqno_t i(0); i < n_read; ++i)
            {
                // log_info << "sending " << buf_vec[i].seqno_g();
                if (use_ssl_ == true)
                {
                    p.send_trx(ssl_stream_, buf_vec[i]);
                }
                else
                {
                    p.send_trx(socket_, buf_vec[i]);
                }
                if (buf_vec[i].seqno_g() == last)
                {
                    if (use_ssl_ == true)
                    {
                        p.send_ctrl(ssl_stream_, Proto::Ctrl::C_EOF);
                    }
                    else
                    {
                        p.send_ctrl(socket_, Proto::Ctrl::C_EOF);
                    }
                    // wait until receiver closes the connection
                    try
                    {
                        gu::byte_t b;
                        size_t n;
                        if (use_ssl_ == true)
                        {
                            n = asio::read(ssl_stream_, asio::buffer(&b, 1));
                        }
                        else
                        {
                            n = asio::read(socket_, asio::buffer(&b, 1));
                        }
                        if (n > 0)
                        {
                            log_warn << "received " << n
                                     << " bytes, expected none";
                        }
                    }
                    catch (asio::system_error& e)
                    { }
                    return;
                }
            }
            first += n_read;
        }
    }
    catch (asio::system_error& e)
    {
        gu_throw_error(e.code().value()) << "ist send failed: " << e.code();
    }
}




extern "C"
void* run_async_sender(void* arg)
{
    galera::ist::AsyncSender* as(reinterpret_cast<galera::ist::AsyncSender*>(arg));
    log_info << "async IST sender starting to serve " << as->peer()
             << " sending " << as->first() << "-" << as->last();
    wsrep_seqno_t join_seqno;
    try
    {
        as->send(as->first(), as->last());
        join_seqno = as->last();
    }
    catch (gu::Exception& e)
    {
        log_error << "async IST sender failed to serve " << as->peer()
                  << ": " << e.what();
        join_seqno = -e.get_errno();
    }
    catch (...)
    {
        log_error << "async IST sender, failed to serve " << as->peer();
        throw;
    }

    try
    {
        as->asmap().remove(as, join_seqno);
        pthread_detach(as->thread());
        delete as;
    }
    catch (gu::NotFound& nf)
    {
        log_debug << "async IST sender already removed";
    }
    log_info << "async IST sender served";

    return 0;
}


void galera::ist::AsyncSenderMap::run(const gu::Config&  conf,
                                      const std::string& peer,
                                      wsrep_seqno_t      first,
                                      wsrep_seqno_t      last,
                                      int                version)
{
    gu::Critical crit(monitor_);
    AsyncSender* as(new AsyncSender(conf, peer, first, last, *this, version));
    int err(pthread_create(&as->thread_, 0, &run_async_sender, as));
    if (err != 0)
    {
        delete as;
        gu_throw_error(err) << "failed to start sender thread";
    }
    senders_.insert(as);
}


void galera::ist::AsyncSenderMap::remove(AsyncSender* as, wsrep_seqno_t seqno)
{
    gu::Critical crit(monitor_);
    std::set<AsyncSender*>::iterator i(senders_.find(as));
    if (i == senders_.end())
    {
        throw gu::NotFound();
    }
    senders_.erase(i);
    gcs_.join(seqno);
}


void galera::ist::AsyncSenderMap::cancel()
{
    gu::Critical crit(monitor_);
    while (senders_.empty() == false)
    {
        AsyncSender* as(*senders_.begin());
        senders_.erase(*senders_.begin());
        int err;
        as->cancel();
        monitor_.leave();
        if ((err = pthread_join(as->thread_, 0)) != 0)
        {
            log_warn << "pthread_join() failed: " << err;
        }
        monitor_.enter();
        delete as;
    }

}
