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
                size_t payload_size;
                size_t off = gu::uleb128_decode(buf, size, payload_size);
                return (payload_size + off);
            }

            size_t serial_size () const { return off_ + size_; }

            RecordIn (const gu::byte_t* buf, size_t size)
                : size_(),
                  off_ (gu::uleb128_decode (buf, size, size_)),
                  buf_ (buf + off_)
            {}

            gu::Buf buf() { gu::Buf ret = { buf_, size_ }; return ret; }

        private:

            ssize_t           size_;
            ssize_t const     off_;
            const gu::byte_t* buf_;

        }; /* class RecordIn */

    }; /* class DataSet */


#if defined(__GNUG__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#endif

    class DataSetOut : public gu::RecordSetOut<DataSet::RecordOut>
    {
    public:

        DataSetOut (const std::string& base_name,
                    DataSet::Version   version)
            :
            RecordSetOut (
                base_name,
                check_type      (version),
                ds_to_rs_version(version)
                ),
            version_(version)
        {}

        size_t
        append (const void* const src, size_t const size, bool const store)
        {
            gu::byte_t serial_size[8]; // this allows to encode 56-bits, any
                                       // size that does not fit in is unreal
            size_t const size_size (gu::uleb128_encode(size, serial_size,
                                                       sizeof(serial_size)));

            /* first - size of record */
            RecordSetOut::append (&serial_size, size_size, true);
            /* then record itself, don't count as a new record */
            RecordSetOut::append (src, size, store, false);
            /* this will be deserialized using DataSet::RecordIn in DataSetIn */

            return size_size + size;
        }

        DataSet::Version
        version () const { return count() ? version_ : DataSet::EMPTY; }

    private:

        // depending on version we may pack data differently
        DataSet::Version version_;

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

        static gu::RecordSet::Version
        ds_to_rs_version (DataSet::Version ver)
        {
            switch (ver)
            {
            case DataSet::EMPTY: break; /* Can't create EMPTY DataSetOut */
            case DataSet::VER1:  return gu::RecordSet::VER1;
            }
            throw;
        }
    };


    class DataSetIn : public gu::RecordSetIn<DataSet::RecordIn>
    {
    public:

        DataSetIn (DataSet::Version ver, const gu::byte_t* buf, size_t size)
            :
            RecordSetIn(buf, size, false),
            version_(ver)
        {}

        DataSetIn () : RecordSetIn(), version_(DataSet::EMPTY) {}

        void init (DataSet::Version ver, const gu::byte_t* buf, size_t size)
        {
            RecordSetIn::init(buf, size, false);
            version_ = ver;
        }

        gu::Buf next () const { return RecordSetIn::next().buf(); }

    private:

        DataSet::Version version_;

    }; /* class DataSetIn */

#if defined(__GNUG__)
#pragma GCC diagnostic pop
#endif

} /* namespace galera */

#endif // GALERA_DATA_SET_HPP
