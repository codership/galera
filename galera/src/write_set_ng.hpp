//
// Copyright (C) 2013 Codership Oy <info@codership.com>
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

#include "key_set.hpp"
#include "data_set.hpp"

#include "gu_serialize.hpp"

#include <vector>
#include <string>

#include <pthread.h>

namespace galera
{
    class WriteSetNG
    {
    public:
        static size_t const MAX_SIZE = 0x7fffffff;

        enum Version
        {
            VER3 = 3
        };

        static Version const MAX_VERSION = VER3;

        static Version version(int v)
        {
            switch (v)
            {
            case VER3: return VER3;
            }

            gu_throw_error (EPROTO) << "Unrecognized writeset version: "<<v;
        }

        enum Flags
        {
            F_COMMIT      = 1 << 0,
            F_ROLLBACK    = 1 << 1,
            F_OOC         = 1 << 2,
            F_TOI         = 1 << 3,
            F_PA_UNSAFE   = 1 << 4
        };

        /* TODO: separate metadata access from physical representation in
         *       future versions */
        class Header
        {
        public:

            static Version version(const gu::Buf& buf)
            {
                uint32_t vr(0);
                if (gu_likely(size_t(buf.size) >= sizeof(vr)))
                    gu::unserialize4(buf.ptr, sizeof(vr), 0, vr);
                /* the following will throw also if buf is too short and vr=0 */
                return WriteSetNG::version(vr >> 24);// compatibility with ver 2
            }

            static int size (Version ver)
            {
                switch (ver)
                {
                case VER3: return V3_SIZE;
                }

                log_fatal << "Unsupported writeset version: " << ver;
                abort(); // want to dump core right here
            }


            /* This is for WriteSetOut */
            explicit
            Header (Version ver)
            : local_(), ver_(ver), ptr_(local_), size_(size(ver)), chksm_()
            {
                assert (size_t(size_) <= sizeof(local_));
            }

            size_t gather (Version                ver,
                           KeySet::Version        kver,
                           DataSet::Version       dver,
                           DataSet::Version       uver,
                           uint16_t               flags,
                           const wsrep_uuid_t&    source,
                           const wsrep_conn_id_t& conn,
                           const wsrep_trx_id_t&  trx,
                           std::vector<gu::Buf>&  out);

            /* records last_seen, timestamp and CRC before replication */
            void set_last_seen (const wsrep_seqno_t& ls);

            /* This is for WriteSetIn */
            explicit
            Header (const gu::Buf& buf)
                :
                local_(),
                ver_  (version(buf)),
                ptr_  (reinterpret_cast<gu::byte_t*>(
                           const_cast<void*>(buf.ptr))),
                size_ (check_size(ver_, buf.size)),
                chksm_(ver_, ptr_, size_)
            {}

            Header () : local_(), ver_(), ptr_(NULL), size_(0), chksm_()
            {}

            /* for late WriteSetIn initialization */
            void read_buf (const gu::Buf& buf)
            {
                ver_ = version(buf);
                ptr_ =
                    reinterpret_cast<gu::byte_t*>(const_cast<void*>(buf.ptr));
                size_ = check_size (ver_, buf.size);
                Checksum::verify(ver_, ptr_, size_);
            }

            Version version() const { return ver_;  }
            int     size()    const { return size_; }
            const gu::byte_t* ptr() const { return ptr_; }

            KeySet::Version  keyset_ver() const
            {
                return KeySet::version((ptr_[V3_KEYSET_VER] & 0xf0) >> 4);
            }

            DataSet::Version dataset_ver() const
            {
                return DataSet::version((ptr_[V3_DATASET_VER] & 0x0c) >> 2);
            }

            DataSet::Version unrdset_ver() const
            {
                return DataSet::version((ptr_[V3_UNRDSET_VER] & 0x03));
            }

            bool          has_keys()  const
            {
                return keyset_ver() != KeySet::EMPTY;
            }

            bool          has_unrd()  const
            {
                return unrdset_ver() != DataSet::EMPTY;
            }

            uint16_t      flags() const
            {
                return gu::gtoh(
                    *(reinterpret_cast<const uint16_t*>(ptr_ + V3_FLAGS))
                    );
            }

            uint32_t      dep_window() const
            {
                return gu::gtoh(
                    *(reinterpret_cast<const uint32_t*>(ptr_ + V3_DEP_WINDOW))
                    );
            }

            wsrep_seqno_t last_seen() const
            {
                assert (dep_window() == 0);
                return seqno_priv();
            }

            wsrep_seqno_t seqno() const
            {
                assert (dep_window() > 0);
                return seqno_priv();
            }

            long long     timestamp() const
            {
                return gu::gtoh(
                    *(reinterpret_cast<const uint64_t*>(ptr_ + V3_TIMESTAMP))
                    );
            }

            const wsrep_uuid_t& source_id() const
            {
                return *(reinterpret_cast<const wsrep_uuid_t*>(ptr_ +
                                                               V3_SOURCE_ID));
            }

            wsrep_trx_id_t conn_id() const
            {
                return gu::gtoh(
                    *(reinterpret_cast<const uint64_t*>(ptr_ + V3_CONN_ID))
                    );
            }

            wsrep_trx_id_t trx_id() const
            {
                return gu::gtoh(
                    *(reinterpret_cast<const uint64_t*>(ptr_ + V3_TRX_ID))
                    );
            }

            const gu::byte_t* payload() const
            {
                return ptr_ + size();
            }

            /* to set seqno and parallel applying range after certification */
            void set_seqno(const wsrep_seqno_t& seqno, int pa_range);

            gu::Buf copy(bool include_keys, bool include_unrd) const;

        private:

            static ssize_t
            check_size (Version ver, ssize_t bufsize)
            {
                assert (bufsize > 4);

                ssize_t const hsize(size(ver));

                if (gu_unlikely(hsize > bufsize))
                {
                    gu_throw_error (EMSGSIZE)
                        << "Input buffer size " << bufsize
                        << " smaller than header size " << hsize;
                }

                return hsize;
            }

            class Checksum
            {
            public:
                typedef uint64_t type_t;

                /* produce value corrected for endianness */
                static void
                compute (const void* ptr, size_t size, type_t& value)
                {
                    gu::CRC::digest (ptr, size, value);
                    value = gu::htog<type_t>(value);
                }

                static void
                verify (Version ver, const void* ptr, ssize_t size);

                Checksum () {}
                Checksum (Version ver, const void* ptr, ssize_t size)
                {
                    verify (ver, ptr, size);
                }
            };

            static int const V3_HEADER_VER  = 0;
            static int const V3_KEYSET_VER  = V3_HEADER_VER + sizeof(uint32_t);
            // data and unordered sets share the same byte with keyset version
            static int const V3_DATASET_VER = V3_KEYSET_VER;
            static int const V3_UNRDSET_VER = V3_DATASET_VER;
            static int const V3_FLAGS       = V3_UNRDSET_VER + sizeof(uint16_t);
            static int const V3_DEP_WINDOW  = V3_FLAGS       + sizeof(uint16_t);
            static int const V3_LAST_SEEN   = V3_DEP_WINDOW  + sizeof(uint32_t);
            static int const V3_SEQNO       = V3_LAST_SEEN;
            // seqno takes place of last seen
            static int const V3_TIMESTAMP   = V3_LAST_SEEN   + sizeof(uint64_t);
            static int const V3_SOURCE_ID   = V3_TIMESTAMP   + sizeof(uint64_t);
            static int const V3_CONN_ID     = V3_SOURCE_ID+sizeof(wsrep_uuid_t);
            static int const V3_TRX_ID      = V3_CONN_ID     + sizeof(uint64_t);
            static int const V3_CRC         = V3_TRX_ID      + sizeof(uint64_t);
            static int const V3_SIZE        = V3_CRC + sizeof(Checksum::type_t);

            struct Offsets
            {
                int const header_ver_;
                int const keyset_ver_;
                int const dataset_ver_;
                int const unrdset_ver_;
                int const flags_;
                int const dep_window_;
                int const last_seen_;
                int const seqno_;
                int const timestamp_;
                int const source_id_;
                int const conn_id_;
                int const trx_id_;
                int const crc_;
                int const size_;

                Offsets(int, int, int, int, int, int, int, int, int, int,
                        int, int, int, int);
            };

            static Offsets const V3;

            static int const MAX_HEADER_SIZE = V3_SIZE;

            mutable
            gu::byte_t  local_[MAX_HEADER_SIZE];
            Version     ver_;
            gu::byte_t* ptr_;
            ssize_t     size_;
            Checksum    chksm_;

            wsrep_seqno_t seqno_priv() const
            {
                return gu::gtoh(
                    *(reinterpret_cast<const uint64_t*>(ptr_ + V3_LAST_SEEN))
                    );
            }

            static void
            update_checksum(gu::byte_t* const ptr, size_t const size)
            {
                Checksum::type_t cval;
                Checksum::compute (ptr, size, cval);
                *reinterpret_cast<Checksum::type_t*>(ptr + size) = cval;
            }
        };
    };

    class WriteSetOut
    {
    public:

        explicit
        WriteSetOut (const std::string&  base_name,
                     uint16_t            flags    = 0,
                     WriteSetNG::Version ver      = WriteSetNG::MAX_VERSION,
                     KeySet::Version     kver     = KeySet::MAX_VERSION,
                     DataSet::Version    dver     = DataSet::MAX_VERSION,
                     DataSet::Version    uver     = DataSet::MAX_VERSION,
                     size_t              max_size = WriteSetNG::MAX_SIZE)
            :
            ver_   (ver),
            header_(ver_),
            keys_  (base_name + "_keys", kver),
            data_  (base_name + "_data", dver),
            unrd_  (base_name + "_unrd", uver),
            left_  (max_size - keys_.size() - data_.size() - unrd_.size()
                    - WriteSetNG::Header::size(ver_)),
            flags_ (flags)
        {}

        void append_key(const KeyData& k)
        {
            left_ -= keys_.append(k);
        }

        void append_data(const void*data, size_t data_len, bool store)
        {
            left_ -= data_.append(data, data_len, store);
        }

        void append_unordered(const void*data, size_t data_len, bool store)
        {
            left_ -= unrd_.append(data, data_len, store);
        }

        void set_flags(uint16_t flags) { flags_  = flags; }
        void add_flags(uint16_t flags) { flags_ |= flags; }
        void mark_toi()                { flags_ |= WriteSetNG::F_TOI; }
        void mark_pa_unsafe()          { flags_ |= WriteSetNG::F_PA_UNSAFE; }

        bool is_empty() const
        {
            return ((data_.count() + keys_.count() + unrd_.count()) == 0);
        }

        size_t gather(const wsrep_uuid_t&    source,
                      const wsrep_conn_id_t& conn,
                      const wsrep_trx_id_t&  trx,
                      std::vector<gu::Buf>&  out)
        {
            check_size();

            out.reserve (out.size() + keys_.page_count() + data_.page_count()
                         + unrd_.page_count() + 1 /* global header */);


            size_t out_size (header_.gather (ver_,
                                             keys_.version(),
                                             data_.version(),
                                             unrd_.version(),
                                             flags_, source, conn, trx,
                                             out));

            out_size += keys_.gather(out);
            out_size += data_.gather(out);
            out_size += unrd_.gather(out);

            return out_size;
        }

        void set_last_seen (const wsrep_seqno_t& ls)
        {
            header_.set_last_seen(ls);
        }

    private:

        WriteSetNG::Version ver_;
        WriteSetNG::Header  header_;
        KeySetOut           keys_;
        DataSetOut          data_;
        DataSetOut          unrd_;
        ssize_t             left_;
        uint16_t            flags_;

        void check_size()
        {
            if (gu_unlikely(left_ < 0))
                gu_throw_error (EMSGSIZE)
                    << "Maximum wirteset size exceeded by " << -left_;
        }

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
              check_thr_(),
              check_ (st <= 0) /* st <= 0 means no checksumming is performed */
        {
            init (st);
        }

        WriteSetIn ()
            : header_(),
              size_  (0),
              keys_  (),
              data_  (),
              unrd_  (),
              check_thr_(),
              check_ (false)
        {}

        /* WriteSetIn(buf) == WriteSetIn() + read_buf(buf) */
        void read_buf (const gu::Buf& buf, ssize_t const st = SIZE_THRESHOLD)
        {
            assert (0 == size_);
            assert (false == check_);

            header_.read_buf (buf);
            size_ = buf.size;
            check_ = (st <= 0);
            init (st);
        }

        void read_buf (const gu::byte_t* const ptr, ssize_t const len)
        {
            assert (ptr != NULL);
            assert (len >= 0);
            gu::Buf tmp = { ptr, len };
            read_buf (tmp);
        }

        ~WriteSetIn ()
        {
            if (gu_unlikely(false == check_ && size_ != 0))
            {
                /* checksum was performed in a parallel thread */
                pthread_join (check_thr_, NULL);
            }
        }

        uint16_t      flags()      const { return header_.flags();       }
        bool          is_toi()     const
        { return flags() & WriteSetNG::F_TOI; }
        bool          pa_unsafe()  const
        { return flags() & WriteSetNG::F_PA_UNSAFE; }
        int           dep_window() const { return header_.dep_window();  }
        bool          certified()  const { return header_.dep_window();  }
        wsrep_seqno_t last_seen()  const { return header_.last_seen();   }
        wsrep_seqno_t seqno()      const { return header_.seqno();       }
        long long     timestamp()  const { return header_.timestamp();   }

        const wsrep_uuid_t& source_id() const { return header_.source_id(); }
        wsrep_conn_id_t     conn_id()   const { return header_.conn_id();   }
        wsrep_trx_id_t      trx_id()    const { return header_.trx_id();    }

        const KeySetIn&  keyset()  const { return keys_; }
        const DataSetIn& dataset() const { return data_; }
        const DataSetIn& unrdset() const { return unrd_; }

        /* This should be called right after certification verdict is obtained
         * and before it is finalized. */
        void verify_checksum() const /* throws */
        {
            if (gu_unlikely(false == check_ && size_ != 0))
            {
                /* checksum was performed in a parallel thread */
                pthread_join (check_thr_, NULL);
                checksum_fin();
            }
        }

        void set_seqno(const wsrep_seqno_t& seqno, int dep_window)
        {
            assert (seqno > 0);
            assert (dep_window > 0);
            header_.set_seqno (seqno, dep_window);
        }

        /* can return pointer to internal storage: out can be used only
         * within object scope. */
        size_t gather(std::vector<gu::Buf>& out,
                      bool include_keys, bool include_unrd) const;

    private:

        WriteSetNG::Header header_;
        ssize_t            size_;
        KeySetIn           keys_;
        DataSetIn          data_;
        DataSetIn          unrd_;
        pthread_t          check_thr_;
        bool               check_;

        static size_t const SIZE_THRESHOLD = 1 << 20; /* 1Mb */

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
