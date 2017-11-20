//
// Copyright (C) 2013 Codership Oy <info@codership.com>
//


#ifndef GALERA_DATA_SET_HPP
#define GALERA_DATA_SET_HPP

#include "gu_rset.hpp"
#include "gu_vlq.hpp"


namespace galera
{
    class DataSet
    {
    public:

        enum Version
        {
            EMPTY = 0,
            VER1
        };

        static Version const MAX_VERSION = VER1;

        static Version version (unsigned int ver)
        {
            if (gu_likely (ver <= MAX_VERSION))
                return static_cast<Version>(ver);

            gu_throw_error (EINVAL) << "Unrecognized DataSet version: " << ver;
        }

        /*! Dummy class to instantiate DataSetOut */
        class RecordOut {};

        /*! A class to instantiate DataSetIn: provides methods necessary to
         *  iterate over the records serialized into single input buffer */
        class RecordIn
        {
        public:

            static size_t serial_size (const gu::byte_t* const buf,
                                       size_t const            size)
            {
                /* There's a single record in a dataset */
                return size;
            }

            size_t serial_size () const { return size_; }

            RecordIn (const gu::byte_t* buf, size_t size)
                  : size_(size), buf_(buf)
            {}

            gu::Buf buf() { gu::Buf ret = { buf_, size_ }; return ret; }

        private:

            ssize_t           size_;
            const gu::byte_t* buf_;

        }; /* class RecordIn */

    }; /* class DataSet */


#if defined(__GNUG__)
# if (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || (__GNUC__ > 4)
#  pragma GCC diagnostic push
# endif // (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || (__GNUC__ > 4)
# pragma GCC diagnostic ignored "-Weffc++"
#endif

    class DataSetOut : public gu::RecordSetOut<DataSet::RecordOut>
    {
    public:

        DataSetOut () // empty ctor for slave TrxHandle
            :
            gu::RecordSetOut<DataSet::RecordOut>(), version_()
        {}

        DataSetOut (gu::byte_t*             reserved,
                    size_t                  reserved_size,
                    const BaseName&         base_name,
                    DataSet::Version        version,
                    gu::RecordSet::Version  rsv)
            :
            gu::RecordSetOut<DataSet::RecordOut> (
                reserved,
                reserved_size,
                base_name,
                check_type(version),
                rsv
                ),
            version_(version)
        {
            assert((uintptr_t(reserved) % GU_WORD_BYTES) == 0);
        }

        size_t
        append (const void* const src, size_t const size, bool const store)
        {
            /* append data as is, don't count as a new record */
            gu_trace(
                gu::RecordSetOut<DataSet::RecordOut>::append (src, size, store,
                                                              false);
                );
            /* this will be deserialized using DataSet::RecordIn in DataSetIn */

            return size;
        }

        DataSet::Version
        version () const { return count() ? version_ : DataSet::EMPTY; }

        typedef gu::RecordSet::GatherVector GatherVector;

    private:

        // depending on version we may pack data differently
        DataSet::Version const version_;

        static gu::RecordSet::CheckType
        check_type (DataSet::Version ver)
        {
            switch (ver)
            {
            case DataSet::EMPTY: break; /* Can't create EMPTY DataSetOut */
            case DataSet::VER1:  return gu::RecordSet::CHECK_MMH128;
            }
            throw;
        }

    }; /* class DataSetOut */


    class DataSetIn : public gu::RecordSetIn<DataSet::RecordIn>
    {
    public:

        DataSetIn (DataSet::Version ver, const gu::byte_t* buf, size_t size)
            :
            gu::RecordSetIn<DataSet::RecordIn>(buf, size, false),
            version_(ver)
        {}

        DataSetIn () : gu::RecordSetIn<DataSet::RecordIn>(),
                       version_(DataSet::EMPTY)
        {}

        void init (DataSet::Version ver, const gu::byte_t* buf, size_t size)
        {
            gu::RecordSetIn<DataSet::RecordIn>::init(buf, size, false);
            version_ = ver;
        }

        gu::Buf next () const
        {
            return gu::RecordSetIn<DataSet::RecordIn>::next().buf();
        }

    private:

        DataSet::Version version_;

    }; /* class DataSetIn */

#if defined(__GNUG__)
# if (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || (__GNUC__ > 4)
#  pragma GCC diagnostic pop
# endif // (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || (__GNUC__ > 4)
#endif

} /* namespace galera */

#endif // GALERA_DATA_SET_HPP
