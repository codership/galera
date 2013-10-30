// Copyright (C) 2013 Codership Oy <info@codership.com>

/*!
 * ReservedContainer template. It is a wrapper for a container and a reserved
 * buffer to allocate elements from.
 *
 * For more rationale see
 * http://src.chromium.org/chrome/trunk/src/base/containers/stack_container.h
 *
 * It is not called "StackContainer" because it is not only for objects
 * allocated on the stack.
 *
 * $Id$
 */

#ifndef _GU_RESERVED_CONTAINER_
#define _GU_RESERVED_CONTAINER_

#include "chromium/aligned_memory.h"

#include "gu_logger.hpp"

#include <cstddef>  // size_t, ptrdiff_t and NULL
#include <cstdlib>  // malloc() and free()
#include <cassert>
#include <new>      // placement new and std::bad_alloc

namespace gu
{

/*!
 * ReservedAllocator is an allocator for STL containers that can use a
 * prealocated buffer (supplied at construction time) for initial container
 * storage allocation. If the number of elements exceeds buffer capacity, it
 * overflows to heap.
 *
 * Unlike the Chromium code, this does not derive from std::allocator, but
 * implements the whole thing.
 *
 * NOTE1: container must support reserve() method.
 *
 * NOTE2: it won't work with containers that require allocator to have default
 *        constructor, like std::basic_string
 */
template <typename T, int reserved, bool diagnostic = false>
class ReservedAllocator
{
public:

    typedef chromium::AlignedBuffer<T, reserved> Buffer;

    typedef T*        pointer;
    typedef const T*  const_pointer;
    typedef T&        reference;
    typedef const T&  const_reference;
    typedef T         value_type;
    typedef size_t    size_type;
    // making size_type unsigned int does not seem to reduce footprint
    typedef ptrdiff_t difference_type;

    template <typename U>
    struct rebind { typedef ReservedAllocator<U, reserved, diagnostic> other; };

          T*  address(T& t)       const { return &t; }
    const T*  address(const T& t) const { return &t; }
    size_type max_size()          const { return size_type(-1)/2/sizeof(T); }

    void construct (T* const p, const T& t) const { new (p) T(t); }
    void destroy   (T* const p)             const { p->~T();      }

    // Storage allocated from this can't be deallocated from other
    bool operator==(const ReservedAllocator& other) const
    {
        return (buffer_ == other.buffer_);
    }

    bool operator!=(const ReservedAllocator& other) const
    {
        return !(*this == other);
    }

    ReservedAllocator(Buffer& buf, size_type n = 0)
        :
        buffer_(&buf),
        used_(n)
    {}

    ReservedAllocator(const ReservedAllocator& other)
    :
    buffer_(other.buffer_),
    used_(other.used_)
    {
//        log_debug << "Copy ctor\n";
    }

    template <typename U, size_type c, bool d>
    ReservedAllocator(const ReservedAllocator<U, c, d>&)
        :
        buffer_(NULL),
        used_(reserved)
    {
//        log_debug << "Rebinding ctor\n";
    }

    ~ReservedAllocator() {}

    T* allocate(size_type const n, void* hint = NULL)
    {
        if (n == 0) return NULL;

        if (reserved - used_ >= n /* && buffer_ != NULL */)
        {
            assert (buffer_ != NULL);

            if (diagnostic)
            { log_info << "Allocating " << n << '/' << (reserved - used_)
                       << " from reserve"; }

            T* const ret(buffer_->base_ptr() + used_);
            used_ += n;
            return ret;
        }

        if (n <= max_size())
        {
            if (diagnostic)
            { log_warn << "Allocating " << n << " from heap"; }

            void* ret = malloc(n * sizeof(T));
            if (NULL != ret) return static_cast<T*>(ret);
        }

        throw std::bad_alloc();
    }

    void deallocate(T* const p, size_type const n)
    {
        if (size_type(p - buffer_->base_ptr()) < reserved)
        {
            assert (used_ > 0);

            if (buffer_->base_ptr() + used_ == p + n)
            {
                /* last allocated buffer, can shrink */
                used_ -= n;
            }
            else
            {
                /* cannot recycle reserved space in this case */
                assert(p + n <= buffer_->base_ptr() + used_);
            }
        }
        else
        {
            free(p);
        }
    }

    size_type used() const { return used_; }

private:

    /* even though we initially allocate buffer in ReservedContainer directly
     * before this, STL containers insist on copying allocators, so we need
     * a pointer to buffer to be an explicit member (and waste another 8 bytes*/
    Buffer*   buffer_;
    size_type used_;

    ReservedAllocator& operator=(const ReservedAllocator&);

}; /* class ReservedAllocator */

/*!
 * ReservedContainer is a wrapper for
 * - fixed size nicely aligned buffer
 * - ReservedAllocator that uses the buffer
 * - container type that uses allocator
 *
 * the point is to have a container allocated on the stack to use stack buffer
 * for element storage.
 */
template <typename ContainerType, int reserved>
class ReservedContainer
{
public:

    ReservedContainer() :
        buffer_   (),
         /* Actual Allocator instance used by container_ should be
          * copy-constructed from the temporary passed to container ctor.
          * Copy-construction preserves pointer to buffer, which is not
          * temporary. This works at least with std::vector */
        container_(Allocator(buffer_))
    {
        /* Make the container use most of the buffer by reserving our buffer
         * size before doing anything else. */
        container_.reserve(reserved);
    }

    /*
     * Getters for the actual container.
     *
     * Danger: any copies of this made using the copy constructor must have
     * shorter lifetimes than the source. The copy will share the same allocator
     * and therefore the same stack buffer as the original. Use std::copy to
     * copy into a "real" container for longer-lived objects.
     */
          ContainerType& container()        { return container_; }
    const ContainerType& container() const  { return container_; }
          ContainerType& operator()()       { return container_; }
    const ContainerType& operator()() const { return container_; }

    /*
     * Support operator-> to get to the container.
     * This allows nicer syntax like:
     *   ReservedContainer<...> foo;
     *   std::sort(foo->begin(), foo->end());
     */
          ContainerType* operator->()       { return &container_; }
    const ContainerType* operator->() const { return &container_; }

    /* For testing only */
    typedef typename ContainerType::value_type ContainedType;
    const ContainedType* reserved_buffer() const { return buffer_.base_ptr(); }

private:

    typedef ReservedAllocator<ContainedType, reserved> Allocator;
    typedef typename Allocator::Buffer                 Buffer;

    Buffer          buffer_;
    ContainerType   container_;
    /* Note that container will use another instance of Allocator, copy
     * constructed from allocator_, so any changes won't be re*/

    ReservedContainer(const ReservedContainer&);
    ReservedContainer& operator=(const ReservedContainer&);

}; /* class ReservedContainer */

} /* namespace gu */

#endif /* _GU_RESERVED_CONTAINER_ */
