/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

/*!
 * Byte buffer class. This is thin wrapper to std::vector
 */

#ifndef GU_BUFFER_HPP
#define GU_BUFFER_HPP

#include "gu_types.hpp" // for gu::byte_t

#include <boost/shared_ptr.hpp>
#include <vector>

namespace gu
{
    typedef std::vector<byte_t> Buffer;
    typedef boost::shared_ptr<Buffer> SharedBuffer;
}

#endif // GU_BUFFER_HPP
