// Copyright (C) 2009 Codership Oy <info@codership.com>

#include "gu_buffer.hpp"
#include "gu_lock.hpp"

#include <boost/pool/pool_alloc.hpp>

#include <new>

using namespace std;
using namespace gu;

#ifdef GU_BUFFER_MEMPOOL


static boost::fast_pool_allocator<Buffer> btype_pool;

void* gu::Buffer::operator new(size_t sz)
{
    return btype_pool.allocate();
}

void gu::Buffer::operator delete(void* ptr)
{
    btype_pool.deallocate(static_cast<Buffer*>(ptr));
}

#endif // GU_BUFFER_MEMPOOL
