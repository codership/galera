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

        static KeySet::Version ws_to_ks_version (Version ver)
        {
            return KeySet::FLAT8A;
        }

        static DataSet::Version ws_to_ds_version (Version ver)
        {
            return DataSet::VER1;
        }

        class Header
        {
        public:

            static Version version(int v)
            {
                switch (v)
                {
                case VER3: return VER3;
                }

                gu_throw_error (EPROTO) << "Unrecognized writeset version: "<<v;
            }

            static Version version(const gu::byte_t* hdr)
            {
                return version(hdr[0]);
            }

            static size_t size (Version ver)
            {
                switch (ver)
                {
                case VER3: return V3_SIZE;
                }

                log_fatal << "Unsupported writeset version: " << ver;
                abort();
            }


            /* This is for WriteSetOut */
            Header () : checksum_(), buf_() { buf_.ptr = NULL; buf_.size = 0; }

            size_t init (Version          ver,
                         KeySet::Version  kver,
                         DataSet::Version dver,
                         DataSet::Version uver,
                         uint16_t         flags);

            void   free () { delete[] buf_.ptr; }
            /* records last_seen, timestamp and CRC before replication */
            void set_last_seen (const wsrep_seqno_t& ls);

            /* This is for WriteSetIn */
            Header (const gu::Buf& buf) : checksum_(buf), buf_(buf) {}

            Version       version()   const { return version(buf_.ptr); }
            size_t        size()      const { return size(version()); }

            KeySet::Version keyset_ver() const
            {
                return KeySet::version((buf_.ptr[V3_KEYSET_VER] & 0xf0) >> 4);
            }

            DataSet::Version dataset_ver() const
            {
                return DataSet::version((buf_.ptr[V3_DATASET_VER] & 0x0c) >> 2);
            }

            DataSet::Version unrdset_ver() const
            {
                return DataSet::version((buf_.ptr[V3_UNRDSET_VER] & 0x03));
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
                    *(reinterpret_cast<const uint16_t*>(buf_.ptr + V3_FLAGS))
                    );
            }

            uint32_t      pa_range() const
            {
                return gu::gtoh(
                    *(reinterpret_cast<const uint32_t*>(buf_.ptr + V3_PA_RANGE))
                    );
            }

            wsrep_seqno_t last_seen() const
            {
                assert (pa_range() == 0);
                return seqno_priv();
            }

            wsrep_seqno_t seqno() const
            {
                assert (pa_range() > 0);
                return seqno_priv();
            }

            long long     timestamp() const
            {
                return gu::gtoh(
                    *(reinterpret_cast<const uint64_t*>(buf_.ptr +V3_TIMESTAMP))
                    );
            }

            const gu::byte_t* payload() const
            {
                return buf_.ptr + size();
            }

            const gu::Buf& operator() () const { return buf_; }

            /* to set seqno and parallel applying range after certification */
            void set_seqno(const wsrep_seqno_t& seqno, int pa_range);

        private:

            static int const V3_HEADER_VER  = 0;
            static int const V3_KEYSET_VER  = V3_HEADER_VER + sizeof(uint8_t);
            // data and unordered sets share the same byte with keyset version
            static int const V3_DATASET_VER = V3_KEYSET_VER;
            static int const V3_UNRDSET_VER = V3_DATASET_VER;
            static int const V3_FLAGS       = V3_UNRDSET_VER + sizeof(uint8_t);
            static int const V3_PA_RANGE    = V3_FLAGS       + sizeof(uint16_t);
            static int const V3_LAST_SEEN   = V3_PA_RANGE    + sizeof(uint32_t);
            static int const V3_SEQNO       = V3_LAST_SEEN;
            // seqno takes place of last seen
            static int const V3_TIMESTAMP   = V3_LAST_SEEN   + sizeof(uint64_t);
            static int const V3_CRC         = V3_TIMESTAMP   + sizeof(uint64_t);
            static int const V3_SIZE        = V3_CRC         + sizeof(uint32_t);

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
                int const crc_;
                int const size_;

                Offsets(int, int, int, int, int, int, int, int, int, int, int);
            };

            static Offsets const V3;

            class Checksum
            {
            public:
                Checksum () {}
                Checksum (const gu::Buf& buf);
            } checksum_;

            gu::Buf buf_;

            wsrep_seqno_t seqno_priv() const
            {
                return gu::gtoh(
                    *(reinterpret_cast<const uint64_t*>(buf_.ptr +V3_LAST_SEEN))
                    );
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
            header_(),
            keys_  (base_name + "_keys", kver),
            data_  (base_name + "_data", dver),
            unrd_  (base_name + "_unrd", uver),
            left_  (max_size - keys_.size() - data_.size() - unrd_.size()
                    - WriteSetNG::Header::size(ver_)),
            flags_ (flags)
        {}

        ~WriteSetOut() { header_.free(); }

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

        void set_flags(uint16_t flags) { flags_ |= flags; }

        bool is_empty() const
        {
            return ((data_.count() + keys_.count() + unrd_.count()) == 0);
        }

        size_t gather(std::vector<gu::Buf>& out)
        {
            check_size();

            out.reserve (out.size() + keys_.page_count() + data_.page_count()
                         + unrd_.page_count() + 1 /* global header */);


            size_t out_size (header_.init (ver_,
                                           keys_.version(),
                                           data_.version(),
                                           unrd_.version(),
                                           flags_));
            out.push_back(header_());

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
              check_ (false)
        {
            WriteSetNG::Version const ver  (header_.version());
            const gu::byte_t*         pptr (header_.payload());
            ssize_t                   psize(size_ - header_.size(ver));

            assert (psize >= 0);

            KeySet::Version const kver(header_.keyset_ver());
            if (kver != KeySet::EMPTY) gu_trace(keys_.init (kver, pptr, psize));

            if (gu_likely(size_ < st))
            {
                checksum();
                checksum_fin();
            }
            else
            {
                int err = pthread_create (&check_thr_, NULL,
                                          checksum_thread, this);

                if (gu_unlikely(err != 0))
                {
                    gu_throw_error(err) << "Starting checksum thread failed";
                }
            }
        }

        ~WriteSetIn ()
        {
            if (gu_unlikely(false == check_))
            {
                /* checksum was performed in a parallel thread */
                pthread_join (check_thr_, NULL);
            }
//            delete keys_;
//            delete data_;
//            delete unrd_;
        }

        uint16_t      flags()     const { return header_.flags();     }
        int           pa_range()  const { return header_.pa_range();  }
        bool          certified() const { return header_.pa_range();  }
        wsrep_seqno_t last_seen() const { return header_.last_seen(); }
        wsrep_seqno_t seqno()     const { return header_.seqno();     }
        long long     timestamp() const { return header_.timestamp(); }

        const KeySetIn&  keyset()  const { return keys_; }
        const DataSetIn& dataset() const { return data_; }
        const DataSetIn& unrdset() const { return unrd_; }

        /* This should be called right after certification verdict is obtained
         * and before it is finalized. */
        void verify_checksum() const
        {
            if (gu_unlikely(false == check_))
            {
                /* checksum was performed in a parallel thread */
                pthread_join (check_thr_, NULL);
                checksum_fin();
            }
        }

        void set_seqno(const wsrep_seqno_t& seqno, int pa_range)
        {
            assert (seqno > 0);
            assert (pa_range > 0);
            header_.set_seqno (seqno, pa_range);
        }

    private:

        WriteSetNG::Header header_;
        ssize_t const      size_;
//        const KeySetIn*    keys_;
//        const DataSetIn*   data_;
//        const DataSetIn*   unrd_;
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

        WriteSetIn (const WriteSetIn&);
        WriteSetIn& operator=(WriteSetIn);
    };

} /* namespace galera */


#endif // GALERA_WRITE_SET_HPP
