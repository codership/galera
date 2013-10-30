// Copyright (C) 2013 Codership Oy <info@codership.com>

/*!
 * @file string class template that allows to allows to allocate initial storage
 *       to hold string data together with the object. If storage is exhausted,
 *       it transparently overflows to heap.
 */

#ifndef _GU_STRING_HPP_
#define _GU_STRING_HPP_

#include "gu_vector.hpp"

#include <string>
#include <new>       // std::bad_alloc
#include <limits>
#include <cstdlib>   // realloc()
#include <cstring>   // strlen(), strcmp()
#include <cstdio>    // snprintf()
#include <iostream>

#include "gu_macros.h" // gu_likely()

namespace gu
{

/* container for a printf()-like format */
struct Fmt
{
    explicit Fmt(const char* f) : fmt_(f) {}
    const char* const fmt_;
};


template <typename T = char>
class StringBase
{
public:

    typedef T        value_type;
    typedef T*       pointer;
    typedef const T* const_pointer;
    typedef size_t   size_type;

    size_type     size()  const { return size_;  }
    size_type     length()const { return size(); }

    pointer       c_str()       { return str_; }
    const_pointer c_str() const { return str_; }

    StringBase& operator<< (const Fmt& f)
    {
        fmt_ = f.fmt_;
        return *this;
    }

    StringBase& operator<< (const StringBase& s)
    {
        size_type const n(s.size());
        append_string (s.c_str(), n);
        return *this;
    }

    StringBase& operator<< (const char* s)
    {
        size_type const n(::strlen(s));
        append_string (s, n);
        return *this;
    }

    StringBase& operator<< (const std::string& s)
    {
        append_string (s.c_str(), s.length());
        return *this;
    }

    StringBase& operator<< (const bool& b)
    {
        // following std::boolalpha
        if (b)
            append_string ("true", 4);
        else
            append_string ("false", 5);

        return *this;
    }

    StringBase& operator<< (const double& d)
    {
        convert ("%f", std::numeric_limits<double>::digits10, d);
        return *this;
    }

    StringBase& operator<< (const void* const ptr)
    {
        /* not using %p here seeing that it may be not universally supported */
        static size_type const ptr_len(sizeof(ptr) == 4 ? 11      : 19       );
        static const char* const fmt(sizeof(ptr) == 4 ? "0x%08lx":"0x%016lx");

        convert (fmt, ptr_len, reinterpret_cast<unsigned long>(ptr));
        return *this;
    }

    StringBase& operator<< (const long long &i)
    {
        convert ("%lld", 21, i);
        return *this;
    }

    StringBase& operator<< (const unsigned long long &i)
    {
        convert ("%llu", 20, i);
        return *this;
    }

    StringBase& operator<< (const int &i)
    {
        convert ("%d", 11, i);
        return *this;
    }

    StringBase& operator<< (const unsigned int &i)
    {
        convert ("%u", 10, i);
        return *this;
    }

    StringBase& operator<< (const short &i)
    {
        convert ("%hd", 6, i);
        return *this;
    }

    StringBase& operator<< (const unsigned short &i)
    {
        convert ("%hu", 5, i);
        return *this;
    }

    StringBase& operator<< (const char &c)
    {
        convert ("%c", 1, c);
        return *this;
    }

    StringBase& operator<< (const unsigned char &c)
    {
        convert ("%hhu", 3, c);
        return *this;
    }

    template <typename X>
    StringBase& operator+= (const X& x) { return operator<<(x); }

    bool operator== (const StringBase& other)
    {
        return (size() == other.size() && !::strcmp(c_str(), other.c_str()));
    }

    bool operator== (const std::string& other)
    {
        return (size() == other.size() && !::strcmp(c_str(), other.c_str()));
    }

    bool operator== (const char* s)
    {
        size_type const s_size(::strlen(s));
        return (size() == s_size && !::strcmp(c_str(), s));
    }

    template <typename X>
    bool operator!= (const X& x) { return !operator==(x); }

    void clear() { derived_clear(); };

    StringBase& operator= (const StringBase& other)
    {
        clear();
        append_string (other.c_str(), other.size());
        return *this;
    }

    StringBase& operator= (const char* const other)
    {
        clear();
        append_string (other, ::strlen(other));
        return *this;
    }

protected:

    pointer     str_; // points to an adequately sized memory area
    const char* fmt_;
    size_type   size_;

    virtual void reserve (size_type n) = 0;
    virtual void derived_clear() = 0; // real clear must happen in derived class

    void append_string (const_pointer const s, size_type const n)
    {
        reserve(size_ + n + 1);
        std::copy(s, s + n, &str_[size_]);
        size_ += n;
        str_[size_] = 0;
    }

    template <typename X>
    void convert (const char* const format, size_type max_len, const X& x)
    {
        ++max_len; // add null termination
        reserve(size_ + max_len);
        int const n(snprintf(&str_[size_], max_len, fmt_ ? fmt_ : format, x));
        assert(n > 0);
        assert(size_type(n) < max_len);
        if (gu_likely(n > 0)) size_ += n;
        str_[size_] = 0; // null-terminate even if snprintf() failed.
        fmt_ = NULL;
    }

    StringBase(pointer init_buf) : str_(init_buf), fmt_(NULL), size_(0) {}

    virtual ~StringBase() {}

private:

    StringBase(const StringBase&);

}; /* class StringBase */

template <typename T>
std::ostream& operator<< (std::ostream& os, const gu::StringBase<T>& s)
{
    os << s.c_str();
    return os;
}

template <size_t capacity = 256, typename T = char>
class String : public StringBase<T>
{
public:

    typedef T        value_type;
    typedef T*       pointer;
    typedef const T* const_pointer;
    typedef size_t   size_type;

    String() : StringBase<T>(buf_), reserved_(capacity), buf_()
    {
        buf_[0] = 0;
    }

    explicit
    String(const StringBase<T>& s)
        : StringBase<T>(buf_), reserved_(capacity), buf_()
    {
        append_string (s.c_str(), s.size());
    }

    String(const T* s, size_type n)
        : StringBase<T>(buf_), reserved_(capacity), buf_()
    {
        append_string (s, n);
    }

    explicit
    String(const char* s)
    : StringBase<T>(buf_), reserved_(capacity), buf_()
    {
        size_type const n(strlen(s));
        append_string (s, n);
    }

    explicit
    String(const std::string& s)
        : StringBase<T>(buf_), reserved_(capacity), buf_()
    {
        append_string (s.c_str(), s.length());
    }

#if 0
    String& operator= (String other)
    {
        using namespace std;
        swap(other);
        return *this;
    }
#endif

    template <typename X>
    String& operator= (const X& x)
    {
        base::operator=(x);
        return *this;
    }

    ~String()
    {
        if (base::str_ != buf_) ::free(base::str_);
    }

private:

    size_type  reserved_;
    value_type buf_[capacity];

    typedef StringBase<value_type> base;

    void reserve (size_type const n)
    {
        if (n <= reserved_) return;

        assert (n > capacity);

        bool const overflow(buf_ == base::str_);

        pointer const tmp
            (static_cast<pointer>
             (::realloc(overflow ? NULL : base::str_, n * sizeof(value_type))));

        if (NULL == tmp) throw std::bad_alloc();

        if (overflow) std::copy(buf_, buf_ + base::size_, tmp);

        base::str_ = tmp;
        reserved_  = n;
    }

    void derived_clear()
    {
        if (base::str_ != buf_) ::free(base::str_);

        base::str_  = buf_;
        base::size_ = 0;
        buf_[0]     = 0;
        reserved_   = capacity;
    }

    void append_string (const_pointer s, size_type n)
    {
        base::append_string(s, n);
    }

}; /* class String */

} /* namespace gu */

#endif /* _GU_STRING_HPP_ */
