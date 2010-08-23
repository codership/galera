/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

/*!
 * @file exception.hpp 
 *
 * @brief GComm exception definitions.
 */

#ifndef GCOMM_EXCEPTION_HPP
#define GCOMM_EXCEPTION_HPP

#include "gu_throw.hpp"

/*!
 * Assert macro for runtime condition checking. This should be used
 * for conditions that may depend on external input and are required 
 * to validate correct protocol operation.
 */
#define gcomm_assert(cond_)                                     \
    if ((cond_) == false) gu_throw_fatal << #cond_ << ": "

#endif // GCOMM_EXCEPTION_HPP
