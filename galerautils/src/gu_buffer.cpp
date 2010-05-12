//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#include "gu_buffer.hpp"

#ifdef GALERA_USE_BOOST_POOL_ALLOC
gu::SharedBufferAllocator gu::shared_buffer_allocator;
#else
std::allocator<gu::SharedBuffer> gu::shared_buffer_allocator;
#endif // GALERA_USE_BOOST_POOL_ALLOC
