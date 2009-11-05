/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

/*!
 * Byte buffer class. This is thin wrapper to std::vector
 */

#ifndef GU_BUFFER_HPP
#define GU_BUFFER_HPP

#include <vector>

#include "gu_throw.hpp"

#define GU_BUFFER_MEMPOOL 1

namespace gu
{
    typedef unsigned char byte_t;
    
    class Buffer
    {
    private:
        
        typedef std::vector<byte_t> BType;
    public:
        typedef byte_t value_type;
        typedef BType::iterator iterator;
        typedef BType::const_iterator const_iterator;

        explicit Buffer(size_t sz = 0) : 
            buf(sz)
        { }
        
        template <class InputIterator>
        Buffer(InputIterator first, InputIterator last) :
            buf(first, last)
        { }
        
        size_t size() const { return buf.size(); }
        void resize(size_t sz) { buf.resize(sz); }
        size_t capacity() const { return buf.capacity(); }
        void reserve(size_t sz) { buf.reserve(sz); }
        template <class InputIterator>
        void insert(iterator position, InputIterator first, InputIterator last)
        { buf.insert(position, first, last); }
        void clear() { buf.clear(); }
        void erase(iterator first, iterator last) { buf.erase(first, last); }
        iterator begin() { return buf.begin(); }
        
        iterator end() { return buf.end(); }
        const_iterator begin() const { return buf.begin(); }
        const_iterator end()   const { return buf.end(); }
        
        value_type& operator[](size_t i) { return buf[i]; }
        const value_type& operator[](size_t i) const { return buf[i]; }
        value_type& at(size_t i) { return buf.at(i); }
        const value_type& at(size_t i) const { return buf.at(i); }
        
#ifdef GU_BUFFER_MEMPOOL
        void* operator new(size_t);
        void operator delete(void* );
#endif
        bool operator==(const Buffer& cmp) const
        { return (cmp.buf == buf); }
    private:
        BType buf;
    };

#ifdef GU_BUFFER_MEMPOOL
    class BufferMempool
    {
    public:
        static void set_thread_safe(bool);
    };
#endif


    
}




#endif // GU_BUFFER_HPP
