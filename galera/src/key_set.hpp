//
// Copyright (C) 2013 Codership Oy <info@codership.com>
//


#ifndef GALERA_KEY_SET_HPP
#define GALERA_KEY_SET_HPP

#include "gu_rset.hpp"
//#include "gu_vlq.hpp"


namespace galera
{
    class KeySet
    {
    public:

        enum Version
        {
            EMPTY = 0,
            VER1
        };

        static Version const MAX_VER = VER1;

        static Version
        ws_to_ds_version (int ver)
        {
            return VER1;
        }

        class RecordOut {};

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

            size_t serial_size () { return off_ + size_; }

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

    }; /* class KeySet */


    class KeySetOut : public gu::RecordSetOut<KeySet::RecordOut>
    {
    public:

        class Record; /* some dummy template parameter */

        KeySetOut (const std::string& base_name,
                    int                version)
            :
            RecordSetOut (
                base_name,
                check_type      (KeySet::ws_to_ds_version(version)),
                ds_to_rs_version(KeySet::ws_to_ds_version(version))
                ),
            version_(KeySet::ws_to_ds_version(version))
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

            return size_size + size;
        }

        KeySet::Version
        version () { return version_; }

    private:

        // depending on version we may pack data differently
        KeySet::Version       version_;
        gu::UnorderedSet<Key> added_;

        static gu::RecordSet::CheckType
        check_type (KeySet::Version ver)
        {
            switch (ver)
            {
            case KeySet::EMPTY: break; /* Can't create EMPTY KeySetOut */
            case KeySet::VER1:  return gu::RecordSet::CHECK_MMH128;
            }
            throw;
        }

        static gu::RecordSet::Version
        ds_to_rs_version (KeySet::Version ver)
        {
            switch (ver)
            {
            case KeySet::EMPTY: break; /* Can't create EMPTY KeySetOut */
            case KeySet::VER1:  return gu::RecordSet::VER1;
            }
            throw;
        }


    };

    class KeySetIn : public gu::RecordSetIn<KeySet::RecordIn>
    {
    public:

        KeySetIn (KeySet::Version ver, const gu::byte_t* buf, size_t size)
            :
            RecordSetIn(buf, size),
            version_(ver)
        {}

        gu::Buf next () const { return RecordSetIn::next().buf(); }

    private:

        KeySet::Version version_;

    }; /* class KeySetIn */

} /* namespace galera */

#endif // GALERA_KEY_SET_HPP
