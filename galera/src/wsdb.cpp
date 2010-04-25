//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#include "wsdb.hpp"
#include "wsdb_wsdb.hpp"

using namespace std;

galera::Wsdb* galera::Wsdb::create(const string& conf)
{
    return new WsdbWsdb();
}


