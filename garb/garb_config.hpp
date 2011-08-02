/* Copyright (C) 2011 Codership Oy <info@codership.com> */

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

    Config (int argc, char* argv[]) throw (gu::Exception);
    ~Config () {}

    bool               daemon()  const throw() { return daemon_ ; }
    const std::string& address() const throw() { return address_; }
    const std::string& group()   const throw() { return group_  ; }
    const std::string& sst()     const throw() { return sst_    ; }
    const std::string& donor()   const throw() { return donor_  ; }
    const std::string& options() const throw() { return options_; }
    const std::string& cfg()     const throw() { return cfg_    ; }
    const std::string& log()     const throw() { return log_    ; }

private:

    bool        daemon_;
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
