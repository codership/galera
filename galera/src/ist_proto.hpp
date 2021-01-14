//
// Copyright (C) 2011-2019 Codership Oy <info@codership.com>
//

#ifndef GALERA_IST_PROTO_HPP
#define GALERA_IST_PROTO_HPP

#include "gcs.hpp"
#include "trx_handle.hpp"

#include "GCache.hpp"

#include "gu_asio.hpp"
#include "gu_logger.hpp"
#include "gu_serialize.hpp"
#include "gu_vector.hpp"
#include "gu_array.hpp"

#include <string>

//
// Message class must have non-virtual destructor until
// support up to version 3 is removed as serialization/deserialization
// depends on the size of the class.
//

#include "gu_disable_non_virtual_dtor.hpp"

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
        static int const VER21 = 4;
        static int const VER40 = 10;

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

            typedef enum
            {
                F_PRELOAD = 0x1
            } Flag;

            explicit
            Message(int       version,
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

            int           version() const { return version_; }
            Type          type()    const { return type_   ; }
            uint8_t       flags()   const { return flags_  ; }
            int8_t        ctrl()    const { return ctrl_   ; }
            uint32_t      len()     const { return len_    ; }
            wsrep_seqno_t seqno()   const { return seqno_  ; }

            void set_type_seqno(Type t, wsrep_seqno_t s)
            {
                type_ = t; seqno_ = s;
            }

            ~Message() { }

            size_t serial_size() const
            {
                if (gu_likely(version_ >= VER40))
                {
                    // header: version 1 byte, type 1 byte, flags 1 byte,
                    //         ctrl field 1 byte, length 4 bytes, seqno 8 bytes
                    return 4 + 4 + 8 + sizeof(checksum_t);
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
                assert(version_ >= VER21);

                size_t const orig_offset(offset);

                offset = gu::serialize1(uint8_t(version_), buf, buflen, offset);
                offset = gu::serialize1(uint8_t(type_), buf, buflen, offset);
                offset = gu::serialize1(flags_, buf, buflen, offset);
                offset = gu::serialize1(ctrl_,  buf, buflen, offset);

                if (gu_likely(version_ >= VER40))
                {
                    offset = gu::serialize4(len_,   buf, buflen, offset);
                    offset = gu::serialize8(seqno_, buf, buflen, offset);

                    *reinterpret_cast<checksum_t*>(buf + offset) =
                        htog_checksum(buf + orig_offset, offset - orig_offset);

                    offset += sizeof(checksum_t);
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
                assert(version_ >= VER21);

                size_t orig_offset(offset);

                uint8_t u8;
                offset = gu::unserialize1(buf, buflen, offset, u8);

                if (gu_unlikely(u8 != version_)) throw_invalid_version(u8);

                offset = gu::unserialize1(buf, buflen, offset, u8);
                type_  = static_cast<Message::Type>(u8);
                offset = gu::unserialize1(buf, buflen, offset, flags_);
                offset = gu::unserialize1(buf, buflen, offset, ctrl_);

                if (gu_likely(version_ >= VER40))
                {
                    offset = gu::unserialize4(buf, buflen, offset, len_);
                    offset = gu::unserialize8(buf, buflen, offset, seqno_);

                    checksum_t const computed(htog_checksum(buf + orig_offset,
                                                            offset-orig_offset));
                    const checksum_t* expected
                        (reinterpret_cast<const checksum_t*>(buf + offset));

                    if (gu_unlikely(computed != *expected))
                        throw_corrupted_header();

                    offset += sizeof(checksum_t);
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

            typedef uint64_t checksum_t;

            // returns endian-adjusted checksum of buf
            static checksum_t
            htog_checksum(const void* const buf, size_t const size)
            {
                return
                    gu::htog<checksum_t>(gu::FastHash::digest<checksum_t>(buf,
                                                                          size));
            }

            void throw_invalid_version(uint8_t v);
            void throw_corrupted_header();
        };

        std::ostream& operator<< (std::ostream& os, const Message& m);

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
            Ordered(int      version,
                    Type     type,
                    uint8_t  flags,
                    uint32_t len,
                    wsrep_seqno_t const seqno)
                :
                Message(version, type, flags, 0, len, seqno)
            { }
        };


        class Proto
        {
        public:

            Proto(gcache::GCache&       gc,
                  int version, bool keep_keys)
                :
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

            void send_handshake(gu::AsioSocket& socket)
            {
                Handshake  hs(version_);
                gu::Buffer buf(hs.serial_size());
                size_t offset(hs.serialize(&buf[0], buf.size(), 0));
                size_t n(socket.write(gu::AsioConstBuffer(&buf[0], buf.size())));
                if (n != offset)
                {
                    gu_throw_error(EPROTO) << "error sending handshake";
                }
            }

            void recv_handshake(gu::AsioSocket& socket)
            {
                Message    msg(version_);
                gu::Buffer buf(msg.serial_size());
                size_t n(socket.read(gu::AsioMutableBuffer(&buf[0], buf.size())));

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

            void send_handshake_response(gu::AsioSocket& socket)
            {
                HandshakeResponse hsr(version_);
                gu::Buffer buf(hsr.serial_size());
                size_t offset(hsr.serialize(&buf[0], buf.size(), 0));
                size_t n(socket.write(gu::AsioConstBuffer(&buf[0], buf.size())));
                if (n != offset)
                {
                    gu_throw_error(EPROTO)
                        << "error sending handshake response";
                }
            }

            void recv_handshake_response(gu::AsioSocket& socket)
            {
                Message    msg(version_);
                gu::Buffer buf(msg.serial_size());
                size_t n(socket.read(gu::AsioMutableBuffer(&buf[0], buf.size())));

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

            void send_ctrl(gu::AsioSocket& socket, int8_t code)
            {
                Ctrl       ctrl(version_, code);
                gu::Buffer buf(ctrl.serial_size());
                size_t offset(ctrl.serialize(&buf[0], buf.size(), 0));
                size_t n(socket.write(gu::AsioConstBuffer(&buf[0], buf.size())));
                if (n != offset)
                {
                    gu_throw_error(EPROTO) << "error sending ctrl message";
                }
            }

            int8_t recv_ctrl(gu::AsioSocket& socket)
            {
                Message    msg(version_);
                gu::Buffer buf(msg.serial_size());
                size_t n(socket.read(gu::AsioMutableBuffer(&buf[0], buf.size())));

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

            void send_ordered(gu::AsioSocket&                           socket,
                              const gcache::GCache::Buffer& buffer,
                              bool const                    preload_flag)
            {
                Message::Type type(ordered_type(buffer));

                std::array<gu::AsioConstBuffer, 3> cbs;

                size_t      payload_size; /* size of the 2nd cbs buffer */
                size_t      sent;

                // for proto ver < VER40 compatibility
                int64_t seqno_d(WSREP_SEQNO_UNDEFINED);

                if (gu_likely(Message::T_SKIP != type))
                {
                    assert(Message::T_TRX == type || version_ >= VER40);

                    galera::WriteSetIn ws;
                    gu::Buf tmp = { buffer.ptr(), buffer.size() };

                    if (keep_keys_ || Message::T_CCHANGE == type)
                    {
                        payload_size = buffer.size();
                        const void* const ptr(buffer.ptr());
                        cbs[1] = gu::AsioConstBuffer(ptr, payload_size);
                        cbs[2] = gu::AsioConstBuffer(ptr, 0);

                        if (gu_likely(Message::T_TRX == type)) // compatibility
                        {
                            ws.read_header (tmp);
                            seqno_d = buffer.seqno_g() - ws.pa_range();
                            assert(buffer.seqno_g() == ws.seqno());
                        }
                    }
                    else
                    {
                        ws.read_buf (tmp, 0);

                        WriteSetIn::GatherVector out;
                        payload_size = ws.gather (out, false, false);
                        assert (2 == out->size());
                        cbs[1] = gu::AsioConstBuffer(out[0].ptr, out[0].size);
                        cbs[2] = gu::AsioConstBuffer(out[1].ptr, out[1].size);

                        seqno_d = buffer.seqno_g() - ws.pa_range();

                        assert(buffer.seqno_g() == ws.seqno());
                    }
                }
                else
                {
                    assert(Message::T_SKIP == type);
                    payload_size = 0;
                    seqno_d = WSREP_SEQNO_UNDEFINED;

                    /* in proto ver < VER40 everything is T_TRX */
                    if (gu_unlikely(version_ < VER40)) type = Message::T_TRX;
                }

                /* in version >= 3 metadata is included in Msg header, leaving
                 * it here for backward compatibility */
                size_t const trx_meta_size(version_ >= VER40 ? 0 :
                                           (8 /* seqno_g */ + 8 /* seqno_d */));

                uint8_t const msg_flags((version_ >= VER40 && preload_flag) ?
                                        Message::F_PRELOAD : 0);

                Ordered to_msg(version_, type, msg_flags,
                               trx_meta_size + payload_size, buffer.seqno_g());

                gu::Buffer buf(to_msg.serial_size() + trx_meta_size);
                size_t  offset(to_msg.serialize(&buf[0], buf.size(), 0));

                if (gu_unlikely(version_ < VER40))
                {
                    offset = gu::serialize8(buffer.seqno_g(),
                                            &buf[0], buf.size(), offset);
                    offset = gu::serialize8(seqno_d,
                                            &buf[0], buf.size(), offset);
                }

                cbs[0] = gu::AsioConstBuffer(&buf[0], buf.size());

                if (gu_likely(payload_size))
                {
                    sent = gu::write(socket, cbs);
                }
                else
                {
                    sent = socket.write(cbs[0]);
                }

                log_debug << "sent " << sent << " bytes";
            }

            void skip_bytes(gu::AsioSocket& socket, size_t bytes)
            {
                gu::Buffer buf(4092);
                while (bytes > 0)
                {
                    bytes -= socket.read(
                        gu::AsioMutableBuffer(
                            &buf[0], std::min(buf.size(), bytes)));
                }
                assert(bytes == 0);
            }

            void
            recv_ordered(gu::AsioSocket& socket,
                         std::pair<gcs_action, bool>& ret)
            {
                gcs_action& act(ret.first);

                act.seqno_g = 0;               // EOF
                // act.seqno_l has no significance
                act.buf     = NULL;            // skip
                act.size    = 0;               // skip
                act.type    = GCS_ACT_UNKNOWN; // EOF

                Message    msg(version_);
                gu::Buffer buf(msg.serial_size());
                size_t n(socket.read(gu::AsioMutableBuffer(&buf[0], buf.size())));

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
                case Message::T_CCHANGE:
                case Message::T_SKIP:
                {
                    size_t  offset(0);
                    int64_t seqno_g(msg.seqno());  // compatibility with 3.x

                    if (gu_unlikely(version_ < VER40)) //compatibility with 3.x
                    {
                        assert(msg.type() == Message::T_TRX);

                        int64_t seqno_d;

                        buf.resize(sizeof(seqno_g) + sizeof(seqno_d));

                        n = socket.read(gu::AsioMutableBuffer(&buf[0],buf.size()));
                        if (n != buf.size())
                        {
                            assert(0);
                            gu_throw_error(EPROTO)
                                << "error reading trx meta data";
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
                    else  // end compatibility with 3.x
                    {
                        assert(seqno_g > 0);
                    }

                    assert(msg.seqno() > 0);

                    /* Backward compatibility code above could change msg type.
                     * but it should not change below. Saving const for later
                     * assert(). */
                    Message::Type const msg_type(msg.type());
                    gcs_act_type  const gcs_type
                        (msg_type == Message::T_CCHANGE ?
                         GCS_ACT_CCHANGE : GCS_ACT_WRITESET);

                    const void* wbuf;
                    ssize_t     wsize;
                    bool        already_cached(false);

                    // Check if cert index preload trx is already in gcache.
                    if ((msg.flags() & Message::F_PRELOAD))
                    {
                        ret.second = true;

                        try
                        {
                            wbuf = gcache_.seqno_get_ptr(seqno_g, wsize);

                            skip_bytes(socket, msg.len() - offset);

                            already_cached = true;
                        }
                        catch (gu::NotFound& nf)
                        {
                            // not found from gcache, continue as normal
                        }
                    }

                    if (!already_cached)
                    {
                        if (gu_likely(msg_type != Message::T_SKIP))
                        {
                            wsize = msg.len() - offset;

                            void*   const ptr(gcache_.malloc(wsize));
                            ssize_t const r
                                (socket.read(gu::AsioMutableBuffer(ptr, wsize)));

                            if (gu_unlikely(r != wsize))
                            {
                                gu_throw_error(EPROTO)
                                    << "error reading write set data, "
                                    << "expected " << wsize
                                    << " bytes, got " << r << " bytes";
                            }

                            wbuf = ptr;
                        }
                        else
                        {
                            wsize = GU_WORDSIZE/8; // bits to bytes
                            wbuf  = gcache_.malloc(wsize);
                        }

                        gcache_.seqno_assign(wbuf, msg.seqno(), gcs_type,
                                             msg_type == Message::T_SKIP);
                    }

                    assert(msg.type() == msg_type);

                    switch(msg_type)
                    {
                    case Message::T_TRX:
                    case Message::T_CCHANGE:
                        act.buf  = wbuf;           // not skip
                        act.size = wsize;
                        // fall through
                    case Message::T_SKIP:
                        act.seqno_g = msg.seqno(); // not EOF
                        act.type    = gcs_type;
                        break;
                    default:
                        gu_throw_error(EPROTO) << "Unrecognized message type"
                                               << msg_type;
                    }

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
            }

        private:

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
                        return (version_ >= VER40 ?
                                Message::T_CCHANGE : Message::T_SKIP);
                    default:
                        log_error << "Unsupported message type from cache: "
                                  << buf.type()
                                  << ". Skipping seqno " << buf.seqno_g();
                        assert(0);
                        return  Message::T_SKIP;
                    }
                }
                else
                {
                    return Message::T_SKIP;
                }
            }
        };
    }
}

#include "gu_enable_non_virtual_dtor.hpp"

#endif // GALERA_IST_PROTO_HPP
