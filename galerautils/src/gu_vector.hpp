// Copyright (C) 2013 Codership Oy <info@codership.com>

/**
 * @file implementation of STL vector functionality "on the stack", that is
 *       with a data buffer reserved inside the object:
 *
 *       gu::Vector<int, 16> v;
 *       v().resize(5);  // uses internal buffer (in this case on the stack)
 *       v().resize(20); // overflows into heap
 *
 * Rather than manually rewriting all std::vector methods, we return
 * a reference to vector object via () operator.
 *
 * $Id$
 */

#ifndef _GU_VECTOR_HPP_
#define _GU_VECTOR_HPP_

#include "gu_reserved_container.hpp"
#include <vector>

namespace gu
{

template <typename T, size_t capacity>
class Vector
{
public:

    Vector() : rv_() {}

    Vector(const Vector& other) : rv_()
    {
        rv_.container().assign(other->begin(), other->end());
    }

    Vector& operator= (Vector other)
    {
        using namespace std;
        swap(other);
        return *this;
    }

    typedef ReservedAllocator<T, capacity> Allocator;
    typedef std::vector<T, Allocator>      ContainerType;

          ContainerType& operator() ()       { return rv_.container(); }
    const ContainerType& operator() () const { return rv_.container(); }

          ContainerType* operator-> ()       { return rv_.operator->(); }
    const ContainerType* operator-> () const { return rv_.operator->(); }

          T& operator[] (size_t i)       { return operator()()[i]; }
    const T& operator[] (size_t i) const { return operator()()[i]; }

    bool in_heap() const // for testing
    {
        return (rv_.reserved_buffer() != &rv_.container()[0]);
    }

private:

    ReservedContainer<ContainerType, capacity> rv_;

}; /* class Vector*/

} /* namespace gu */

#endif /* _GU_VECTOR_HPP_ */
