#ifndef _GCOMM_TYPES_HPP_
#define _GCOMM_TYPES_HPP_

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>

#include <sstream>
using std::ostringstream;

#include <gcomm/common.hpp>
#include <gcomm/string.hpp>

BEGIN_GCOMM_NAMESPACE

typedef unsigned char byte_t;

template <class T> 
inline size_t read(const byte_t* buf, const size_t buflen, 
                   const size_t offset, T* ret)
{
    if (buflen < sizeof(T) + offset)
    {
        return 0;
    }
#if __BYTE_ORDER == __LITTLE_ENDIAN
    *ret = *reinterpret_cast<const T*>(buf + offset);
#elif __BYTE_ORDER == __BIG_ENDIAN
#error "Big endian not supported yet"
#else
#error "Byte order unrecognized"
#endif // __BYTE_ORDER
    return offset + sizeof(T);
}

template <class T> 
inline size_t write(const T val, byte_t* buf, const size_t buflen, 
                    const size_t offset)
{
    if (buflen < sizeof(T) + offset)
    {
        return 0;
    }
#if __BYTE_ORDER == __LITTLE_ENDIAN
    *reinterpret_cast<T*>(buf + offset) = val;
#elif __BYTE_ORDER == __BIG_ENDIAN
#error "Big endian not supported yet"
#else
#error "Byte order unrecognized"
#endif // __BYTE_ORDER
    return offset + sizeof(T);
}


template <class T> class IntType
{
    T t;
public:
    IntType() :
        t()
    {
    }
    IntType(const T t_) : 
        t(t_)
    {
    }

    T get() const
    {
        return t;
    }
    
    string to_string() const
    {
        ostringstream os;
        os << t;
        return os.str();
    }
    
    size_t read(const byte_t* from, const size_t fromlen, 
                const size_t from_offset)
    {
        return gcomm::read(from, fromlen, from_offset, &t);
    }
    
    size_t write(byte_t* to, const size_t tolen, const size_t to_offset) const
    {
        return gcomm::write(t, to, tolen, to_offset);
    }
    
    static size_t size()
    {
        return sizeof(T);
    }
};


template<class T> 
inline bool operator==(const IntType<T> a, const IntType<T> b)
{
    return a.get() == b.get();
}

template<class T> 
inline bool operator!=(const IntType<T> a, const IntType<T> b)
{
    return a.get() != b.get();
}


template<class T> 
inline bool operator<(const IntType<T> a, const IntType<T> b)
{
    return a.get() < b.get();
}

template<class T> 
inline bool operator>(const IntType<T> a, const IntType<T> b)
{
    return a.get() > b.get();
}

template<class T> 
inline bool operator<=(const IntType<T> a, const IntType<T> b)
{
    return a.get() <= b.get();
}

template<class T> 
inline bool operator>=(const IntType<T> a, const IntType<T> b)
{
    return a.get() >= b.get();
}

template<class T>
inline IntType<T> make_int(T t)
{
    return IntType<T>(t);
}


class Double
{
    double d;
public:

    Double() : 
        d()
    {
    }

    Double(const double d_) :
        d(d_)
    {
    }

    string to_string() const
    {
        ostringstream os;
        
        os << d;
        return os.str();
    }
};

class Pointer 
{
    void* p;
public:
    Pointer() :
        p()
    {
    }
    Pointer(void* const p_) :
        p(p_)
    {
    }
    
    string to_string() const
    {
        ostringstream os;
        os << p;
        return os.str();
    }
};

class Sockaddr
{
    sockaddr sa;
public:
    Sockaddr() :
        sa()
    {
    }
    Sockaddr(const sockaddr& sa_) :
        sa(sa_)
    {
    }

    string to_string() const
    {
        string ret("sockaddr(");
        ret += IntType<unsigned short int>(sa.sa_family).to_string();
        ret += ",";
        ostringstream os;
        os.setf(ostringstream::hex);
        for (size_t i = 0; i < sizeof(sa.sa_data); ++i)
        {
            os << IntType<unsigned int>(sa.sa_data[i]).to_string();
        }
        ret += ")";
        return ret;
    }

};



END_GCOMM_NAMESPACE

#endif /* _GCOMM_TYPES_HPP_ */
