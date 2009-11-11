/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#ifndef _GCOMM_EXCEPTION_HPP_
#define _GCOMM_EXCEPTION_HPP_

#include "gu_throw.hpp"

#define gcomm_assert(cond_)                    \
    if ((cond_) == false) gu_throw_fatal << #cond_ << ": "

#endif // _GCOMM_EXCEPTION_HPP_
