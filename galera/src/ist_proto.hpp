//
// Copyright (C) 2011-2014 Codership Oy <info@codership.com>
//

#ifndef GALERA_IST_PROTO_HPP
#define GALERA_IST_PROTO_HPP

//remove #include "ist_action.hpp"
#include "gcs.hpp"
#include "trx_handle.hpp"

#include "GCache.hpp"

#include "gu_logger.hpp"
#include "gu_serialize.hpp"
#include "gu_vector.hpp"

#include <string>

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
                T_NONE      = 0,
                T_HANDSHAKE = 1,
                T_HANDSHAKE_RESPONSE = 2,
                T_CTRL      = 3,
                T_TRX       = 4,
                T_CCHANGE   = 5,
                T_SKIP      = 6
            } Type;

            Message(int       version = -1,
                    Type      type    = T_NONE,
                    uint8_t   flags   = 0,
                    int8_t    ctrl    = 0,
                    uint32_t  len     = 0,
                    wsrep_seqno_t seqno = WSREP_SEQNO_UNDEFINED)
                :
                seqno_  (seqno  ),
                len_    (len    ),
                type_   (type   ),
                version_(version),
                flags_  (flags  ),
                ctrl_   (ctrl   )
            {}

            int      version() const { return version_; }
            Type     type()    const { return type_   ; }
            uint8_t  flags()   const { return flags_  ; }
            int8_t   ctrl()    const { return ctrl_   ; }
            uint32_t len()     const { return len_    ; }
            wsrep_seqno_t seqno() const { return seqno_; }

            void set_type_seqno(Type t, wsrep_seqno_t s)
            {
                type_ = t; seqno_ = s;
            }

            size_t serial_size() const
            {
                if (gu_likely(version_ >= 8))
                {
                    // header: version 1 byte, type 1 byte, flags 1 byte,
                    //         ctrl field 1 byte, length 4 bytes, seqno 8 bytes
                    return 4 + 4 + 8;
                }
                else
                {
                    // header: version 1 byte, type 1 byte, flags 1 byte,
                    //         ctrl field 1 byte, length 8 bytes
                    return 4 + 8;
                }
            }

            size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset)const
            {
                assert(version_ >= 4);
#ifndef NDEBUG
                size_t orig_offset(offset);
#endif // NDEBUG
                offset = gu::serialize1(uint8_t(version_), buf, buflen, offset);
                offset = gu::serialize1(uint8_t(type_), buf, buflen, offset);
                offset = gu::serialize1(flags_, buf, buflen, offset);
                offset = gu::serialize1(ctrl_,  buf, buflen, offset);

                if (gu_likely(version_ >= 8))
                {
                    offset = gu::serialize4(len_,   buf, buflen, offset);
                    offset = gu::serialize8(seqno_, buf, buflen, offset);
                }
                else /**/
                {
                    uint64_t const tmp(len_);
                    offset = gu::serialize8(tmp, buf, buflen, offset);
                }

                assert(offset - orig_offset == serial_size());

                return offset;
            }

            size_t unserialize(const gu::byte_t* buf, size_t buflen,
                               size_t offset)
            {
                assert(version_ >= 4);
#ifndef NDEBUG
                size_t orig_offset(offset);
#endif // NDEBUG
                uint8_t u8;
                offset = gu::unserialize1(buf, buflen, offset, u8);

                if (u8 != version_)
                {
                    gu_throw_error(EPROTO) << "invalid protocol version "
                                           << int(u8)
                                           << ", expected " << version_;
                }

//                version_ = u8;
                offset = gu::unserialize1(buf, buflen, offset, u8);
                type_  = static_cast<Message::Type>(u8);
                offset = gu::unserialize1(buf, buflen, offset, flags_);
                offset = gu::unserialize1(buf, buflen, offset, ctrl_);

                if (gu_likely(version_ >= 8))
                {
                    offset = gu::unserialize4(buf, buflen, offset, len_);
                    offset = gu::unserialize8(buf, buflen, offset, seqno_);
                }
                else
                {
                    uint64_t tmp;
                    offset = gu::unserialize8(buf, buflen, offset, tmp);
                    assert(tmp < std::numeric_limits<uint32_t>::max());
                    len_ = tmp;
                }

                assert(offset - orig_offset == serial_size());

                return offset;
            }

        private:

            wsrep_seqno_t seqno_;
            uint32_t len_;
            Type     type_;
            uint8_t  version_;
            uint8_t  flags_;
            int8_t   ctrl_;
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

        class Ordered : public Message
        {
        public:
#if 0 //remove
            Ordered(int version = -1,
                    Type type = T_NONE,
                    uint32_t len = 0,
                    wsrep_seqno_t seqno = WSREP_SEQNO_UNDEFINED)
                :
                Message(version, type, 0, 0, len, seqno)
            { }
#endif
            Ordered(int version,
                    Type type,
                    uint32_t len,
                    const wsrep_seqno_t& seqno)
                :
                Message(version, type, 0, 0, len, seqno)
            { }
        };


        class Proto
        {
        public:

            Proto(TrxHandleSlave::Pool& sp,
                  gcache::GCache&       gc,
                  int version, bool keep_keys)
                :
                trx_pool_ (sp),
                gcache_   (gc),
                raw_sent_ (0),
                real_sent_(0),
                version_  (version),
                keep_keys_(keep_keys)
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
            void send_ordered(ST&                           socket,
                              const gcache::GCache::Buffer& buffer)
            {
                Message::Type type(ordered_type(buffer));

                boost::array<asio::const_buffer, 3> cbs;
                size_t      payload_size; /* size of the 2nd cbs buffer */
                size_t      sent;

                galera::WriteSetIn ws;
                gu::Buf tmp = { buffer.ptr(), buffer.size() };
                int64_t seqno_d; // for proto ver < 8 compatibility

                if (gu_likely(Message::T_SKIP != type))
                {
                    assert(Message::T_TRX == type || version_ >= 8);

                    if (keep_keys_ || Message::T_CCHANGE == type ||
                        version_ < WS_NG_VERSION)
                    {
                        ws.read_header (tmp); // for seqno_d

                        payload_size = buffer.size();
                        const void* const ptr(buffer.ptr());
                        cbs[1] = asio::const_buffer(ptr, payload_size);
                        cbs[2] = asio::const_buffer(ptr, 0);
                    }
                    else
                    {
                        ws.read_buf (tmp, 0);

                        WriteSetIn::GatherVector out;
                        payload_size = ws.gather (out, false, false);
                        assert (2 == out->size());
                        cbs[1] = asio::const_buffer(out[0].ptr, out[0].size);
                        cbs[2] = asio::const_buffer(out[1].ptr, out[1].size);
                    }

                    assert(buffer.seqno_g() == ws.seqno());

                    seqno_d = buffer.seqno_g() - ws.pa_range();
                }
                else
                {
                    assert(Message::T_SKIP == type);
                    payload_size = 0;
                    seqno_d = WSREP_SEQNO_UNDEFINED;

                    /* in proto ver < 8 everything is T_TRX */
                    if (gu_unlikely(version_ < 8)) type = Message::T_TRX;
                }

                /* in version >= 8 metadata is included in Msg header, leaving
                 * it here for backward compatibility */
                size_t const trx_meta_size (version_ >= 8 ? 0 :
                                            (8 /* seqno_g */ + 8 /* seqno_d */));

                Ordered to_msg(version_, type, trx_meta_size + payload_size,
                               buffer.seqno_g());

                gu::Buffer buf(to_msg.serial_size() + trx_meta_size);
                size_t  offset(to_msg.serialize(&buf[0], buf.size(), 0));

                if (gu_unlikely(version_ < 8))
                {
                    offset = gu::serialize8(buffer.seqno_g(),
                                            &buf[0], buf.size(), offset);
                    offset = gu::serialize8(seqno_d,
                                            &buf[0], buf.size(), offset);
                }

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
            void
            recv_ordered(ST& socket, gcs_action& act)
            {
                act.seqno_g = 0;               // EOF
                // act.seqno_l has no significance
                act.buf     = NULL;            // skip
                act.size    = 0;               // skip
                act.type    = GCS_ACT_UNKNOWN; // EOF

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

                size_t offset(0);

                switch (msg.type())
                {
                case Message::T_TRX:
                case Message::T_CCHANGE:
                case Message::T_SKIP:
                {
                    if(gu_unlikely(version_ < 8)) // compatibility with 3.x
                    {
                        assert(msg.type() == Message::T_TRX);

                        int64_t seqno_g, seqno_d;

                        buf.resize(sizeof(seqno_g) + sizeof(seqno_d));

                        n = asio::read(socket, asio::buffer(&buf[0],buf.size()));
                        if (n != buf.size())
                        {
                            gu_throw_error(EPROTO) <<
                                "error reading trx meta data";
                        }

                        offset = gu::unserialize8(&buf[0],buf.size(),0,seqno_g);

                        if (gu_unlikely(seqno_g <= 0))
                        {
                            assert(0);
                            gu_throw_error(EINVAL)
                                << "non-positive sequence number " << seqno_g;
                        }

                        offset = gu::unserialize8(&buf[0], buf.size(), offset,
                                                  seqno_d);
                        if (gu_unlikely(seqno_d == WSREP_SEQNO_UNDEFINED &&
                                        offset != msg.len()))
                        {
                            assert(0);
                            gu_throw_error(EINVAL)
                                << "message size " << msg.len()
                                << " does not match expected size "<< offset;
                        }

                        Message::Type const type
                            (seqno_d >= 0 ? Message::T_TRX : Message::T_SKIP);

                        msg.set_type_seqno(type, seqno_g);
                    }
                    else
                    {
                        assert(msg.seqno() > 0);
                    } // end compatibility with 3.x

                    assert(msg.seqno() > 0);

                    void* wbuf;
                    size_t wsize;

                    if (gu_likely(msg.type() != Message::T_SKIP))
                    {
                        wsize = msg.len() - offset;
                        wbuf  = gcache_.malloc(wsize);

                        n = asio::read(socket, asio::buffer(wbuf, wsize));

                        if (gu_unlikely(n != wsize))
                        {
                            gu_throw_error(EPROTO)
                                << "error reading write set data";
                        }
                    }
                    else
                    {
                        wsize = GU_WORDSIZE/8; // 4/8 bytes
                        wbuf = gcache_.malloc(wsize);
                    }

                    gcache_.seqno_assign(wbuf, msg.seqno(),
//remove                                         seqno_d,
                                         gcs_type(msg.type()),
                                         msg.type() == Message::T_SKIP);

                    switch(msg.type())
                    {
                    case Message::T_TRX:
                    case Message::T_CCHANGE:
                        act.buf  = wbuf;           // not skip
                        act.size = wsize;
                    case Message::T_SKIP:
                        act.seqno_g = msg.seqno(); // not EOF
                        act.type    = gcs_type(msg.type());
                        break;
                    default:
                        assert(0);
                    };
#if 0 //remove
                    switch(msg.type())
                    {
                    case T_TRX:
                    case T_SKIP:
                    {
                        galera::TrxHandleSlave*
                            trx(galera::TrxHandleSlave::New(trx_pool_));

                        if (gu_likely(msg.type() == T_TRX))
                        {
                            // TODO: this should happen in parallel in applier
                            // threads, like it happens during normal replicaiton
                            trx->unserialize(static_cast<gu::byte_t*>(wbuf),
                                             wsize, 0);
                            assert(trx->global_seqno() == msg.seqno());
                            assert(trx->depends_seqno() >= 0);
                        }
                        else
                        {
                            assert(msg.type() == Message::T_SKIP);
                            trx->set_received(0, -1, msg.seqno());
                            trx->set_depends_seqno(WSREP_SEQNO_UNDEFINED);
                            trx->mark_certified();
                        }

                        if (gu_unlikely
                            (gu::Logger::no_log(gu::LOG_DEBUG) == false))
                        {
                            std::ostringstream os;
                            os << "received trx body: ";
                            os << *trx;
                            log_debug << os;
                        }

                        return Action(trx, Action::T_TRX);
                    }
                    case T_CCHANGE:
                    {
                        gcs_act_cchange* const cc(new gcs_act_cchange(wbuf,
                                                                      wsize));
                        return Action(cc, Action::T_CC);
                    }
                    default:
                        assert(0);
                        throw std::runtime_error;
                    }; /* switch(msg.type()) */
#endif //remove
                    return;
                }
                case Message::T_CTRL:
                    switch (msg.ctrl())
                    {
                    case Ctrl::C_EOF:
                        return;
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
//remove                return Action(NULL, Action::T_EOF); // keep compiler happy
            }

        private:

            TrxHandleSlave::Pool& trx_pool_;

            gcache::GCache& gcache_;

            uint64_t raw_sent_;
            uint64_t real_sent_;
            int      version_;
            bool     keep_keys_;

            Message::Type ordered_type(const gcache::GCache::Buffer& buf)
            {
                assert(buf.type() == GCS_ACT_WRITESET ||
                       buf.type() == GCS_ACT_CCHANGE);

                if (gu_likely(!buf.skip()))
                {
                    switch (buf.type())
                    {
                    case GCS_ACT_WRITESET:
                        return Message::T_TRX;
                    case GCS_ACT_CCHANGE:
                        return (version_ >= 8 ?
                                Message::T_CCHANGE : Message::T_SKIP);
                    default:
                        log_error << "Unsupported message type from cache. "
                                  << "Skipping seqno " << buf.seqno_g();
                        assert(0);
                        return  Message::T_SKIP;
                    }
                }
                else
                {
                    return Message::T_SKIP;
                }
            }

            gcs_act_type
            gcs_type(Message::Type t) const
            {
                switch(t)
                {
                case Message::T_TRX:
                case Message::T_SKIP:
                    return GCS_ACT_WRITESET;
                case Message::T_CCHANGE:
                    return GCS_ACT_CCHANGE;
                };
            }
        };
    }
}

#endif // GALERA_IST_PROTO_HPP
