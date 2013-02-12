//
// Copyright (C) 2013 Codership Oy <info@codership.com>
//

#ifndef GALERA_KEY_DATA_HPP
#define GALERA_KEY_DATA_HPP

#include "wsrep_api.h"

namespace galera
{

struct KeyData
{
    const wsrep_buf_t* const key_parts;
    long const               key_parts_num;
    int  const               proto_ver;
    bool const               nocopy;
    bool const               shared;

    KeyData (int const pv, const wsrep_buf_t* const k,
             long const kn, bool const nc, bool const sh)
        : key_parts    (k),
          key_parts_num(kn),
          proto_ver    (pv),
          nocopy       (nc),
          shared       (sh)
    {}

private:

    KeyData (const KeyData&);
    KeyData& operator = (const KeyData&);

}; /* struct KeyData */

} /* namespace galera */

#endif /* GALERA_KEY_DATA_HPP */
