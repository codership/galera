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
#include <string>


namespace galera
{
    class WriteSetNG
    {
    };

    class WriteSetOut
    {
    public:

        WriteSetOut (const std::string& base_name,
                     int ver,
                     size_t max_size = 0x7fffffff)
            :
            version_(version),
            keys_(base_name + "_keys", ws_to_ks_version(ver)),
            data_(base_name + "_data", ws_to_ds_version(ver)),
            unrd_(base_name + "_unrd", ws_to_ds_version(ver)),
            left_(max_size - keys_.size() - data_.size() - unrd_.size()
                  - header_size(version_))
        {}

        ~WriteSetOut() {}


        void append_key(const KeyData& k)
        {
            keys_.append(k);
        }

        void append_data(const void*data, size_t data_len, bool store)
        {
            data_.append(data, data_len, store);
        }

        void append_unordered(const void*data, size_t data_len, bool store)
        {
            unrd_.append(data, data_len, store);
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
