// Copyright (C) 2013 Codership Oy <info@codership.com>

/**
 * @file operator << for hexdumps.
 *
 * Usage: std::cout << gu::Hexdump(ptr, size)
 *
 * $Id$
 */

#ifndef _GU_HEXDUMP_HPP_
#define _GU_HEXDUMP_HPP_

#include "gu_types.hpp"

#include <ostream>

namespace gu {

class Hexdump
{
public:

    Hexdump (const void* const buf,
             size_t const      size,
             bool const        alpha = false)
        :
        buf_  (static_cast<const byte_t*>(buf)),
        size_ (size),
        alpha_(alpha)
    {}

    std::ostream& to_stream (std::ostream& os) const;

    // according to clang C++98 wants copy ctor to be public for temporaries
    Hexdump (const Hexdump& h) : buf_(h.buf_), size_(h.size_), alpha_(h.alpha_)
    {}

private:

    const byte_t* const buf_;
    size_t const        size_;
    bool const          alpha_;

    Hexdump& operator = (const Hexdump&);
};

inline std::ostream&
operator << (std::ostream& os, const Hexdump& h)
{
    return h.to_stream(os);
}

}

#endif /* _GU_HEXDUMP_HPP_ */
