//
// Copyright (C) 2013-2017 Codership Oy <info@codership.com>
//

/*
 * Planned writeset composition (not to scale):
 *
 * [WS header][   key set   ][         data set        ][    unordered set    ]
 *
 * WS header contains common info: total size, set versions etc.
 * Key set and data set are always present, unordered set is optional.
 */

#ifndef GALERA_WRITE_SET_NG_HPP
#define GALERA_WRITE_SET_NG_HPP

#include "wsrep_api.h"
#include "key_set.hpp"
#include "data_set.hpp"

#include "gu_serialize.hpp"
#include "gu_vector.hpp"

#include <vector>
#include <string>
#include <iomanip>

#include <gu_threads.h>

namespace galera
{
    class WriteSetNG
    {
    public:
        static int const MAX_SIZE     = 0x7fffffff;
        static int const MAX_PA_RANGE = 0x0000ffff;

        enum Version
        {
            VER3 = 3,
            VER4,
            VER5
        };

        /* Max header version that we can understand */
        static Version const MAX_VERSION = VER5;

        /* Parses beginning of the header to detect writeset version and
         * returns it as raw integer for backward compatibility
         * static Version version(int v) will convert it to enum */
        static int version(const void* const buf, size_t const buflen)
        {
            if (gu_likely(buflen >= 4))
            {
                const gu::byte_t* const b(static_cast<const gu::byte_t*>(buf));

                if (b[0] == Header::MAGIC_BYTE   &&
                    b[1] >= ((VER3 << 4) | VER3) &&
                    b[2] >=  32 /* header size will hardly ever go below 32 */)
                {
                    int const min_ver(b[1] & 0x0f);
                    int const max_ver(b[1] >> 4);

                    if (min_ver <= max_ver) /* sanity check */
                    {
                        /* supported situations: return max supported version */
                        if (max_ver <  MAX_VERSION) return max_ver;
                        if (min_ver <= MAX_VERSION) return MAX_VERSION;

                        /* unsupported situation: minimum required version is
                         * greater than maximum known */
                        return min_ver;
                    }
                }
                else if (0 == b[1] && 0 == b[2] && b[3] <= 2)
                {
                    /* header from 2.x and before */
                    return b[3];
                }

                /* unrecognized header, fall through to error */
            }

            return -1;
        }

        static Version version(int v)
        {
            switch (v)
            {
            case VER3: return VER3;
            case VER4: return VER4;
            case VER5: return VER5;
            }

            gu_throw_error (EPROTO) << "Unrecognized writeset version: " << v;
        }

        /* These flags should be fixed to wire protocol version and so
         * technically can't be initialized to WSREP_FLAG_xxx macros as the
         * latter may arbitrarily change. */
        enum Flags
        {
            F_COMMIT      = 1 << 0,
            F_ROLLBACK    = 1 << 1,
            F_TOI         = 1 << 2,
            F_PA_UNSAFE   = 1 << 3,
            F_COMMUTATIVE = 1 << 4,
            F_NATIVE      = 1 << 5,
            F_BEGIN       = 1 << 6,
            F_PREPARE     = 1 << 7,
            /*
             * reserved for provider extension
             */
            F_CERTIFIED   = 1 << 14, // needed to correctly interprete pa_range
                                     // field (VER5 and up)
            F_PREORDERED  = 1 << 15  // (VER5 and up)
        };

        static bool const FLAGS_MATCH_API_FLAGS =
                           (WSREP_FLAG_TRX_END     == F_COMMIT       &&
                            WSREP_FLAG_ROLLBACK    == F_ROLLBACK     &&
                            WSREP_FLAG_ISOLATION   == F_TOI          &&
                            WSREP_FLAG_PA_UNSAFE   == F_PA_UNSAFE    &&
                            WSREP_FLAG_COMMUTATIVE == F_COMMUTATIVE  &&
                            WSREP_FLAG_NATIVE      == F_NATIVE       &&
                            WSREP_FLAG_TRX_START   == F_BEGIN        &&
                            WSREP_FLAG_TRX_PREPARE == F_PREPARE);

        static uint32_t wsrep_flags_to_ws_flags (uint32_t flags);

        typedef gu::RecordSet::GatherVector GatherVector;

        /* TODO: separate metadata access from physical representation in
         *       future versions */
        class Header
        {
        public:

            static unsigned char const MAGIC_BYTE = 'G';

            static Version version(const gu::Buf& buf)
            {
                /* the following will throw if version is not supported */
                return WriteSetNG::version
                    (WriteSetNG::version(buf.ptr, buf.size));
            }

            static unsigned char size(Version ver)
            {
                switch (ver)
                {
                case VER3:
                // fall through
                case VER4:
                // fall through
                case VER5:
                {
                    GU_COMPILE_ASSERT(0 == (V3_SIZE % GU_MIN_ALIGNMENT),
                                      unaligned_header_size);
                    return V3_SIZE;
                }
                }

                log_fatal << "Unknown writeset version: " << ver;
                abort(); // want to dump core right here
            }


            /* This is for WriteSetOut */
            explicit
            Header (Version ver)
            : local_(), ptr_(local_), ver_(ver), size_(size(ver)), chksm_()
            {
                assert((uintptr_t(ptr_) % GU_WORD_BYTES) == 0);
                assert (size_t(size_) <= sizeof(local_));
            }

            size_t gather (KeySet::Version        kver,
                           DataSet::Version const dver,
                           bool                   unord,
                           bool                   annot,
                           uint16_t               flags,
                           const wsrep_uuid_t&    source,
                           const wsrep_conn_id_t& conn,
                           const wsrep_trx_id_t&  trx,
                           GatherVector&          out);

            /* records last_seen, timestamp and CRC before replication */
            void finalize(wsrep_seqno_t ls, int pa_range);

            /* records partial seqno, pa_range, timestamp and CRC before
             * replication (for preordered events)*/
            void finalize_preordered(uint16_t pa_range)
            {
                finalize(0, pa_range);
            }

            /* This is for WriteSetIn */
            explicit
            Header (const gu::Buf& buf)
                :
                local_(),
                ptr_  (static_cast<gu::byte_t*>(const_cast<void*>(buf.ptr))),
                ver_  (version(buf)),
                size_ (check_size(ver_, ptr_, buf.size)),
                chksm_(ver_, ptr_, size_)
            {
                assert((uintptr_t(ptr_) % GU_WORD_BYTES) == 0);
            }

            Header () : local_(), ptr_(NULL), ver_(), size_(0), chksm_()
            {}

            /* for late WriteSetIn initialization */
            void read_buf (const gu::Buf& buf)
            {
                ver_ = version(buf);
                ptr_ = static_cast<gu::byte_t*>(const_cast<void*>(buf.ptr));
                gu_trace(size_ = check_size (ver_, ptr_, buf.size));
                Checksum::verify(ver_, ptr_, size_);
            }

            Version           version() const { return ver_;  }
            unsigned char     size()    const { return size_; }
            const gu::byte_t* ptr()     const { return ptr_;  }

            KeySet::Version  keyset_ver() const
            {
                return KeySet::version((ptr_[V3_SETS_OFF] & 0xf0) >> 4);
            }

            bool             has_keys()  const
            {
                return keyset_ver() != KeySet::EMPTY;
            }

            bool             has_unrd()  const
            {
                return (ptr_[V3_SETS_OFF] & V3_UNORD_FLAG);
            }

            bool             has_annt()  const
            {
                return (ptr_[V3_SETS_OFF] & V3_ANNOT_FLAG);
            }

            DataSet::Version dataset_ver() const
            {
                return DataSet::version((ptr_[V3_SETS_OFF] & 0x0c) >> 2);
            }

            DataSet::Version unrdset_ver() const
            {
                return has_unrd() ? dataset_ver() : DataSet::EMPTY;
            }

            DataSet::Version anntset_ver() const
            {
                return has_annt() ? dataset_ver() : DataSet::EMPTY;
            }

            uint16_t         flags() const
            {
                uint16_t ret;
                gu::unserialize2(ptr_, V3_FLAGS_OFF, ret);
                return ret;
            }

            uint16_t         pa_range() const
            {
                uint16_t ret;
                gu::unserialize2(ptr_, V3_PA_RANGE_OFF, ret);
                return ret;
            }

            wsrep_seqno_t    last_seen() const
            {
                assert (pa_range() == 0 || version() >= VER5);
                return seqno_priv();
            }

            wsrep_seqno_t    seqno() const
            {
                return seqno_priv();
            }

            long long        timestamp() const
            {
                long long ret;
                gu::unserialize8(ptr_, V3_TIMESTAMP_OFF, ret);
                return ret;
            }

            const wsrep_uuid_t& source_id() const
            {
                /* This one is tricky. I would not like to create a copy
                 * of 16 bytes for the sole purpose of referencing it when
                 * alignment in the buffer is already guaranteed */
                assert(uintptr_t(ptr_ + V3_SOURCE_ID_OFF)%GU_WORD_BYTES == 0);
                return *(reinterpret_cast<const wsrep_uuid_t*>
                         (ptr_ + V3_SOURCE_ID_OFF));
            }

            wsrep_conn_id_t conn_id() const
            {
                wsrep_conn_id_t ret;
                gu::unserialize8(ptr_, V3_CONN_ID_OFF, ret);
                return ret;
            }

            wsrep_trx_id_t trx_id() const
            {
                wsrep_trx_id_t ret;
                gu::unserialize8(ptr_, V3_TRX_ID_OFF, ret);
                return ret;
            }

            const gu::byte_t* payload() const
            {
                return ptr_ + size();
            }

            /* to set seqno and parallel applying range after certification */
            void set_seqno(wsrep_seqno_t seqno, uint16_t pa_range);

            gu::Buf copy(bool include_keys, bool include_unrd) const;

        private:

            static ssize_t
            check_size (Version const           ver,
                        const gu::byte_t* const buf,
                        ssize_t const           bufsize)
            {
                assert (bufsize > 4);

                ssize_t const hsize(buf[V3_HEADER_SIZE_OFF]);

                if (gu_unlikely(hsize > bufsize))
                {
                    gu_throw_error (EMSGSIZE)
                        << "Input buffer size " << bufsize
                        << " smaller than header size " << hsize;
                }

                return hsize;
            }

            static int const V3_CHECKSUM_SIZE = 8;

            class Checksum
            {
            public:
                typedef uint64_t type_t;

                static void
                compute (const void* ptr, size_t size, type_t& value)
                {
                    gu::FastHash::digest (ptr, size, value);
                }

                static void
                verify (Version ver, const void* ptr, ssize_t size);

                Checksum () {}
                Checksum (Version ver, const void* ptr, ssize_t size)
                {
                    verify (ver, ptr, size);
                }
            private:
                GU_COMPILE_ASSERT(sizeof(type_t) == V3_CHECKSUM_SIZE, uhoh);
            };

            static unsigned char const V3_ANNOT_FLAG = 0x01;
            static unsigned char const V3_UNORD_FLAG = 0x02;

            /* Fist 8 bytes of header:

               0: 'G' - "magic" byte
               1: bits 4-7: header version
                  bits 0-3: minimum compatible version
               2: header size (payload offset)
               3: bits 4-7: keyset  version
                  bits 2-3: dataset version
                  bit 1:    has unordered set
                  bit 0:    has annotation
               4-5: flags
               6-7: PA range

               all multibyte integers are in little-endian encoding */

            static int const V3_MAGIC_OFF       = 0;
            static int const V3_HEADER_VERS_OFF = V3_MAGIC_OFF       + 1;
            static int const V3_HEADER_SIZE_OFF = V3_HEADER_VERS_OFF + 1;
            static int const V3_SETS_OFF        = V3_HEADER_SIZE_OFF + 1;
            static int const V3_FLAGS_OFF       = V3_SETS_OFF        + 1;
            static int const V3_PA_RANGE_OFF    = V3_FLAGS_OFF       + 2;
            static int const V3_LAST_SEEN_OFF   = V3_PA_RANGE_OFF    + 2;
            static int const V3_SEQNO_OFF       = V3_LAST_SEEN_OFF;
            // seqno takes place of last seen
            static int const V3_TIMESTAMP_OFF   = V3_LAST_SEEN_OFF   + 8;
            static int const V3_SOURCE_ID_OFF   = V3_TIMESTAMP_OFF   + 8;
            static int const V3_CONN_ID_OFF     = V3_SOURCE_ID_OFF   + 16;
            static int const V3_TRX_ID_OFF      = V3_CONN_ID_OFF     + 8;
            static int const V3_CRC_OFF         = V3_TRX_ID_OFF      + 8;
            static int const V3_SIZE            = V3_CRC_OFF         + 8; // 64

            struct Offsets
            {
                int const header_ver_;
                int const header_size_;
                int const sets_;
                int const flags_;
                int const pa_range_;
                int const last_seen_;
                int const seqno_;
                int const timestamp_;
                int const source_id_;
                int const conn_id_;
                int const trx_id_;
                int const crc_;

                Offsets(int, int, int, int, int, int,
                        int, int, int, int, int, int);
            };

            static Offsets const V3;

            static int const MAX_HEADER_SIZE = V3_SIZE;

            mutable
            gu::byte_t  local_[MAX_HEADER_SIZE];
            gu::byte_t* ptr_;
            Version     ver_;
            gu::byte_t  size_;
            Checksum    chksm_;

            wsrep_seqno_t seqno_priv() const
            {
                wsrep_seqno_t ret;
                gu::unserialize8(ptr_, V3_LAST_SEEN_OFF, ret);
                return ret;
            }

            static void
            update_checksum(gu::byte_t* const ptr, size_t const size)
            {
                Checksum::type_t cval;
                Checksum::compute (ptr, size, cval);
                gu::serialize(cval, ptr, size);
            }
        }; /* class Header */

    private:

        /* this assert should be removed when wsrep API flags become
         * explicitly incompatible with wirteset flags */
        GU_COMPILE_ASSERT(FLAGS_MATCH_API_FLAGS, flags_incompatible);

        template <bool>
        static inline uint32_t
        wsrep_flags_to_ws_flags_tmpl (uint32_t const flags)
        {
            assert(0); // remove when needed
            uint32_t ret(0);

            if (flags & WSREP_FLAG_TRX_END)     ret |= F_COMMIT;
            if (flags & WSREP_FLAG_ROLLBACK)    ret |= F_ROLLBACK;
            if (flags & WSREP_FLAG_ISOLATION)   ret |= F_TOI;
            if (flags & WSREP_FLAG_PA_UNSAFE)   ret |= F_PA_UNSAFE;
            if (flags & WSREP_FLAG_COMMUTATIVE) ret |= F_COMMUTATIVE;
            if (flags & WSREP_FLAG_NATIVE)      ret |= F_NATIVE;
            if (flags & WSREP_FLAG_TRX_START)   ret |= F_BEGIN;
            if (flags & WSREP_FLAG_TRX_PREPARE) ret |= F_PREPARE;

            return ret;
        }

    }; /* class WriteSetNG */

    template <> inline uint32_t
    WriteSetNG::wsrep_flags_to_ws_flags_tmpl<true>(uint32_t const flags)
    { return flags; }

    inline uint32_t
    WriteSetNG::wsrep_flags_to_ws_flags (uint32_t const flags)
    { return wsrep_flags_to_ws_flags_tmpl<FLAGS_MATCH_API_FLAGS>(flags); }

    class WriteSetOut
    {
    public:

        typedef gu::RecordSetOutBase::BaseName BaseName;

        WriteSetOut (const std::string&      dir_name,
                     wsrep_trx_id_t          id,
                     KeySet::Version         kver,
                     gu::byte_t*             reserved,
                     size_t                  reserved_size,
                     uint16_t                flags    = 0,
                     gu::RecordSet::Version  rsv      = gu::RecordSet::VER2,
                     WriteSetNG::Version     ver      = WriteSetNG::MAX_VERSION,
                     DataSet::Version        dver     = DataSet::MAX_VERSION,
                     DataSet::Version        uver     = DataSet::MAX_VERSION,
                     size_t                  max_size = WriteSetNG::MAX_SIZE)
            :
            header_(ver),
            base_name_(dir_name, id),
            /* 1/8 of reserved (aligned by 8) goes to key set  */
            kbn_   (base_name_),
            keys_  (reserved,
                    (reserved_size >>= 6, reserved_size <<= 3, reserved_size),
                    kbn_, kver, rsv, ver),
            /* 5/8 of reserved goes to data set  */
            dbn_   (base_name_),
            data_  (reserved + reserved_size, reserved_size*5, dbn_, dver, rsv),
            /* 2/8 of reserved goes to unordered set  */
            ubn_   (base_name_),
            unrd_  (reserved + reserved_size*6, reserved_size*2, ubn_, uver,rsv),
            /* annotation set is not allocated unless requested */
            abn_   (base_name_),
            annt_  (NULL),
            left_  (max_size - keys_.size() - data_.size() - unrd_.size()
                    - header_.size()),
            flags_ (flags)
        {
            assert ((uintptr_t(reserved) % GU_WORD_BYTES) == 0);
        }

        ~WriteSetOut() { delete annt_; }

        void append_key(const KeyData& k)
        {
            left_ -= keys_.append(k);
        }

        void append_data(const void* data, size_t data_len, bool store)
        {
            left_ -= data_.append(data, data_len, store);
        }

        void append_unordered(const void* data, size_t data_len, bool store)
        {
            left_ -= unrd_.append(data, data_len, store);
        }

        void append_annotation(const void* data, size_t data_len, bool store)
        {
            if (NULL == annt_)
            {
                annt_ = new DataSetOut(NULL, 0, abn_, DataSet::MAX_VERSION,
                                       // use the same version as the dataset
                                       data_.gu::RecordSet::version());
                left_ -= annt_->size();
            }

            left_ -= annt_->append(data, data_len, store);
        }

        void set_flags(uint16_t flags) { flags_  = flags; }
        void add_flags(uint16_t flags) { flags_ |= flags; }
        void mark_toi()                { flags_ |= WriteSetNG::F_TOI; }
        void mark_pa_unsafe()          { flags_ |= WriteSetNG::F_PA_UNSAFE; }

        bool is_empty() const
        {
            return ((data_.count() + keys_.count() + unrd_.count() +
                     (annt_ ? annt_->count() : 0)) == 0);
        }


        /* !!! This returns header without checksum! *
         *     Use finalize() to finalize it.   */
        size_t gather(const wsrep_uuid_t&       source,
                      const wsrep_conn_id_t&    conn,
                      const wsrep_trx_id_t&     trx,
                      WriteSetNG::GatherVector& out)
        {
            gu_trace(check_size());

            out->reserve (out->size() + keys_.page_count() + data_.page_count()
                          + unrd_.page_count() + 1 /* global header */);


            size_t out_size (header_.gather (keys_.version(),
                                             data_.version(),
                                             unrd_.version() != DataSet::EMPTY,
                                             NULL != annt_,
                                             flags_, source, conn, trx,
                                             out));

            out_size += keys_.gather(out);
            out_size += data_.gather(out);
            out_size += unrd_.gather(out);

            if (NULL != annt_) out_size += annt_->gather(out);

            return out_size;
        }

        void finalize(wsrep_seqno_t const ls, int const pa_range)
        {
            header_.finalize(ls, pa_range);
        }

        /* Serializes wiriteset into a single buffer (for unit test purposes)
         * set last_seen to -1 if ws was explicitly finalized */
        void serialize(std::vector<gu::byte_t>& ret,
                       const wsrep_uuid_t&      source,
                       const wsrep_conn_id_t&   conn,
                       const wsrep_trx_id_t&    trx,
                       const wsrep_seqno_t      last_seen,
                       const int                pa_range = -1)
        {
            WriteSetNG::GatherVector out;
            size_t const out_size(gather(source, conn, trx, out));
            finalize(last_seen, pa_range);

            ret.clear(); ret.reserve(out_size);

            /* concatenate all out buffers into ret */
            for (size_t i(0); i < out->size(); ++i)
            {
                const gu::byte_t* ptr
                    (static_cast<const gu::byte_t*>(out[i].ptr));
                ret.insert (ret.end(), ptr, ptr + out[i].size);
            }
        }

        void finalize_preordered (ssize_t pa_range)
        {
            assert (pa_range >= 0);

            /* By current convention pa_range is off by 1 from wsrep API def.
             * 0 meaning failed certification. */
            pa_range++;

            header_.finalize_preordered(pa_range);
        }

    private:

        struct BaseNameCommon
        {
            const std::string&       dir_name_;
            unsigned long long const id_;

            BaseNameCommon(const std::string& dir_name, unsigned long long id)
                :
                dir_name_(dir_name),
                id_      (id)
            {}
        };

        template <const char* suffix_>
        class BaseNameImpl : public BaseName
        {
            const BaseNameCommon& data_;

        public:

            BaseNameImpl (const BaseNameCommon& data) : data_(data) {}

            void print(std::ostream& os) const
            {
                os << data_.dir_name_ << "/0x"
                   << std::hex << std::setfill('0') << std::setw(8)
                   << data_.id_ << suffix_;
            }

        }; /* class BaseNameImpl */

        static const char keys_suffix[];
        static const char data_suffix[];
        static const char unrd_suffix[];
        static const char annt_suffix[];

        WriteSetNG::Header  header_;
        BaseNameCommon      base_name_;
        BaseNameImpl<keys_suffix> kbn_;
        KeySetOut           keys_;
        BaseNameImpl<data_suffix> dbn_;
        DataSetOut          data_;
        BaseNameImpl<unrd_suffix> ubn_;
        DataSetOut          unrd_;
        BaseNameImpl<annt_suffix> abn_;
        DataSetOut*         annt_;
        ssize_t             left_;
        uint16_t            flags_;

        void check_size()
        {
            if (gu_unlikely(left_ < 0))
                gu_throw_error (EMSGSIZE)
                    << "Maximum writeset size exceeded by " << -left_;
        }

        WriteSetOut (const WriteSetOut&);
        WriteSetOut& operator= (const WriteSetOut);

    }; /* class WriteSetOut */

    class WriteSetIn
    {
    public:

        WriteSetIn (const gu::Buf& buf, ssize_t const st = SIZE_THRESHOLD)
            : header_(buf),
              size_  (buf.size),
              keys_  (),
              data_  (),
              unrd_  (),
              annt_  (NULL),
              check_thr_id_(),
              check_thr_(false),
              check_ (false)
        {
            gu_trace(init(st));
        }

        WriteSetIn ()
            : header_(),
              size_  (0),
              keys_  (),
              data_  (),
              unrd_  (),
              annt_  (NULL),
              check_thr_id_(),
              check_thr_(false),
              check_ (false)
        {}

        void read_header (const gu::Buf& buf)
        {
            assert (0 == size_);
            assert (false == check_);

            header_.read_buf (buf);
            size_ = buf.size;
        }

        /*
         * WriteSetIn(buf) == WriteSetIn() + read_buf(buf)
         *
         * @param st threshold at which launch dedicated thread for checksumming
         *           0 - no checksumming
         */
        void read_buf (const gu::Buf& buf, ssize_t const st = SIZE_THRESHOLD)
        {
            read_header (buf);
            gu_trace(init(st));
        }

        void read_buf (const void* const ptr, ssize_t const len,
                       ssize_t const st = SIZE_THRESHOLD)
        {
            assert (ptr != NULL);
            assert (len >= 0);
            gu::Buf tmp = { static_cast<const gu::byte_t*>(ptr), len };
            read_buf (tmp, st);
        }

        ~WriteSetIn ()
        {
            if (gu_unlikely(check_thr_))
            {
                /* checksum was performed in a parallel thread */
                gu_thread_join (check_thr_id_, NULL);
            }

            delete annt_;
        }

        WriteSetNG::Version version()   const { return header_.version(); }

        ssize_t       size()      const { return size_;               }
        uint16_t      flags()     const { return header_.flags();     }
        bool          is_toi()    const { return flags() & WriteSetNG::F_TOI; }
        bool          pa_unsafe() const
        { return flags() & WriteSetNG::F_PA_UNSAFE; }
        int           pa_range()  const { return header_.pa_range();  }
        bool          certified() const
        {
            if (gu_likely(version() >= WriteSetNG::VER5))
                return (flags() & WriteSetNG::F_CERTIFIED);
            else
                return (pa_range()); // VER3
        }
        wsrep_seqno_t last_seen() const { return header_.last_seen(); }
        wsrep_seqno_t seqno()     const { return header_.seqno();     }
        long long     timestamp() const { return header_.timestamp(); }

        const wsrep_uuid_t& source_id() const { return header_.source_id(); }
        wsrep_conn_id_t     conn_id()   const { return header_.conn_id();   }
        wsrep_trx_id_t      trx_id()    const { return header_.trx_id();    }

        const KeySetIn&  keyset()  const { return keys_; }
        const DataSetIn& dataset() const { return data_; }
        const DataSetIn& unrdset() const { return unrd_; }

        bool annotated() const { return (annt_ != NULL); }
        void write_annotation(std::ostream& os) const;

        /* This should be called right after certification verdict is obtained
         * and before it is finalized. */
        void verify_checksum() const /* throws */
        {
            if (gu_unlikely(check_thr_))
            {
                /* checksum was performed in a parallel thread */
                gu_thread_join (check_thr_id_, NULL);
                check_thr_ = false;
                gu_trace(checksum_fin());
            }
        }

        uint64_t get_checksum() const
        {
            /* since data segment is the only thing that definitely stays
             * unchanged through WS lifetime, it is the WS signature */
            return (data_.get_checksum());
        }

        void set_seqno(wsrep_seqno_t const seqno, int pa_range)
        {
            assert (seqno    >  0);
            assert (pa_range >= 0);

            /* cap PA range by maximum we can represent */
            if (gu_unlikely(pa_range > WriteSetNG::MAX_PA_RANGE))
                pa_range = WriteSetNG::MAX_PA_RANGE;

            header_.set_seqno (seqno, pa_range);
        }

        typedef gu::Vector<gu::Buf, 8> GatherVector;

        /* can return pointer to internal storage: out can be used only
         * within object scope. */
        size_t gather(GatherVector& out,
                      bool include_keys, bool include_unrd) const;

    private:

        WriteSetNG::Header header_;
        ssize_t            size_;
        KeySetIn           keys_;
        DataSetIn          data_;
        DataSetIn          unrd_;
        DataSetIn*         annt_;
        gu_thread_t        check_thr_id_;
        bool mutable       check_thr_;
        bool               check_;

        static size_t const SIZE_THRESHOLD = 1 << 22; /* 4Mb */

        void checksum (); /* checksums writeset, stores result in check_ */

        void checksum_fin() const
        {
            if (gu_unlikely(!check_))
            {
                gu_throw_error(EINVAL) << "Writeset checksum failed";
            }
        }

        static void* checksum_thread (void* arg)
        {
            WriteSetIn* ws(reinterpret_cast<WriteSetIn*>(arg));
            ws->checksum();
            return NULL;
        }

        /* late initialization after default constructor */
        void init (ssize_t size_threshold);

        WriteSetIn (const WriteSetIn&);
        WriteSetIn& operator=(WriteSetIn);
    };

} /* namespace galera */


#endif // GALERA_WRITE_SET_HPP
