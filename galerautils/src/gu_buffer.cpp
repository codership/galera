
#include "gu_buffer.hpp"
#include "gu_lock.hpp"

#include <boost/pool/pool.hpp>

#include <new>

using namespace std;
using namespace gu;

#ifdef GU_BUFFER_MEMPOOL

static bool thread_safe = false;
static Mutex mutex;
static boost::pool<> btype_pool(sizeof(Buffer));

void* gu::Buffer::operator new(size_t sz)
{
    
    void* ret;
    if (thread_safe == true)
    {
        Lock lock(mutex);
        ret = btype_pool.malloc();
    }
    else
    {
        ret = btype_pool.malloc();
    }
    if (ret == 0)
    {
        throw std::bad_alloc();
    }
    return ret;
}

void gu::Buffer::operator delete(void* ptr)
{
    if (thread_safe == true)
    {
        Lock lock(mutex);
        btype_pool.free(ptr);
    }
    else
    {
        btype_pool.free(ptr);
    }
}


void BufferMempool::set_thread_safe(bool val)
{
    thread_safe = val;
}

#endif // GU_BUFFER_MEMPOOL
