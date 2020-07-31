//
// Copyright (C) 2010-2020 Codership Oy <info@codership.com>
//

//
// @todo Check that the at least the following gcc versions are supported
// gcc version 4.1.2 20080704 (Red Hat 4.1.2-48)
//

#ifndef GU_ATOMIC_HPP
#define GU_ATOMIC_HPP

#include "gu_atomic.h"
#include <memory>

namespace gu
{
    template <typename I>
    class Atomic
    {
    public:
        Atomic<I>(I i = 0) : i_(i) { }

        I operator()() const
        {
            I i;
            gu_atomic_get(&i_, &i);
            return i;
        }

        Atomic<I>& operator=(I i)
        {
            gu_atomic_set(&i_, &i);
            return *this;
        }

        I fetch_and_zero()
        {
            return gu_atomic_fetch_and_and(&i_, 0);
        }

        I fetch_and_add(I i)
        {
            return gu_atomic_fetch_and_add(&i_, i);
        }

        I add_and_fetch(I i)
        {
            return gu_atomic_add_and_fetch(&i_, i);
        }

        I sub_and_fetch(I i)
        {
            return gu_atomic_sub_and_fetch(&i_, i);
        }

        Atomic<I>& operator++()
        {
            gu_atomic_fetch_and_add(&i_, 1);
            return *this;
        }
        Atomic<I>& operator--()
        {
            gu_atomic_fetch_and_sub(&i_, 1);
            return *this;
        }

        Atomic<I>& operator+=(I i)
        {
            gu_atomic_fetch_and_add(&i_, i);
            return *this;
        }

        bool operator!=(I i)
        {
            return (operator()() != i);
        }

        bool operator==(I i)
        {
            return (!operator!=(i));
        }

    private:
#if !defined(__ATOMIC_RELAXED)
        // implementation of gu_atomic_get() via __sync_fetch_and_or()
        // is not read-only for GCC
        mutable
#endif
        I i_;
    };
}

#endif // ::GU_ATOMIC_HPP
