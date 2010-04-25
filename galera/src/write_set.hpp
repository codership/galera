//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//


#ifndef GALERA_WRITE_SET_HPP
#define GALERA_WRITE_SET_HPP

extern "C"
{
#include "wsdb_api.h"
}

#include "wsrep_api.h"
#include "gu_buffer.hpp"


namespace galera
{

    class WriteSet
    {
    public:
        WriteSet() { }
        virtual ~WriteSet() { }
        virtual enum wsdb_ws_type get_type() const = 0;
        virtual enum wsdb_ws_level get_level() const = 0;
        virtual wsrep_seqno_t get_last_seen_trx() const = 0;
        virtual const gu::Buffer& get_rbr() const = 0;
        virtual bool empty() const = 0;
        virtual void serialize(gu::Buffer&) const = 0;
    private:
    };
}


#endif // GALERA_WRITE_SET_HPP
