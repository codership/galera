//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#include "wsdb.hpp"
#if GALERA_USE_WSDB
#include "wsdb_wsdb.hpp"
#endif
#include "galera_wsdb.hpp"

using namespace std;





galera::Wsdb* galera::Wsdb::create(const string& conf)
{
#if GALERA_USE_WSDB
    if (conf == "wsdb")
        return new WsdbWsdb();
#endif
    if (conf == "galera")
        return new GaleraWsdb();
    gu_throw_fatal << "not implemented: " << conf;
    throw;
}


