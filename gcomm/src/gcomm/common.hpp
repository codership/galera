/*
 * Copyright (C) 2012 Codership Oy <info@codership.com>
 */

/*!
 * @file common.hpp
 *
 * @brief Imports definitions from the global common.h
 */

#ifndef GCOMM_COMMON_HPP
#define GCOMM_COMMON_HPP

#if defined(HAVE_COMMON_H)
#include <common.h>
#endif

#include <string>

namespace gcomm
{
#if defined(HAVE_COMMON_H)
    static std::string const BASE_PORT_KEY(COMMON_BASE_PORT_KEY);
    static std::string const BASE_PORT_DEFAULT(COMMON_BASE_PORT_DEFAULT);
    static std::string const BASE_DIR_DEFAULT(COMMON_BASE_DIR_DEFAULT);
#else
    static std::string const BASE_PORT_KEY("base_port");
    static std::string const BASE_PORT_DEFAULT("4567");
    static std::string const BASE_DIR_DEFAULT(".");
#endif
}

#endif /* GCOMM_COMMON_HPP */
