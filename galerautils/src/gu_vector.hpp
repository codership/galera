// Copyright (C) 2013 Codership Oy <info@codership.com>

/*!
 * @file implementation of STL vector functionality "on the stack", that is
 *       with initial buffer for allocations reserved inside the object:
 *
 *       gu::Vector<int, 16> v;
 *       v().resize(5);  // uses internal buffer (in this case on the stack)
 *       v().resize(20); // overflows into heap
 *
 * In many cases, when the number of elements in a vector is predictably low or
 * even known exactly, this will save us from going to heap just to allocate
 * few elements.
 *
 * Rather than manually rewriting all std::vector methods, we return
 * a reference to std::vector object via operator(). operator[] is also
 * rewritten to provide the familiar v[i] interface.
 *
 * $Id$
 */

#ifndef _GU_VECTOR_HPP_
#define _GU_VECTOR_HPP_

#include "gu_reserved_container.hpp"
#include <vector>

namespace gu
{

/* gu::VectorBase is an interface to generalize gu::Vector template over
 * capacity so that it is possible to pass gu::Vector<T,capacity> objects
 * by reference to gu::VectorBase<T> */
template <typename T>
class VectorBase
{
public:

    typedef T        value_type;
    typedef T&       reference;
    typedef const T& const_reference;
    typedef size_t   size_type;

    virtual       reference operator[] (size_type i)       = 0;
    virtual const_reference operator[] (size_type i) const = 0;

    virtual size_type size    () const = 0;
    virtual void      reserve (size_type n) = 0;
    virtual void      resize  (size_type n, value_type val = value_type()) = 0;

    // Now iterators, which I have no time for ATM. Leaving unfinished.

protected:

    VectorBase() {}
    virtual ~VectorBase() {}
};

template <typename T, typename VectorBase<T>::size_type capacity>
class Vector : public VectorBase<T>
{
public:

    Vector() : VectorBase<T>(), rv_() {}

    Vector(const Vector& other) : VectorBase<T>(), rv_()
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

    typedef typename VectorBase<T>::size_type size_type;

          T& operator[] (size_type i)       { return operator()()[i]; }
    const T& operator[] (size_type i) const { return operator()()[i]; }

    size_type size    () const      { return operator()().size();     }
    void      reserve (size_type n) {        operator()().reserve(n); }

    typedef typename VectorBase<T>::value_type value_type;

    void      resize  (size_type n, value_type val = value_type())
    {
        operator()().resize(n, val);
    }

    bool in_heap() const // for testing
    {
        return (rv_.reserved_buffer() != &rv_.container()[0]);
    }

private:

    ReservedContainer<ContainerType, capacity> rv_;

}; /* class Vector*/

} /* namespace gu */

#endif /* _GU_VECTOR_HPP_ */
