/* Copyright (C) 2011-2013 Codership Oy <info@codership.com> */

#ifndef _GARB_CONFIG_HPP_
#define _GARB_CONFIG_HPP_

#include <galerautils.hpp>

#include <string>
#include <iostream>

namespace garb
{

class Config
{
public:

    static std::string const DEFAULT_SST; // default (empty) SST request

    Config (int argc, char* argv[]);
    ~Config () {}

    bool               daemon()  const { return daemon_ ; }
    const std::string& name()    const { return name_   ; }
    const std::string& address() const { return address_; }
    const std::string& group()   const { return group_  ; }
    const std::string& sst()     const { return sst_    ; }
    const std::string& donor()   const { return donor_  ; }
    const std::string& options() const { return options_; }
    const std::string& cfg()     const { return cfg_    ; }
    const std::string& log()     const { return log_    ; }

private:

    bool        daemon_;
    std::string name_;
    std::string address_;
    std::string group_;
    std::string sst_;
    std::string donor_;
    std::string options_;
    std::string log_;
    std::string cfg_;

}; /* class Config */

std::ostream& operator << (std::ostream&, const Config&);

} /* namespace garb */

#endif /* _GARB_CONFIG_HPP_ */
