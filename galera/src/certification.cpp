//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#include "certification.hpp"
#include "wsdb_certification.hpp"


galera::Certification* galera::Certification::create(const std::string& conf)
{
    return new WsdbCertification();
}
