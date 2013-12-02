//
// Copyright (C) 2011-2013 Codership Oy <info@codership.com>
//

#ifndef GALERA_IST_PROTO_HPP
#define GALERA_IST_PROTO_HPP

#include "trx_handle.hpp"

#include "GCache.hpp"

#include "gu_logger.hpp"
#include "gu_serialize.hpp"
#include "gu_vector.hpp"

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

            int      version() const { return version_; }
            Type     type()    const { return type_   ; }
            uint8_t  flags()   const { return flags_  ; }
            int8_t   ctrl()    const { return ctrl_   ; }
            uint64_t len()     const { return len_    ; }

            size_t serial_size() const
            {
                if (version_ > 3)
                {
                    // header: version 1 byte, type 1 byte, flags 1 byte,
                    //         ctrl field 1 byte
                    return 4 + sizeof(len_);
                }
                else
                {
                    return sizeof(*this);
                }
            }

            size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset)const
            {
#ifndef NDEBUG
                size_t orig_offset(offset);
#endif // NDEBUG
                if (version_ > 3)
                {
                    offset = gu::serialize1(uint8_t(version_),
                                            buf, buflen, offset);
                    offset = gu::serialize1(uint8_t(type_),
                                            buf, buflen, offset);
                    offset = gu::serialize1(flags_, buf, buflen, offset);
                    offset = gu::serialize1(ctrl_,  buf, buflen, offset);
                    offset = gu::serialize8(len_,   buf, buflen, offset);
                }
                else
                {
                    if (buflen < offset + sizeof(*this))
                    {
                        gu_throw_error(EMSGSIZE) << "buffer too short";
                    }

                    *reinterpret_cast<Message*>(buf + offset) = *this;
                    offset += sizeof(*this);
                }

                assert((version_ > 3 && offset - orig_offset == 12) ||
                       (offset - orig_offset == sizeof(*this)));

                return offset;
            }

            size_t unserialize(const gu::byte_t* buf, size_t buflen,
                               size_t offset)
            {
                assert(version_ >= 0);
#ifndef NDEBUG
                size_t orig_offset(offset);
#endif // NDEBUG
                uint8_t u8;
                if (version_ > 3)
                {
                    offset = gu::unserialize1(buf, buflen, offset, u8);
                }
                else
                {
                    u8 = *reinterpret_cast<const int*>(buf + offset);
                }

                if (u8 != version_)
                {
                    gu_throw_error(EPROTO) << "invalid protocol version "
                                           << int(u8)
                                           << ", expected " << version_;
                }

                if (u8 > 3)
                {
                    version_ = u8;
                    offset = gu::unserialize1(buf, buflen, offset, u8);
                    type_  = static_cast<Message::Type>(u8);
                    offset = gu::unserialize1(buf, buflen, offset, flags_);
                    offset = gu::unserialize1(buf, buflen, offset, ctrl_);
                    offset = gu::unserialize8(buf, buflen, offset, len_);
                }
                else
                {
                    if (buflen < offset + sizeof(*this))
                    {
                        gu_throw_error(EMSGSIZE)
                            <<" buffer too short for version " << version_
                            << ": " << buflen << " " << offset << " "
                            << sizeof(*this);
                    }

                    *this = *reinterpret_cast<const Message*>(buf + offset);
                    offset += sizeof(*this);
                }

                assert((version_ > 3 && offset - orig_offset == 12) ||
                       (offset - orig_offset == sizeof(*this)));

                return offset;
            }

        private:

            int      version_; // unfortunately for compatibility with older
                               // versions we must leave it as int (4 bytes)
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
                Handshake  hs(version_);
                gu::Buffer buf(hs.serial_size());
                size_t offset(hs.serialize(&buf[0], buf.size(), 0));
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
                Message    msg(version_);
                gu::Buffer buf(msg.serial_size());
                size_t n(asio::read(socket, asio::buffer(&buf[0], buf.size())));

                if (n != buf.size())
                {
                    gu_throw_error(EPROTO) << "error receiving handshake";
                }

                (void)msg.unserialize(&buf[0], buf.size(), 0);

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
                    default:
                        gu_throw_error(EPROTO) << "unexpected ctrl code: " <<
                            msg.ctrl();
                    }
                    break;
                default:
                    gu_throw_error(EPROTO)
                        << "unexpected message type: " << msg.type();
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
                gu::Buffer buf(hsr.serial_size());
                size_t offset(hsr.serialize(&buf[0], buf.size(), 0));
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
                Message    msg(version_);
                gu::Buffer buf(msg.serial_size());
                size_t n(asio::read(socket, asio::buffer(&buf[0], buf.size())));

                if (n != buf.size())
                {
                    gu_throw_error(EPROTO) << "error receiving handshake";
                }

                (void)msg.unserialize(&buf[0], buf.size(), 0);

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
                    default:
                        gu_throw_error(EPROTO) << "unexpected ctrl code: "
                                               << msg.ctrl();
                    }
                default:
                    gu_throw_error(EINVAL) << "unexpected message type: "
                                           << msg.type();
                }
            }

            template <class ST>
            void send_ctrl(ST& socket, int8_t code)
            {
                Ctrl       ctrl(version_, code);
                gu::Buffer buf(ctrl.serial_size());
                size_t offset(ctrl.serialize(&buf[0], buf.size(), 0));
                size_t n(asio::write(socket, asio::buffer(&buf[0],buf.size())));
                if (n != offset)
                {
                    gu_throw_error(EPROTO) << "error sending ctrl message";
                }
            }

            template <class ST>
            int8_t recv_ctrl(ST& socket)
            {
                Message    msg(version_);
                gu::Buffer buf(msg.serial_size());
                size_t n(asio::read(socket, asio::buffer(&buf[0], buf.size())));

                if (n != buf.size())
                {
                    gu_throw_error(EPROTO) << "error receiving handshake";
                }

                (void)msg.unserialize(&buf[0], buf.size(), 0);

                log_debug << "msg: " << msg.version() << " " << msg.type()
                          << " " << msg.len();

                switch (msg.type())
                {
                case Message::T_CTRL:
                    break;
                default:
                    gu_throw_error(EPROTO) << "unexpected message type: "
                                           << msg.type();
                }
                return msg.ctrl();
            }


            template <class ST>
            void send_trx(ST&                           socket,
                          const gcache::GCache::Buffer& buffer)
            {
                const bool rolled_back(buffer.seqno_d() == -1);

                galera::WriteSetIn ws;
                boost::array<asio::const_buffer, 3> cbs;
                size_t      payload_size; /* size of the 2nd cbs buffer */
                size_t      sent;

                if (gu_unlikely(rolled_back))
                {
                    payload_size = 0;
                }
                else
                {
                    if (keep_keys_ || version_ < WS_NG_VERSION)
                    {
                        payload_size = buffer.size();
                        const void* const ptr(buffer.ptr());
                        cbs[1] = asio::const_buffer(ptr, payload_size);
                        cbs[2] = asio::const_buffer(ptr, 0);
                    }
                    else
                    {
                        gu::Buf tmp = { buffer.ptr(), buffer.size() };
                        ws.read_buf (tmp, 0);

                        WriteSetIn::GatherVector out;
                        payload_size = ws.gather (out, false, false);
                        assert (2 == out->size());
                        cbs[1] = asio::const_buffer(out[0].ptr, out[0].size);
                        cbs[2] = asio::const_buffer(out[1].ptr, out[1].size);
                    }
                }

                size_t const trx_meta_size(
                    8 /* serial_size(buffer.seqno_g()) */ +
                    8 /* serial_size(buffer.seqno_d()) */
                    );

                Trx trx_msg(version_, trx_meta_size + payload_size);

                gu::Buffer buf(trx_msg.serial_size() + trx_meta_size);
                size_t  offset(trx_msg.serialize(&buf[0], buf.size(), 0));

                offset = gu::serialize8(buffer.seqno_g(),
                                        &buf[0], buf.size(), offset);
                offset = gu::serialize8(buffer.seqno_d(),
                                        &buf[0], buf.size(), offset);
                cbs[0] = asio::const_buffer(&buf[0], buf.size());

                if (gu_likely(payload_size))
                {
                    sent = asio::write(socket, cbs);
                }
                else
                {
                    sent = asio::write(socket, asio::buffer(cbs[0]));
                }

                log_debug << "sent " << sent << " bytes";
            }


            template <class ST>
            galera::TrxHandle*
            recv_trx(ST& socket)
            {
                Message    msg(version_);
                gu::Buffer buf(msg.serial_size());
                size_t n(asio::read(socket, asio::buffer(&buf[0], buf.size())));

                if (n != buf.size())
                {
                    gu_throw_error(EPROTO) << "error receiving trx header";
                }

                (void)msg.unserialize(&buf[0], buf.size(), 0);

                log_debug << "received header: " << n << " bytes, type "
                          << msg.type() << " len " << msg.len();

                switch (msg.type())
                {
                case Message::T_TRX:
                {
                    // TODO: ideally we want to make seqno_g and cert verdict
                    // be a part of msg object above, so that we can skip this
                    // read. The overhead is tiny given that vast majority of
                    // messages will be trx writesets.
                    wsrep_seqno_t seqno_g, seqno_d;

                    buf.resize(sizeof(seqno_g) + sizeof(seqno_d));

                    n = asio::read(socket, asio::buffer(&buf[0], buf.size()));
                    if (n != buf.size())
                    {
                        gu_throw_error(EPROTO) << "error reading trx meta data";
                    }

                    size_t offset(gu::unserialize8(&buf[0], buf.size(), 0,
                                                   seqno_g));
                    offset = gu::unserialize8(&buf[0], buf.size(), offset,
                                              seqno_d);

                    galera::TrxHandle* trx(new galera::TrxHandle);

                    if (seqno_d == WSREP_SEQNO_UNDEFINED)
                    {
                        if (offset != msg.len())
                        {
                            gu_throw_error(EINVAL)
                                << "message size " << msg.len()
                                << " does not match expected size " << offset;
                        }
                    }
                    else
                    {
                        MappedBuffer& wbuf(trx->write_set_collection());
                        size_t const wsize(msg.len() - offset);
                        wbuf.resize(wsize);

                        n = asio::read(socket,
                                       asio::buffer(&wbuf[0], wbuf.size()));

                        if (gu_unlikely(n != wbuf.size()))
                        {
                            gu_throw_error(EPROTO)
                                << "error reading write set data";
                        }

                        trx->unserialize(&wbuf[0], wbuf.size(), 0);
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
                        }
                        else
                        {
                            gu_throw_error(-msg.ctrl()) <<"peer reported error";
                        }
                    }
                default:
                    gu_throw_error(EPROTO) << "unexpected message type: "
                                           << msg.type();
                }

                gu_throw_fatal; throw;
                return 0; // keep compiler happy
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
