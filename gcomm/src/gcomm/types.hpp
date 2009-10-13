#ifndef _GCOMM_TYPES_HPP_
#define _GCOMM_TYPES_HPP_

#include <sys/socket.h>


#include <gcomm/common.hpp>
#include <gcomm/exception.hpp>

#include <sstream>
#include <algorithm>
#include <string>

namespace gcomm
{

    typedef unsigned char byte_t;
    
    /*!
     * Serialize template function
     */
    template <typename T> 
    inline size_t serialize(const T      val, 
                            byte_t*      buf, 
                            size_t const buflen, 
                            size_t const offset)
    {
        if (buflen < sizeof(T) + offset)
            gcomm_throw_runtime (EMSGSIZE) << sizeof(T) << " > " 
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
    inline size_t unserialize(const byte_t* buf, 
                              size_t const  buflen, 
                              size_t const  offset, 
                              T* ret)
        throw (gu::Exception)
    {
        if (buflen < sizeof(T) + offset)
            gcomm_throw_runtime (EMSGSIZE) << sizeof(T) << " > " 
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
    
    template <typename I> class IntType
    {
    public:
        
        IntType() : i() { }

        IntType(const I i_) : i(i_) { }
        
        size_t serialize(byte_t* buf, size_t const buflen, size_t const offset)
            const throw(gu::Exception)
        {
            return gcomm::serialize(i, buf, buflen, offset);
        }
        
        size_t unserialize(const byte_t* buf, size_t const buflen,
                           size_t const offset)
            throw(gu::Exception)
        {
            return gcomm::unserialize(buf, buflen, offset, &i);
        }
        
        static size_t serial_size()
        {
            return sizeof(I);
        }

        I get() const { return i; }

        bool operator==(const IntType<I>& cmp) const
        {
            return i == cmp.i;
        }

        bool operator<(const IntType<I>& cmp) const
        {
            return i < cmp.i;
        }

    private:
        I i;
    };

    template <typename I> inline IntType<I> make_int(I i)
    {
        return IntType<I>(i);
    }
    
    
    template <size_t SZ> 
    class String
    {
    public:
        String(const std::string& str_) : str(str_) 
        { 
            if (str.size() != str_size)
            {
                str.resize(str_size);
            }
        }
        
        virtual ~String() { }

        size_t serialize(byte_t* buf, size_t buflen, size_t offset) 
            const throw(gu::Exception)
        {
            if (buflen < offset + str_size)
            {
                gcomm_throw_runtime (EMSGSIZE) << str_size
                                               << " > " << (buflen-offset);
            }
            (void)std::copy(str.data(), str.data() + str_size, buf + offset);
            return offset + str_size;
        }
        
        size_t unserialize(const byte_t* buf, size_t buflen, size_t offset)
            throw(gu::Exception)
        {
            if (buflen < offset + str_size)
            {
                gcomm_throw_runtime (EMSGSIZE) << str_size
                                               << " > " << (buflen-offset);
        }
            str.assign(reinterpret_cast<const char*>(buf) + offset, str_size);
            return offset + str_size;
        }
        
        static size_t serial_size()
        {
            return str_size;
        }
        
    private:
        static const size_t str_size = SZ ;
        std::string str; /* Human readable name if any */
    };
        
    
} // namespace gcomm

#endif /* _GCOMM_TYPES_HPP_ */
