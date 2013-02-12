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

//#include "wsrep_api.h"
//#include "gu_buffer.hpp"
//#include "gu_logger.hpp"
//#include "gu_unordered.hpp"

#include <vector>
//#include <deque>
#include <string>

//#include <cstring>

namespace galera
{
    class WriteSetNG
    {
    protected:
        ~WriteSetNG() {}
    };

    class WriteSetOut
    {
    public:
//        typedef std::deque<Key> KeySequence;

        WriteSetOut (std::string& base_name,
                     int version,
                     size_t max_size = 0x7fffffff)
            :
            version_(version),
            keys_(base_name + "_keys", version_),
            data_(base_name + "_data", version_),
            unrd_(base_name + "_unrd", version_),
            left_(max_size - keys_.size() - data_.size() - unrd_.size()
                  - header_size(version_))
        {}

        ~WriteSetOut() {}

        void set_version(int version) { version_ = version; }

        void append_key(const KeyData& k)
        {
            keys_.append(k);
        }

        void append_data(const void*data, size_t data_len, bool store)
        {
            data_.append(data, data_len, store = true);
        }

        void append_unordered(const void*data, size_t data_len, bool store)
        {
            unrd_.append(data, data_len, store = true);
        }

        bool empty() const
        {
            return ((data_.count() + keys_.count() + unrd_.count()) == 0);
        }

        size_t gather(std::vector<Buf>& out)
        {
            out.reserve (out.size() + keys_.page_count() + data_.page_count
                         + unrd_.page_count() + 1 /* global header */);

            prepare_header();
            out.push_back(header_);
            size_t out_size(header_.size);

            out_size += keys_.gather(out);
            out_size += data_.gather(out);
            out_size += unrd_.gather(out);

            return size_;
        }

    private:

        static header_size (int version);

//        typedef gu::UnorderedMultimap<size_t, size_t> KeyRefMap;

        int                version_;
        Buf                header_;
        KeySetOut          keys_;
        DataSetOut         data_;
        DataSetOut         unrd_;
        size_t             left_;
    }; /* class WriteSetOut */

    class WriteSetIn
    {
    public:

        WriteSetIn (const byte_t* ws, size_t size);

        key_count() const { return keys_.count(); }

    private:

        int const version_;
        KeySetIn  keys_;
        DataSetIn data_;
        DataSetIn unrd_;
    };
}


#endif // GALERA_WRITE_SET_HPP
