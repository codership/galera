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
    typedef std::vector<byte_t> Buffer;
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
