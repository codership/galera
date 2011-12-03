//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

//
// @todo Check that the at least the following gcc versions are supported
// gcc version 4.1.2 20080704 (Red Hat 4.1.2-48)
//

#ifndef GU_ATOMIC_HPP
#define GU_ATOMIC_HPP

#include <memory>

namespace gu
{
    template <typename I>
    class Atomic
    {
    public:
        Atomic<I>(const I i = 0) : i_(i) { }

        I operator()() const
        {
            __sync_synchronize();
            return i_;
        }

        Atomic<I>& operator=(const I i)
        {
            i_ = i;
            __sync_synchronize();
            return *this;
        }

        I fetch_and_zero()
        {
            return __sync_fetch_and_and(&i_, 0);
        }

        I fetch_and_add(const I i)
        {
            return __sync_fetch_and_add(&i_, i);
        }

        I add_and_fetch(const I i)
        {
            return __sync_add_and_fetch(&i_, i);
        }

        I sub_and_fetch(const I i)
        {
            return __sync_sub_and_fetch(&i_, i);
        }

        Atomic<I>& operator++()
        {
            __sync_fetch_and_add(&i_, 1);
            return *this;
        }
        Atomic<I>& operator--()
        {
            __sync_fetch_and_sub(&i_, 1);
            return *this;
        }

        Atomic<I>& operator+=(const I i)
        {
            __sync_fetch_and_add(&i_, i);
            return *this;
        }
    private:
        I i_;
    };
}

#endif // GU_ATOMIC_HPP
