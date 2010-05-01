//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#include "certification.hpp"
#include "wsdb_certification.hpp"
#include "galera_certification.hpp"

galera::Certification* galera::Certification::create(const std::string& conf)
{
    if (conf == "wsdb")
        return new WsdbCertification();
    else if (conf == "galera")
        return new GaleraCertification();
    gu_throw_fatal << "not implemented: " << conf;
    throw;
}
