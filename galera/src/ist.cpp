//
// Copyright (C) 2011 Codership Oy <info@codership.com>
//

#include "ist.hpp"

#include "gu_logger.hpp"
#include "gu_uri.hpp"

#include "GCache.hpp"

#include "serialization.hpp"
#include "trx_handle.hpp"

namespace
{
    static inline std::string unescape_addr(const std::string& addr)
    {
        std::string ret(addr);
        remove(ret.begin(), ret.end(), '[');
        remove(ret.begin(), ret.end(), ']');
        return addr;
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
                    T_HANDSHAKE_RESPONSE,
                    T_CTRL,
                    T_TRX
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
            private:
                friend size_t serial_size(const Message&);
                friend size_t serialize(const Message&,
                                        gu::byte_t*, size_t, size_t);
                friend size_t unserialize(const gu::byte_t*, size_t, size_t,
                                          Message&);
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

            Proto() : version_(0) { }
            void send_handshake(asio::ip::tcp::socket& socket);
            void send_handshake_response(asio::ip::tcp::socket& socket);
            void recv_handshake(asio::ip::tcp::socket& socket);
            void recv_handshake_response(asio::ip::tcp::socket& socket);
            void send_trx(asio::ip::tcp::socket& socket,
                          const gcache::GCache::Buffer& buffer);
            galera::TrxHandle* recv_trx(asio::ip::tcp::socket& socket);
            void send_ctrl(asio::ip::tcp::socket& socket, int32_t code);
            int16_t recv_ctrl(asio::ip::tcp::socket& socket);
        private:
            int version_;
        };

        inline size_t serial_size(const Proto::Message& msg)
        {
            // header: version 1 byte, type 1 byte, ctrl field 2 bytes
            return 4 + sizeof(msg.len_);
        }

        inline size_t serialize(const Proto::Message& msg,
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

        inline size_t unserialize(const gu::byte_t* buf,
                                  size_t buflen,
                                  size_t offset,
                                  Proto::Message& msg)
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

    }
}


void galera::ist::Proto::send_handshake(asio::ip::tcp::socket& socket)
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


void galera::ist::Proto::recv_handshake(asio::ip::tcp::socket& socket)
{
    Handshake hs;
    gu::Buffer buf(serial_size(hs));
    size_t n(asio::read(socket, asio::buffer(&buf[0], buf.size())));
    if (n != buf.size())
    {
        gu_throw_error(EPROTO) << "error receiving handshake";
    }
    unserialize(&buf[0], buf.size(), 0, hs);
    log_debug << "hs: " << hs.version() << " " << hs.type() << " " << hs.len();
    // TODO: Figure out protocol versions to use
}


void galera::ist::Proto::send_handshake_response(asio::ip::tcp::socket& socket)
{
    HandshakeResponse hsr(version_);
    gu::Buffer buf(serial_size(hsr));
    size_t offset(serialize(hsr, &buf[0], buf.size(), 0));
    size_t n(asio::write(socket, asio::buffer(&buf[0], buf.size())));
    if (n != offset)
    {
        gu_throw_error(EPROTO) << "error sending handshake response";
    }
}


void galera::ist::Proto::recv_handshake_response(asio::ip::tcp::socket& socket)
{
    Handshake hsr;
    gu::Buffer buf(serial_size(hsr));
    size_t n(asio::read(socket, asio::buffer(&buf[0], buf.size())));
    if (n != buf.size())
    {
        gu_throw_error(EPROTO) << "error receiving handshake";
    }
    unserialize(&buf[0], buf.size(), 0, hsr);

    log_debug << "hsr: " << hsr.version() << " " << hsr.type()
             << " " << hsr.len();
}


void galera::ist::Proto::send_ctrl(asio::ip::tcp::socket& socket, int32_t code)
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


int16_t galera::ist::Proto::recv_ctrl(asio::ip::tcp::socket& socket)
{
    Handshake ctrl;
    gu::Buffer buf(serial_size(ctrl));
    size_t n(asio::read(socket, asio::buffer(&buf[0], buf.size())));
    if (n != buf.size())
    {
        gu_throw_error(EPROTO) << "error receiving handshake";
    }
    unserialize(&buf[0], buf.size(), 0, ctrl);

    log_debug << "ctrl: " << ctrl.version() << " " << ctrl.type()
              << " " << ctrl.len();
    return ctrl.ctrl();
}


void galera::ist::Proto::send_trx(asio::ip::tcp::socket&        socket,
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


galera::TrxHandle*
galera::ist::Proto::recv_trx(asio::ip::tcp::socket& socket)
{
    Message msg;
    gu::Buffer buf(serial_size(msg));
    size_t n(asio::read(socket, asio::buffer(&buf[0], buf.size())));
    if (n != buf.size())
    {
        gu_throw_error(EPROTO) << "error receiving trx header";
    }
    unserialize(&buf[0], buf.size(), 0, msg);
    log_debug << "received header: " << n << " bytes, type " << msg.type()
              << " len " << msg.len();
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
        gu_throw_error(EPROTO) << "unexpected message type " << msg.type();
        throw;
    }
    gu_throw_fatal;
    throw;
}

std::string const
galera::ist::Receiver::RECV_ADDR("ist.recv_addr");

galera::ist::Receiver::Receiver(gu::Config& conf, const char* addr)
    :
    conf_      (conf),
    io_service_(),
    acceptor_  (io_service_),
    thread_(),
    mutex_(),
    cond_(),
    consumers_(),
    running_(false)
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

    try /* check if explicit scheme is present */
    {
        gu::URI tmp(recv_addr);
    }
    catch (gu::Exception&)
    {
        bool ssl(false);

        try
        {
            std::string ssl_key = conf.get("socket.ssl_key");
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
galera::ist::Receiver::prepare()
{
    std::string const recv_addr(IST_determine_recv_addr(conf_));
    gu::URI     const uri(recv_addr);

    try
    {
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

    int err;

    if ((err = pthread_create(&thread_, 0, &run_receiver_thread, this)) != 0)
    {
        gu_throw_error(err) << "unable to create receiver thread";
    }

    running_ = true;

    return (recv_addr);
}


void galera::ist::Receiver::run()
{
    asio::ip::tcp::socket socket(io_service_);
    try
    {
        acceptor_.accept(socket);
    }
    catch (asio::system_error& e)
    {
        gu_throw_error(e.code().value()) << "accept() failed";
    }


    try
    {
        Proto p;
        p.send_handshake(socket);
        p.recv_handshake_response(socket);
        p.send_ctrl(socket, Proto::Ctrl::C_OK);
        while (true)
        {
            TrxHandle* trx(p.recv_trx(socket));
            gu::Lock lock(mutex_);
            while (consumers_.empty())
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
                socket.close();
                break;
            }
        }
    }
    catch (asio::system_error& e)
    {
        log_warn << "got error while reading ist stream: " << e.code();
    }

    gu::Lock lock(mutex_);
    socket.close();
    running_ = false;
    while (consumers_.empty() == false)
    {
        consumers_.top()->cond().signal();
        consumers_.pop();
    }
}


int galera::ist::Receiver::recv(TrxHandle** trx)
{
    Consumer cons;
    gu::Lock lock(mutex_);
    if (running_ == false)
    {
        return EINTR;
    }
    consumers_.push(&cons);
    cond_.signal();
    lock.wait(cons.cond());
    if (cons.trx() == 0)
    {
        // TODO: Figure out proper errno
        return EINTR;
    }
    *trx = cons.trx();
    return 0;
}


void galera::ist::Receiver::finished()
{
    int err;
    if ((err = pthread_cancel(thread_)) == 0)
    {
        if ((err = pthread_join(thread_, 0)) != 0)
        {
            log_warn << "pthread_join() failed: " << err;
        }
    }
    else
    {
        log_warn << "pthread_cancel() failed: " << err;
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


galera::ist::Sender::Sender(gcache::GCache&    gcache,
                            const std::string& peer)
    :
    io_service_(),
    socket_(io_service_),
    gcache_(gcache)
{
    gu::URI uri(peer);
    asio::ip::tcp::resolver resolver(io_service_);
    asio::ip::tcp::resolver::query query(unescape_addr(uri.get_host()),
                                         uri.get_port());
    asio::ip::tcp::resolver::iterator i(resolver.resolve(query));
    socket_.connect(*i);
}


galera::ist::Sender::~Sender()
{
    socket_.close();
    gcache_.seqno_release();
}

void galera::ist::Sender::send(wsrep_seqno_t first, wsrep_seqno_t last)
{
    try
    {
        Proto p;

        p.recv_handshake(socket_);
        p.send_handshake_response(socket_);
        const int32_t ctrl(p.recv_ctrl(socket_));
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
                p.send_trx(socket_, buf_vec[i]);
                if (buf_vec[i].seqno_g() == last)
                {
                    p.send_ctrl(socket_, Proto::Ctrl::C_EOF);
                    // wait until receiver closes the connection
                    try
                    {
                        gu::byte_t b;
                        size_t n(asio::read(socket_, asio::buffer(&b, 1)));
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
