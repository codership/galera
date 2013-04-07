/*
 * Copyright (C) 2009-2012 Codership Oy <info@codership.com>
 */

#ifndef _GCOMM_TYPES_HPP_
#define _GCOMM_TYPES_HPP_


#include "gcomm/exception.hpp"

#include "gu_byteswap.hpp"
#include "gu_buffer.hpp"

#include <sstream>
#include <algorithm>
#include <string>


namespace gcomm
{
    template <size_t SZ>
    class String
    {
    public:

        String(const std::string& str = "") : str_(str)
        {
            if (str_.size() > str_size_)
            {
                gu_throw_error(EMSGSIZE);
            }
        }

        virtual ~String() { }

        size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset) const
        {
            if (buflen < offset + str_size_)
            {
                gu_throw_error (EMSGSIZE) << str_size_
                                          << " > " << (buflen-offset);
            }
            std::string ser_str(str_);
            ser_str.resize(str_size_, '\0');
            (void)std::copy(ser_str.data(), ser_str.data() + ser_str.size(),
                            buf + offset);
            return offset + str_size_;
        }

        size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset)
        {
            if (buflen < offset + str_size_)
            {
                gu_throw_error (EMSGSIZE) << str_size_
                                          << " > " << (buflen-offset);
            }
            str_.assign(reinterpret_cast<const char*>(buf) + offset, str_size_);
            const size_t tc(str_.find_first_of('\0'));
            if (tc != std::string::npos)
            {
                str_.resize(tc);
            }
            return offset + str_size_;
        }

        static size_t serial_size()
        {
            return str_size_;
        }

        const std::string& to_string() const { return str_; }

        bool operator==(const String<SZ>& cmp) const
        { return (str_ == cmp.str_); }

    private:
        static const size_t str_size_ = SZ ;
        std::string str_; /* Human readable name if any */
    };

    template <size_t SZ>
    inline std::ostream& operator<<(std::ostream& os, const String<SZ>& str)
    { return (os << str.to_string()); }

} // namespace gcomm

#endif /* _GCOMM_TYPES_HPP_ */
