/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#ifndef _GCOMM_TYPES_HPP_
#define _GCOMM_TYPES_HPP_


#include "gcomm/exception.hpp"

#include "gu_buffer.hpp"

#include <sstream>
#include <algorithm>
#include <string>


namespace gcomm
{

    /*!
     * Serialize template function
     */
    template <typename T> 
    inline size_t serialize(const T      val, 
                            gu::byte_t*      buf, 
                            size_t const buflen, 
                            size_t const offset)
    {
        if (buflen < sizeof(T) + offset)
            gu_throw_error (EMSGSIZE) << sizeof(T) << " > " 
                                           << (buflen-offset);
        
#if __BYTE_ORDER == __LITTLE_ENDIAN
        *reinterpret_cast<T*>(buf + offset) = val;
#elif __BYTE_ORDER == __BIG_ENDIAN
#error "Big endian not supported yet"
#else
#error "Byte order unrecognized"
#endif // __BYTE_ORDER
        return offset + sizeof(T);
    }

    
    /*!
     * Unserialize template function
     */
    template <typename T> 
    inline size_t unserialize(const gu::byte_t* buf, 
                              size_t const  buflen, 
                              size_t const  offset, 
                              T* ret)
        throw (gu::Exception)
    {
        if (buflen < sizeof(T) + offset)
            gu_throw_error (EMSGSIZE) << sizeof(T) << " > " 
                                           << (buflen-offset);
        
#if __BYTE_ORDER == __LITTLE_ENDIAN
        *ret = *reinterpret_cast<const T*>(buf + offset);
#elif __BYTE_ORDER == __BIG_ENDIAN
#error "Big endian not supported yet"
#else
#error "Byte order unrecognized"
#endif // __BYTE_ORDER
        return offset + sizeof(T);
    }
    
    /*!
     * Template function to return serializable size.
     */
    template <typename T> 
    inline size_t serial_size(const T t)
    {
        return sizeof(t);
    }
    
    
    template <size_t SZ> 
    class String
    {
    public:
        String(const std::string& str_ = "") : str(str_) 
        { 
            if (str.size() > str_size)
            {
                gu_throw_error(EMSGSIZE);
            }
            
        }
        
        virtual ~String() { }
        
        size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset) 
            const throw(gu::Exception)
        {
            if (buflen < offset + str_size)
            {
                gu_throw_error (EMSGSIZE) << str_size
                                               << " > " << (buflen-offset);
            }
            std::string ser_str(str);
            ser_str.resize(str_size, '\0');
            (void)std::copy(ser_str.data(), ser_str.data() + ser_str.size(), 
                            buf + offset);
            return offset + str_size;
        }
        
        size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset)
            throw(gu::Exception)
        {
            if (buflen < offset + str_size)
            {
                gu_throw_error (EMSGSIZE) << str_size
                                               << " > " << (buflen-offset);
            }
            str.assign(reinterpret_cast<const char*>(buf) + offset, str_size);
            const size_t tc(str.find_first_of('\0'));
            if (tc != std::string::npos)
            {
                str.resize(tc);
            }
            return offset + str_size;
        }
        
        static size_t serial_size()
        {
            return str_size;
        }

        const std::string& to_string() const { return str; }

        bool operator==(const String<SZ>& cmp) const
        { return (str == cmp.str); }
        
    private:
        static const size_t str_size = SZ ;
        std::string str; /* Human readable name if any */
    };

    template <size_t SZ>
    inline std::ostream& operator<<(std::ostream& os, const String<SZ>& str)
    { return (os << str.to_string()); }
    
} // namespace gcomm

#endif /* _GCOMM_TYPES_HPP_ */
