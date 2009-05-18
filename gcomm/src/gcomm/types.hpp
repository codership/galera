#ifndef TYPES_H
#define TYPES_H

#include <gcomm/common.hpp>
#include <gcomm/string.hpp>

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>

#include <sstream>
using std::ostringstream;


BEGIN_GCOMM_NAMESPACE

template <class T> 
inline size_t read(const void* buf, const size_t buflen, 
                   const size_t offset, T* ret)
{
    if (buflen < sizeof(T) + offset)
    {
        return 0;
    }
#if __BYTE_ORDER == __LITTLE_ENDIAN
    *ret = *reinterpret_cast<T*>((char*) buf + offset);
#elif __BYTE_ORDER == __BIG_ENDIAN
#error "Big endian not supported yet"
#else
#error "Byte order unrecognized"
#endif // __BYTE_ORDER
    return offset + sizeof(T);
}

template <class T> 
inline size_t write(const T val, void* buf, const size_t buflen, 
                    const size_t offset)
{
    if (buflen < sizeof(T) + offset)
    {
        return 0;
    }
#if __BYTE_ORDER == __LITTLE_ENDIAN
    *reinterpret_cast<T*>((char*) buf + offset) = val;
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
    explicit IntType()
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
    
    size_t read(const void* from, const size_t fromlen, 
                const size_t from_offset)
    {
        return gcomm::read(from, fromlen, from_offset, &t);
    }
    
    size_t write(void* to, const size_t tolen, const size_t to_offset) const
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



struct Int8 : IntType<int8_t> 
{
    Int8(int8_t t) : IntType<int8_t>(t) {}
};

struct Int16 : IntType<int16_t>
{
    Int16(int16_t t) : IntType<int16_t>(t) {}
};

struct Int32 : IntType<int32_t>
{
    Int32(int32_t t) : IntType<int32_t>(t) {}
};

struct Int64 : IntType<int64_t>
{
    Int64(int64_t t) : IntType<int64_t>(t) {}
};

struct UInt8 : IntType<uint8_t>
{
    UInt8() : IntType<uint8_t>() {}
    UInt8(const uint8_t t) : IntType<uint8_t>(t) {}
};

struct UInt16 : IntType<uint16_t>
{
    UInt16() : IntType<uint16_t>() {}
    UInt16(const uint16_t t) : IntType<uint16_t>(t) {}
};

struct UInt32 : IntType<uint32_t>
{
    UInt32() : IntType<uint32_t>() {}
    UInt32(const uint32_t t) : IntType<uint32_t>(t) {}
};

struct UInt64 : IntType<uint64_t>
{
    UInt64() : IntType<uint64_t>() {}
    UInt64(const uint64_t t) : IntType<uint64_t>(t) {}
};

struct Int : IntType<int>
{
    Int(const int t) : IntType<int>(t) {}
};

struct Long : IntType<long>
{
    Long(const long t) : IntType<long>(t) {}
};

struct Size : IntType<size_t>
{
    Size(const size_t t) : IntType<size_t>(t) {}
};

struct Bool : IntType<bool>
{
    Bool(const bool t) : IntType<bool>(t) {}
};


template<class T>
inline IntType<T> make_int(T t)
{
    return IntType<T>(t);
}


class Double
{
    double d;
public:

    Double()
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
    Pointer() {}
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
    Sockaddr() {}
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
            os << sa.sa_data[i];
        }
        ret += ")";
        return ret;
    }

};


END_GCOMM_NAMESPACE

#endif /* TYPES_H */
