// Copyright (C) 2013 Codership Oy <info@codership.com>

/**
 * @file time manipulation functions/macros
 *
 * $Id: $
 */

#if defined(__APPLE__)

#include <errno.h>
#include <time.h> // struct timespec
#include <sys/time.h> // gettimeofday
#include <mach/clock.h> // clock_get_time
#include <mach/mach.h> // host_get_clock_service
#include <mach/mach_time.h> // mach_absolute_time, mach_timebase_info
#include <libkern/OSAtomic.h>
#include "gu_time.h"

#define NSEC_PER_SEC 1000000000
#define NSEC_PER_USEC      1000

# if defined(__LP64__)

// OS X comm page time offsets
// see http://www.opensource.apple.com/source/xnu/xnu-2050.22.13/osfmk/i386/cpu_capabilities.h
#define nt_tsc_base     "0x50"
#define nt_scale        "0x58"
#define nt_shift        "0x5c"
#define nt_ns_base      "0x60"
#define nt_generation   "0x68"
#define gtod_generation "0x6c"
#define gtod_ns_base    "0x70"
#define gtod_sec_base   "0x78"

static inline int64_t nanotime (void) {
    int64_t ntime;
    __asm volatile (
                "mov    $0x7fffffe00000, %%rbx;" /* comm page base */
        "0:"    /* Loop trying to take a consistent snapshot of the time parameters. */
                "movl   "gtod_generation"(%%rbx), %%r8d;"
                "testl  %%r8d, %%r8d;"
                "jz     1f;"
                "movl   "nt_generation"(%%rbx), %%r9d;"
                "testl  %%r9d, %%r9d;"
                "jz     0b;"
                "rdtsc;"
                "movq   "nt_tsc_base"(%%rbx), %%r10;"
                "movl   "nt_scale"(%%rbx), %%r11d;"
                "movq   "nt_ns_base"(%%rbx), %%r12;"
                "cmpl   "nt_generation"(%%rbx), %%r9d;"
                "jne    0b;"
                "movq   "gtod_ns_base"(%%rbx), %%r13;"
                "movq   "gtod_sec_base"(%%rbx), %%r14;"
                "cmpl   "gtod_generation"(%%rbx), %%r8d;"
                "jne    0b;"

                /* Gathered all the data we need. Compute time. */
                /* ((tsc - nt_tsc_base) * nt_scale) >> 32 + nt_ns_base - gtod_ns_base + gtod_sec_base*1e9 */
                /* The multiply and shift extracts the top 64 bits of the 96-bit product. */
                "shlq   $32, %%rdx;"
                "addq   %%rdx, %%rax;"
                "subq   %%r10, %%rax;"
                "mulq   %%r11;"
                "shrdq  $32, %%rdx, %%rax;"
                "addq   %%r12, %%rax;"
                "subq   %%r13, %%rax;"
                "imulq  $1000000000, %%r14;"
                "addq   %%r14, %%rax;"
                "jmp    2f;"
        "1:"    /* Fall back to system call (usually first call in this thread). */
                "movq   %%rsp, %%rdi;" /* rdi must be non-nil, unused */
                "movq   $0, %%rsi;"
                "movl   $(0x2000000+116), %%eax;" /* SYS_gettimeofday */
                "syscall; /* may destroy rcx and r11 */"
                /* sec is in rax, usec in rdx */
                /* return nsec in rax */
                "imulq  $1000000000, %%rax;"
                "imulq  $1000, %%rdx;"
                "addq   %%rdx, %%rax;"
        "2:"
                : "=a"(ntime)
                : /* no input parameters */
                : "%rbx", "%rcx", "%rdx", "%rsi", "%rdi", "%r8", "%r9", "%r10", "%r11", "%r12", "%r13", "%r14"
    );
    return ntime;
}

static inline int64_t nanouptime (void) {
    int64_t ntime;
    __asm volatile (
                "movabs $0x7fffffe00000, %%rbx;" /* comm page base */
        "0:"    /* Loop trying to take a consistent snapshot of the time parameters. */
                "movl   "nt_generation"(%%rbx), %%r9d;"
                "testl  %%r9d, %%r9d;"
                "jz     0b;"
                "rdtsc;"
                "movq   "nt_tsc_base"(%%rbx), %%r10;"
                "movl   "nt_scale"(%%rbx), %%r11d;"
                "movq   "nt_ns_base"(%%rbx), %%r12;"
                "cmpl   "nt_generation"(%%rbx), %%r9d;"
                "jne    0b;"

                /* Gathered all the data we need. Compute time. */
                /* ((tsc - nt_tsc_base) * nt_scale) >> 32 + nt_ns_base */
                /* The multiply and shift extracts the top 64 bits of the 96-bit product. */
                "shlq   $32, %%rdx;"
                "addq   %%rdx, %%rax;"
                "subq   %%r10, %%rax;"
                "mulq   %%r11;"
                "shrdq  $32, %%rdx, %%rax;"
                "addq   %%r12, %%rax;"
                : "=a"(ntime)
                : /* no input parameters */
                : "%rbx", "%rcx", "%rdx", "%rsi", "%rdi", "%r9", "%r10", "%r11", "%r12"
    );
    return ntime;
}

int
clock_gettime (clockid_t clk_id, struct timespec * tp)
{
    int64_t abstime = 0;
    if (tp == NULL) {
        return EFAULT;
    }
    switch (clk_id) {
        case CLOCK_REALTIME:
            abstime = nanotime ();
            break;
        case CLOCK_MONOTONIC:
            abstime = nanouptime ();
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    tp->tv_sec = abstime / (uint64_t)NSEC_PER_SEC;
    tp->tv_nsec = (uint32_t)(abstime % (uint64_t)NSEC_PER_SEC);
    return 0;
}

#else /* !__LP64__ */

static struct mach_timebase_info g_mti;

int
clock_gettime (clockid_t clk_id, struct timespec * tp)
{
    int64_t abstime = 0;
    mach_timebase_info_data_t mti; /* {uint32_t numer, uint32_t denom} */
    if (tp == NULL) {
        return EFAULT;
    }
    switch (clk_id) {
        case CLOCK_REALTIME:
            struct timeval tv;
            if (gettimeofday (&tv, NULL) != 0) {
                return -1;
            }
            tp->tv_sec = tv.tv_sec;
            tp->tv_nsec = tv.tv_usec * NSEC_PER_USEC;
            return 0;
        case CLOCK_MONOTONIC:
            abstime = mach_absolute_time ();
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    if (g_mti.denom == 0) {
        struct mach_timebase_info mti;
        mach_timebase_info (&mti);
        g_mti.numer = mti.numer;
        OSMemoryBarrier ();
        g_mti.denom = mti.denom;
    }
    nanos = (uint64_t)(abstime * (((double)g_mti.numer) / ((double)g_mti.denom)));
    tp->tv_sec = nanos / (uint64_t)NSEC_PER_SEC;
    tp->tv_nsec = (uint32_t)(nanos % (uint64_t)NSEC_PER_SEC);
    return 0;
}

#endif /* !__LP64__ */

#else /* !__APPLE__ */

#ifdef __GNUC__
// error: ISO C forbids an empty translation unit
int dummy_var_gu_time;
#endif

#endif /* __APPLE__ */

