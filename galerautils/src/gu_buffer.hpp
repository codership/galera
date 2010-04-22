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


#include <boost/shared_ptr.hpp>
#ifdef GALERA_USE_BOOST_POOL_ALLOC
#include <boost/pool/pool_alloc.hpp>
#endif // GALERA_USE_BOOST_POOL_ALLOC




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
        
#ifdef GALERA_USE_BOOST_POOL_ALLOC
        void* operator new(size_t);
        void operator delete(void* );
#endif // GALERA_USE_BOOST_POOL_ALLOC
        bool operator==(const Buffer& cmp) const
        { return (cmp.buf == buf); }
    private:
        BType buf;
    };    

    class BufferDeleter
    {
    public:
        BufferDeleter() { }
        BufferDeleter(const BufferDeleter& bt) { }
        ~BufferDeleter() { }
        void operator()(Buffer* b) { delete b; }
    private:
        void operator=(const BufferDeleter&);
    };


    typedef boost::shared_ptr<Buffer> SharedBuffer;
#ifdef GALERA_USE_BOOST_POOL_ALLOC
    typedef boost::fast_pool_allocator<SharedBuffer> SharedBufferAllocator;
    extern SharedBufferAllocator shared_buffer_allocator;
#else
#include <memory>
    extern std::allocator<SharedBuffer> shared_buffer_allocator;
#endif // GALERA_USE_BOOST_POOL_ALLOC

}




#endif // GU_BUFFER_HPP
