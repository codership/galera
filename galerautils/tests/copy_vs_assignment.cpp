// Copyright (C) 2017 Codership Oy <info@codership.com>
// This program compares performance of assignemnt operator vs. std::copy()
// with both aligned and unaligned arguments.
/*
 * Overall findings are (GCC-6.3, clang-3.8):
 * - 8-byte alignment is as good as 16-byte for 16-byte args.
 * - as rule of thumb std::copy() is MUCH slower on debug builds,
 *   while ::memcpy() is not.
 * - as rule of thumb std::copy() is as fast (or faster) on optimized builds.
 *   In particular:
 *   - on x86/GCC std::copy() is 2-5x faster than = operator for 16-byte args.
 *   - on armhf/GCC std::copy() is 2 orders of magniture faster than =
 *     operator for 8-byte unaligned args and ~2x slower for 8-byte aligned.
 * - on x86 non-aligned access is significatntly (3x) slower than aligned,
 *   there is almost no dependence of the penalty on the argument size.
 * - on armhf there is no difference between aligned and non-aligned
 *   access except in case of 8 and 16 byte sized arguments.
 * - non-aligned 8-byte assignement on armhf is TWO ORDERS of magnitude slower
 *   than std::copy() - probably due to a bug in GCC, clang does not show it.
 * - clang does not optimize std::copy() as good as GCC.
 * - GCC optimization is very sensitive to syntax. Something that is logically
 *   equivalent may compile into something 4x slower. See examples below.
 *
 * Conclusions:
 * 1) default assignment operator drawbacks:
 *    a) slower at bigger (bigger than the word size) data types
 *    b) much slower and/or bus error/unknown command on non-aligned access
 *       on some platforms (armhf/clang)
 * 2) std::copy() drawbacks:
 *    a) much lower on non-optimized builds
 *    b) optimization is sensitive to exact syntax, seemingly equivalent
 *       expression may lead to 2x worse performance.
 * 3) ::memcpy() drawback:
 *    it can be marginally slower than std::copy() (few percent) on optimized
 *    builds.
 *
 * WINNER: ::memcpy()!
 *
 */

#define NDEBUG 1

#include <iostream>
#include <algorithm>
#include <sys/time.h>
#include <cstring> // memcmp(), memcpy()

#include "../src/gu_arch.h"
#include "../src/gu_uuid.hpp"

static double time_diff(const struct timeval& l,
                        const struct timeval& r)
{
    double const left(double(l.tv_usec)*1.0e-06 + l.tv_sec);
    double const right(double(r.tv_usec)*1.0e-06 + r.tv_sec);
    return left - right;
}

enum METHOD
{
    ASSIGN,
    STDCOPY,
    MEMCPY
};

template <typename T, bool aligned, METHOD m>
double timing()
{
    std::string method;

    switch (m)
    {
    case ASSIGN:  method = "assignment";  break;
    case STDCOPY: method = "std::copy()"; break;
    case MEMCPY:  method = "::memcpy()";  break;
    }

    std::cout << "Timing " << (aligned ? "aligned " : "non-aligned ")
              << sizeof(T) << "-byte " << method << ":\t" << std::flush;

    int const loops((1 << 16));
    static int const arr_size(1024);
    int const increment(aligned ? std::min(sizeof(T), GU_WORD_BYTES) : 1);
    char a1[arr_size + sizeof(T)] = { 1, }, a2[arr_size + sizeof(T)] = { 2, };
    struct timeval tv_start, tv_end;

    gettimeofday(&tv_start, NULL);

    for (int l(0); l < loops; ++l)
    {
        int i(0);
        int j(arr_size / 2);
        for (int k(0); k < arr_size; ++k)
        {
            T t1((T())), t2((T()));
            T* const p1i(reinterpret_cast<T*>(a1 + i));
            T* const p2j(reinterpret_cast<T*>(a2 + j));

            switch (m)
            {
            case ASSIGN:
                t1 = *p1i;
                t2 = *p2j;
                *p1i = t2;
                *p2j = t1;
                break;
            case STDCOPY:
                std::copy(p1i, p1i + 1, &t1);
                std::copy(p2j, p2j + 1, &t2);
                std::copy(&t1, &t1 + 1, p2j);
                std::copy(&t2, &t2 + 1, p1i);
                break;
            case MEMCPY:
                ::memcpy(&t1, p1i, sizeof(T));
                ::memcpy(&t2, p2j, sizeof(T));
                ::memcpy(p2j, &t1, sizeof(T));
                ::memcpy(p1i, &t2, sizeof(T));
                break;
            }

            i = (i + increment) % arr_size;
            j = (j + increment) % arr_size;
        }
    }

    gettimeofday(&tv_end, NULL);

    double const ret(time_diff(tv_end, tv_start));

    std::cout <<  ret << std::endl;

    return ret;
}

template <typename T>
void timing_type()
{
    double a, c, m __attribute__((unused));
    a = timing<T, false, ASSIGN> ();
    c = timing<T, false, STDCOPY>();
    m = timing<T, false, MEMCPY> ();
    std::cout << "Diff(ass/copy): " << 2*(a - c)/(a + c) << std::endl;
    a = timing<T, true,  ASSIGN> ();
    c = timing<T, true,  STDCOPY>();
    m = timing<T, true,  MEMCPY> ();
    std::cout << "Diff(ass/copy): " << 2*(a - c)/(a + c) << std::endl;
}

/* a 16-byte type to compare assignment operator and std::copy() */
struct hexe
{
    char a[16];

    GU_FORCE_INLINE
    hexe& operator= (const hexe& h)
    {
        ::memcpy(a, h.a, sizeof(a));
//        const char* const from(h.a);
//        std::copy(from, from + sizeof(a), a);
// Surprisingly the following syntaxes turn out MUCH slower with GCC while
// clang does not show anything like that, but then clang is generally slower
//          std::copy(h.a, h.a + sizeof(a), a);
//          std::copy(&h.a[0], &h.a[0 + sizeof(a)], a);
//          std::copy(&h.a[0], &h.a[0] + sizeof(a), a);
//if (::memcmp(&h.a[0], &a[0], 16)) { abort(); }
        return *this;
    }
};

int main()
{
    timing_type<char>();
    timing_type<short>();
    timing_type<int>();
    timing_type<long long>();
    timing_type<hexe>();
    timing_type<gu::UUID>();

    return 0;
}
