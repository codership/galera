// Copyright (C) 2009 Codership Oy <info@codership.com>

#ifdef GALERA_USE_BOOST_POOL_ALLOC

#include "gu_buffer.hpp"
#include <boost/pool/pool_alloc.hpp>

using namespace std;
using namespace gu;

static boost::fast_pool_allocator<Buffer> btype_pool;

void* gu::Buffer::operator new(size_t sz)
{
    return btype_pool.allocate();
}

void gu::Buffer::operator delete(void* ptr)
{
    btype_pool.deallocate(static_cast<Buffer*>(ptr));
}

#endif // GALERA_USE_BOOST_POOL_ALLOC
