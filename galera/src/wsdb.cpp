//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#include "wsdb.hpp"
#include "wsdb_wsdb.hpp"
#include "galera_wsdb.hpp"

using namespace std;

galera::Wsdb* galera::Wsdb::create(const string& conf)
{
    if (conf == "wsdb")
        return new WsdbWsdb();
    else if (conf == "galera")
        return new GaleraWsdb();
    gu_throw_fatal << "not implemented: " << conf;
    throw;
}


