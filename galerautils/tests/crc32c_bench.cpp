/*
 * Copyright (C) 2020 Codership Oy <info@codership.com>
 */

#include "../src/gu_crc32c.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>

#if __cplusplus >= 201103L
#include <chrono>
#else
#include <sys/time.h>
static double time_diff(const struct timeval& l,
                        const struct timeval& r)
{
    double const left(double(l.tv_usec)*1.0e-06 + l.tv_sec);
    double const right(double(r.tv_usec)*1.0e-06 + r.tv_sec);
    return left - right;
}
#endif // C++11

static std::vector<unsigned char> data(1<<21 /* 2M */);

// Initialize data
static class Setup
{
public:
    Setup()
    {
        for (size_t i(0); i < data.size(); ++i)
        {
            data[i] = static_cast<unsigned char>(i);
        }
    }
}
    setup;

static uint32_t
run_bench(size_t const len, size_t const reps)
{
    static const size_t align_loop(sizeof(uint64_t));
    if ((data.size() - len) < align_loop)
        throw std::out_of_range("Too many reps");

    gu_crc32c_t state;
    gu_crc32c_init(&state);

    for (size_t r(0); r < reps; ++r)
        for (size_t i(0); i < align_loop; ++i)
        {
            // here we roll data window over the main data buffer to give equal
            // chance to different alignments
            gu_crc32c_append(&state, &data[i], len);
        }

    return gu_crc32c_get(state);
}

static void
run_bench_with_impl(gu_crc32c_func_t impl,
                    size_t           len,
                    size_t           reps,
                    const char*      comment)
{
    gu_crc32c_func = impl;

    // Run computation once to make complete possible lazy initializations.
    {
        gu_crc32c_t s;
        gu_crc32c_init(&s);
        gu_crc32c_append(&s, "1", 1);
        (void)gu_crc32c_get(s);
    }

#if __cplusplus >= 201103L
    auto start(std::chrono::steady_clock::now());
    auto result(run_bench(len, reps));
    auto stop(std::chrono::steady_clock::now());
    auto duration(std::chrono::duration<double>(stop - start).count());
#else
    struct timeval start, stop;
    gettimeofday(&start, NULL);
    uint32_t result(run_bench(len, reps));
    gettimeofday(&stop,  NULL);
    double const duration(time_diff(stop, start));
#endif // C++11

    std::cout << comment << '\t' << len << '\t'
              << std::fixed << duration << '\t' << result << '\n';
}

static void
one_length(size_t const len, size_t const reps)
{
    std::cout << "\nImpl:   \tBytes:\tDuration:\tResult:\n";

    run_bench_with_impl(gu_crc32c_sarwate,      len, reps, "GU Sarwate ");
    run_bench_with_impl(gu_crc32c_slicing_by_4, len, reps, "GU Slicing4");
    run_bench_with_impl(gu_crc32c_slicing_by_8, len, reps, "GU Slicing8");
#if defined(GU_CRC32C_X86)
    run_bench_with_impl(gu_crc32c_x86,          len, reps, "GU x86_32  ");
#if defined(GU_CRC32C_X86_64)
    run_bench_with_impl(gu_crc32c_x86_64,       len, reps, "GU x86_64  ");
#endif /* GU_CRC32C_X86_64 */
#endif /* GU_CRC32C_X86 */

#if defined(GU_CRC32C_ARM64)
    run_bench_with_impl(gu_crc32c_arm64,        len, reps, "GU arm64   ");
#endif /* GU_CRC32C_X86 */
}

int main()
{
    gu_crc32c_configure(); // compute SW lookup tables

    one_length(11,  1<<22 /* 4M   */);
    one_length(31,  1<<21 /* 2M   */);
    one_length(64,  1<<20 /* 1M   */);
    one_length(512, 1<<17 /* 128K */);
    one_length(1<<20 /* 1M */,    64);
}
