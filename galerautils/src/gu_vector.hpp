// Copyright (C) 2013-2019 Codership Oy <info@codership.com>

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
    typedef typename ReservedAllocator<value_type,0>::size_type size_type;

    virtual       reference operator[] (size_type i)       = 0;
    virtual const_reference operator[] (size_type i) const = 0;

    virtual size_type size     () const = 0;
    virtual void      reserve  (size_type n) = 0;
    virtual void      resize   (size_type n, value_type val = value_type()) = 0;
    virtual void      push_back(const value_type& val) = 0;

          reference front()       { return operator[](0); }
    const_reference front() const { return operator[](0); }
          reference back ()       { return operator[](size() - 1); }
    const_reference back () const { return operator[](size() - 1); }

    // Now iterators, which I have no time for ATM. Leaving unfinished.

protected:

    VectorBase() {}
    virtual ~VectorBase() {}
};

/* a base class to be used as a member of other classes */
template <typename T, typename VectorBase<T>::size_type capacity>
class Vector
{
public:

    typedef typename VectorBase<T>::size_type  size_type;
    typedef typename VectorBase<T>::value_type value_type;

    Vector() : rv_() {}

    Vector(const Vector& other) : rv_()
    {
        rv_().assign(other().begin(), other().end());
    }

    Vector& operator= (Vector other)
    {
        using namespace std;
        swap(other);
        return *this;
    }

    typedef ReservedAllocator<T, capacity> Allocator;
    typedef std::vector<T, Allocator>      ContainerType;

          ContainerType& operator() ()       { return container(); }
    const ContainerType& operator() () const { return container(); }

          ContainerType* operator-> ()       { return &container(); }
    const ContainerType* operator-> () const { return &container(); }

          T& operator[] (size_type i)       { return container()[i]; }
    const T& operator[] (size_type i) const { return container()[i]; }

    size_type size    () const      { return container().size();     }
    void      reserve (size_type n) {        container().reserve(n); }

    void      resize  (size_type n, value_type val = value_type())
    {
        container().resize(n, val);
    }

    void push_back (const value_type& val) { container().push_back(val); };

          T& front()       { return container().front(); }
    const T& front() const { return container().front(); }
          T& back ()       { return container().back();  }
    const T& back () const { return container().back();  }

    /* this mehtod must be specialized for each template instantiation */
    size_type serialize(void* buf, size_type size, size_type offset = 0);

    bool in_heap() const // for testing
    {
        return (rv_.reserved_buffer() != &rv_.container()[0]);
    }

private:

    ReservedContainer<ContainerType, capacity> rv_;

          ContainerType& container()       { return rv_.container(); }
    const ContainerType& container() const { return rv_.container(); }

}; /* class Vector*/

/* Vector class derived from VectorBase - to be passed as a parameter */
template <typename T, typename VectorBase<T>::size_type capacity>
class VectorDerived : public VectorBase<T>
{
public:

    typedef typename VectorBase<T>::size_type       size_type;
    typedef typename VectorBase<T>::value_type      value_type;
    typedef typename VectorBase<T>::reference       reference;
    typedef typename VectorBase<T>::const_reference const_reference;

    VectorDerived() : VectorBase<T>(), v_() {}

    template <typename VectorBase<T>::size_type C>
    VectorDerived(const Vector<T,C>& other) : VectorBase<T>(), v_()
    {
        v_().assign(other().begin(), other().end());
    }

          reference operator[] (size_type i)       { return v_[i]; }
    const_reference operator[] (size_type i) const { return v_[i]; }

    size_type size    () const { return v_.size(); }
    void      reserve (size_type n) { v_.reserve(); }
    void      resize  (size_type n, value_type val = value_type()){ v_.resize();}
    void      push_back(const value_type& val) { v_.push_back(); }

private:

    Vector<T, capacity> v_;

}; /* class VectorDerived */

} /* namespace gu */

#endif /* _GU_VECTOR_HPP_ */
