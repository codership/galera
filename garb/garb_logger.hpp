/* Copyright (C) 2011 Codership Oy <info@codership.com> */

#ifndef _GARB_LOGGER_HPP_
#define _GARB_LOGGER_HPP_

#include <galerautils.hpp>

#include <string>

namespace garb
{
    extern void set_logfile (const std::string& fname) throw (gu::Exception);
    extern void set_syslog  () throw ();
} /* namespace garb */

#endif /* _GARB_LOGGER_HPP_ */
