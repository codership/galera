//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#include "certification.hpp"
#if GALERA_USE_WSDB
#include "wsdb_certification.hpp"
#endif
#include "galera_certification.hpp"

galera::Certification* galera::Certification::create(const std::string& conf)
{
#if GALERA_USE_WSDB
    if (conf == "wsdb")
        return new WsdbCertification();
#endif
    if (conf == "galera")
        return new GaleraCertification(conf);
    gu_throw_fatal << "not implemented: " << conf;
    throw;
}
