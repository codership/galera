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
            VER0
        };

        static Version const MAX_VER = VER0;

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
                case VER0: return VER0;
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
                case VER0:
                    return
                        sizeof(gu::byte_t) + /* version   */
                        sizeof(gu::byte_t) + /* flags     */
                        sizeof(uint64_t)   + /* timestamp */
                        sizeof(uint64_t)   + /* last seen */
                        sizeof(uint32_t);    /* CRC       */
                }

                log_fatal << "Unsupported writeset version: " << ver;
                abort();
            }


            /* This is for WriteSetOut */
            Header () : checksum_(), buf_() { buf_.ptr = NULL; buf_.size = 0; }

            size_t init (Version ver, bool has_keys, bool has_unrd);
            void   free () { delete[] buf_.ptr; } //const_cast<gu::byte_t*>(ptr_); }
            /* records last_seen, timestamp and CRC before replication */
            void set_last_seen (const wsrep_seqno_t& ls);

            /* This is for WriteSetIn */
            Header (const gu::Buf& buf) : checksum_(buf), buf_(buf) {}

            Version       version()   const { return version(buf_.ptr); }
            size_t        size()      const { return size(version()); }
            bool          has_keys()  const { return buf_.ptr[1] & F_HAS_KEYS; }
            bool          has_unrd()  const { return buf_.ptr[1] & F_HAS_UNRD; }

            long long     timestamp() const
            {
                return gu::gtoh(
                    *(reinterpret_cast<const uint64_t*>(buf_.ptr + 2))
                    );
            }

            wsrep_seqno_t last_seen() const
            {
                return gu::gtoh(
                    *(reinterpret_cast<const uint64_t*>(buf_.ptr + 10))
                    );
            }

            const gu::byte_t* payload() const
            {
                return buf_.ptr + size(version());
            }

            const gu::Buf& operator() () const { return buf_; }

        private:

            static gu::byte_t const F_HAS_KEYS = 0x01;
            static gu::byte_t const F_HAS_UNRD = 0x02;

            class Checksum
            {
            public:
                Checksum () {}
                Checksum (const gu::Buf& buf);
            } checksum_;

            gu::Buf buf_;
        };
    };

    class WriteSetOut
    {
    public:

        WriteSetOut (const std::string&  base_name,
                     WriteSetNG::Version ver      = WriteSetNG::VER0,
                     size_t              max_size = WriteSetNG::MAX_SIZE)
            :
            ver_   (ver),
            header_(),
            keys_  (base_name + "_keys", WriteSetNG::ws_to_ks_version(ver)),
            data_  (base_name + "_data", WriteSetNG::ws_to_ds_version(ver)),
            unrd_  (base_name + "_unrd", WriteSetNG::ws_to_ds_version(ver)),
            left_  (max_size - keys_.size() - data_.size() - unrd_.size()
                    - WriteSetNG::Header::size(ver_))
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
                                           keys_.count() != 0,
                                           unrd_.count() != 0));
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
              keys_  (NULL),
              data_  (NULL),
              unrd_  (NULL),
              check_thr_(pthread_self()),
              check_ (false)
        {
            WriteSetNG::Version const ver  (header_.version());
            const gu::byte_t*         pptr (header_.payload());
            ssize_t                   psize(size_ - header_.size(ver));

            assert (psize >= 0);

            if (header_.has_keys())
                keys_ = new KeySetIn (WriteSetNG::ws_to_ks_version(ver),
                                      pptr, psize);

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
            delete keys_;
            delete data_;
            delete unrd_;
        }

        wsrep_seqno_t last_seen() const { return header_.last_seen(); }
        long long     timestamp() const { return header_.timestamp(); }

        const KeySetIn*  keyset()  const { return keys_; }
        const DataSetIn* dataset() const { return data_; }
        const DataSetIn* unrdset() const { return unrd_; }

        /* This should be called right after certification verdict is obtained
         * and before it is finalized. */
        void verify_checksum()
        {
            if (gu_unlikely(false == check_))
            {
                /* checksum was performed in a parallel thread */
                pthread_join (check_thr_, NULL);
                checksum_fin();
            }
        }

    private:

        WriteSetNG::Header header_;
        ssize_t const      size_;
        const KeySetIn*    keys_;
        const DataSetIn*   data_;
        const DataSetIn*   unrd_;
        pthread_t          check_thr_;
        bool               check_;

        static size_t const SIZE_THRESHOLD = 1 << 20; /* 1Mb */

        void checksum () // throws if checksum fails.
        {
            WriteSetNG::Version const ver  (header_.version());
            bool const                unrd (header_.has_unrd());
            const gu::byte_t*         pptr (header_.payload());
            ssize_t                   psize(size_ - header_.size());

            assert (psize >= 0);

            try
            {
                if (keys_)
                {
                    gu_trace(keys_->checksum());
                    psize -= keys_->size();
                    assert (psize >= 0);
                    pptr  += keys_->size();
                }

                DataSet::Version const dv(WriteSetNG::ws_to_ds_version(ver));

                data_ = new DataSetIn (dv, pptr, psize);
                gu_trace(data_->checksum());
                psize -= data_->size();
                assert (psize >= 0);
                pptr  += data_->size();

                if (unrd)
                {
                    unrd_ = new DataSetIn (dv, pptr, psize);
                    gu_trace(unrd_->checksum());
                    psize -= unrd_->size();
                    assert (psize == 0);
                }

                check_ = true;
            }
            catch (std::exception& e)
            {
                log_error << e.what();
            }
            catch (...)
            {
                log_error << "Non-standard exception in WriteSet::checksum()";
            }
        }

        void checksum_fin()
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
