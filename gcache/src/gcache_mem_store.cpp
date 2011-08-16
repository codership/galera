/*
 * Copyright (C) 2010-2011 Codership Oy <info@codership.com>
 */

#include "gcache_mem_store.hpp"

namespace gcache
{

bool
MemStore::have_free_space (ssize_t size) throw()
{
    while (size_ + size > max_size_) /* try to free some released bufs*/
    {
        BufferHeader* bh = ptr2BH (seqno2ptr_.begin()->second);

        if (BH_is_released(bh)) /* discard buffer */
        {
            seqno2ptr_.erase(seqno2ptr_.begin());
            bh->seqno = SEQNO_NONE;

            switch (bh->store)
            {
            case BUFFER_IN_RB:
                bh->ctx->discard(bh);
                break;
            case BUFFER_IN_MEM:
                discard(bh);
                break;
            }
        }
        else /* no more buffers to discard */
        {
            return false;
        }
    }

    return true;
}

} /* namespace gcache */
