//
// Copyright (C) 2011 Codership Oy <info@codership.com>
//

#ifndef GALERA_IST_PROTO_HPP
#define GALERA_IST_PROTO_HPP

#include "serialization.hpp"
#include "trx_handle.hpp"
#include "gu_logger.hpp"
#include "GCache.hpp"

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

//
// Note about protocol/message versioning:
// Version is determined by GCS and IST protocol is initialized in total
// order. Therefore it is not necessary to negotiate version at IST level,
// it should be enough to check that message version numbers match.
//


namespace galera
{
    namespace ist
    {
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
            Message(int       version = -1,
                    Type      type    = T_NONE,
                    uint8_t   flags   = 0,
                    int8_t    ctrl    = 0,
                    uint64_t  len     = 0)
                :
                version_(version),
                type_   (type   ),
                flags_  (flags  ),
                ctrl_   (ctrl   ),
                len_    (len    )
            { }
            int  version()  const { return version_; }
            Type    type()  const { return type_   ; }
            uint8_t flags() const { return flags_  ; }
            int8_t  ctrl()  const { return ctrl_   ; }
            uint64_t len()  const { return len_    ; }

        private:
            friend size_t serial_size(const Message&);
            friend size_t serialize(const Message&, gu::byte_t*,
                                    size_t, size_t);
            friend size_t unserialize(const gu::byte_t*, size_t, size_t,
                                      Message&);

            int      version_;
            Type     type_;
            uint8_t  flags_;
            int8_t   ctrl_;
            uint64_t len_;
        };

        class Handshake : public Message
        {
        public:
            Handshake(int version = -1)
                :
                Message(version, Message::T_HANDSHAKE, 0, 0, 0)
            { }
        };
        class HandshakeResponse : public Message
        {
        public:
            HandshakeResponse(int version = -1)
                :
                Message(version, Message::T_HANDSHAKE_RESPONSE, 0, 0, 0)
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
            Ctrl(int version = -1, int8_t code = 0)
                :
                Message(version, Message::T_CTRL, 0, code, 0)
            { }
        };
        class Trx : public Message
        {
        public:
            Trx(int version = -1, uint64_t len = 0)
                :
                Message(version, Message::T_TRX, 0, 0, len)
            { }
        };


        inline size_t serial_size(const Message& msg)
        {
            if (msg.version_ > 3)
            {
                // header: version 1 byte, type 1 byte, flags 1 byte,
                //         ctrl field 1 byte
                return 4 + sizeof(msg.len_);
            }
            else
            {
                return sizeof(msg);
            }
        }

        inline size_t serialize(const Message& msg,
                                gu::byte_t* buf,
                                size_t buflen, size_t offset)
        {
#ifndef NDEBUG
            size_t orig_offset(offset);
#endif // NDEBUG
            if (msg.version_ > 3)
            {
                offset = galera::serialize(static_cast<uint8_t>(msg.version_),
                                           buf, buflen, offset);
                offset = galera::serialize(static_cast<uint8_t>(msg.type_),
                                           buf, buflen, offset);
                offset = galera::serialize(msg.flags_, buf, buflen, offset);
                offset = galera::serialize(msg.ctrl_, buf, buflen, offset);
                offset = galera::serialize(msg.len(), buf, buflen, offset);
            }
            else
            {
                if (buflen < offset + sizeof(msg))
                {
                    gu_throw_error(EMSGSIZE) << "buffer too short";
                }
                *reinterpret_cast<Message*>(buf + offset) = msg;
                offset += sizeof(msg);
            }
            assert((msg.version_ > 3 && offset - orig_offset == 12) ||
                   (offset - orig_offset == sizeof(msg)));

            return offset;
        }

        inline size_t unserialize(const gu::byte_t* buf,
                                  size_t buflen,
                                  size_t offset,
                                  Message& msg)
        {
            assert(msg.version_ >= 0);
#ifndef NDEBUG
            size_t orig_offset(offset);
#endif // NDEBUG
            uint8_t u8;
            offset = galera::unserialize(buf, buflen, offset, u8);
            if (u8 != msg.version_)
            {
                gu_throw_error(EPROTO) << "invalid protocol version "
                                       << static_cast<int>(u8)
                                       << ", expected " << msg.version_;
            }
            if (u8 > 3)
            {
                msg.version_ = u8;
                offset = galera::unserialize(buf, buflen, offset, u8);
                msg.type_ = static_cast<Message::Type>(u8);
                offset = galera::unserialize(buf, buflen, offset, msg.flags_);
                offset = galera::unserialize(buf, buflen, offset, msg.ctrl_);
                offset = galera::unserialize(buf, buflen, offset, msg.len_);
            }
            else
            {
                // Decrement offset by one to revert adjustment by
                // version number unserialization.
                offset -= 1;
                if (buflen < offset + sizeof(msg))
                {
                    gu_throw_error(EMSGSIZE) << "buffer too short for version "
                                             << msg.version_ << ": "
                                             << buflen << " " << offset
                                             << " " << sizeof(msg);
                }
                msg = *reinterpret_cast<const Message*>(buf + offset);
                offset += sizeof(msg);
            }
            assert((msg.version_ > 3 && offset - orig_offset == 12) ||
                   (offset - orig_offset == sizeof(msg)));

            return offset;
        }

        class Proto
        {
        public:

            Proto(int version, bool keep_keys)
                :
                version_(version),
                keep_keys_(keep_keys),
                raw_sent_(0),
                real_sent_(0)
            { }

            ~Proto()
            {
                if (raw_sent_ > 0)
                {
                    log_info << "ist proto finished, raw sent: "
                             << raw_sent_
                             << " real sent: "
                             << real_sent_
                             << " frac: "
                             << (raw_sent_ == 0 ? 0. :
                                 static_cast<double>(real_sent_)/raw_sent_);
                }
            }

            template <class ST>
            void send_handshake(ST& socket)
            {
                Handshake hs(version_);
                gu::Buffer buf(serial_size(hs));
                size_t offset(serialize(hs, &buf[0], buf.size(), 0));
                size_t n(asio::write(socket, asio::buffer(&buf[0],
                                                          buf.size())));
                if (n != offset)
                {
                    gu_throw_error(EPROTO) << "error sending handshake";
                }
            }

            template <class ST>
            void recv_handshake(ST& socket)
            {
                Message msg(version_);
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
                Message msg(version_);
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
            void send_ctrl(ST& socket, int8_t code)
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
            int8_t recv_ctrl(ST& socket)
            {
                Message msg(version_);
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
                const size_t trx_meta_size(
                    galera::serial_size(buffer.seqno_g())
                    + galera::serial_size(buffer.seqno_d()));
                const bool rolled_back(buffer.seqno_d() == -1);

                size_t n;
                if (rolled_back == true)
                {
                    Trx trx_msg(version_, trx_meta_size);
                    gu::Buffer buf(serial_size(trx_msg) + trx_meta_size);
                    size_t offset(serialize(trx_msg, &buf[0], buf.size(), 0));
                    offset = galera::serialize(buffer.seqno_g(),
                                               &buf[0], buf.size(), offset);
                    offset = galera::serialize(buffer.seqno_d(),
                                               &buf[0], buf.size(), offset);
                    n = asio::write(socket, asio::buffer(&buf[0], buf.size()));
                }
                else if (keep_keys_ == true)
                {
                    Trx trx_msg(version_, trx_meta_size + buffer.size());
                    gu::Buffer buf(serial_size(trx_msg) + trx_meta_size);
                    size_t offset(serialize(trx_msg, &buf[0], buf.size(), 0));
                    offset = galera::serialize(buffer.seqno_g(),
                                               &buf[0], buf.size(), offset);
                    offset = galera::serialize(buffer.seqno_d(),
                                               &buf[0], buf.size(), offset);
                    boost::array<asio::const_buffer, 2> cbs;
                    cbs[0] = asio::const_buffer(&buf[0], buf.size());
                    cbs[1] = asio::const_buffer(buffer.ptr(), buffer.size());
                    n = asio::write(socket, cbs);
                }
                else
                {
                    class AutoRelease
                    {
                    public:
                        AutoRelease(TrxHandle* trx) : trx_(trx) { }
                        ~AutoRelease() { trx_->unref(); }
                        TrxHandle* trx() { return trx_; }
                    private:
                        AutoRelease(const AutoRelease&);
                        void operator=(const AutoRelease&);
                        TrxHandle* trx_;
                    };
                    // reconstruct trx without keys
                    AutoRelease ar(new TrxHandle);
                    galera::TrxHandle* trx(ar.trx());
                    const gu::byte_t* const ptr(
                        reinterpret_cast<const gu::byte_t*>(buffer.ptr()));
                    size_t offset(galera::unserialize(ptr,
                                                      buffer.size(), 0, *trx));
                    while (offset < static_cast<size_t>(buffer.size()))
                    {
                        // skip over keys
                        uint32_t len;
                        offset = galera::unserialize(
                            ptr, buffer.size(), offset, len);
                        offset += len;
                        offset = galera::unserialize(
                            ptr, buffer.size(), offset, len);
                        if (offset + len > static_cast<size_t>(buffer.size()))
                        {
                            gu_throw_error(ERANGE)
                                << (offset + len) << " > " << buffer.size();
                        }
                        trx->append_data(ptr + offset, len);
                        offset += len;
                    }
                    trx->flush(0);

                    Trx trx_msg(version_, trx_meta_size
                                + trx->write_set_collection().size());
                    gu::Buffer buf(serial_size(trx_msg) + trx_meta_size);
                    offset = serialize(trx_msg, &buf[0], buf.size(), 0);
                    offset = galera::serialize(buffer.seqno_g(),
                                               &buf[0], buf.size(), offset);
                    offset = galera::serialize(buffer.seqno_d(),
                                               &buf[0], buf.size(), offset);
                    boost::array<asio::const_buffer, 2> cbs;
                    cbs[0] = asio::const_buffer(&buf[0], buf.size());
                    cbs[1] = asio::const_buffer(
                        &trx->write_set_collection()[0],
                        trx->write_set_collection().size());
                    raw_sent_ += buffer.size();
                    real_sent_ += trx->write_set_collection().size();
                    n = asio::write(socket, cbs);
                }
                log_debug << "sent " << n << " bytes";
            }


            template <class ST>
            galera::TrxHandle*
            recv_trx(ST& socket)
            {
                Message msg(version_);
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
            int  version_;
            bool keep_keys_;
            uint64_t raw_sent_;
            uint64_t real_sent_;
        };
    }
}

#endif // GALERA_IST_PROTO_HPP
