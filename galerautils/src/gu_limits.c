// Copyright (C) 2013-2016 Codership Oy <info@codership.com>

/**
 * @file system limit macros
 *
 * $Id:$
 */

#include "gu_limits.h"
#include "gu_log.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#if defined(__APPLE__)

#include <sys/sysctl.h> // doesn't seem to be used directly, but jst in case
#include <mach/mach.h>

static long darwin_phys_pages (void)
{
    /* Note: singleton pattern would be useful here */
    vm_statistics64_data_t vm_stat;
    unsigned int count = HOST_VM_INFO64_COUNT;
    kern_return_t ret = host_statistics64 (mach_host_self (), HOST_VM_INFO64,
                                           (host_info64_t) &vm_stat, &count);
    if (ret != KERN_SUCCESS)
    {
        gu_error ("host_statistics64 failed with code %d", ret);
        return 0;
    }
    /* This gives a value a little less than physical memory of computer */
    return vm_stat.free_count + vm_stat.active_count + vm_stat.inactive_count
         + vm_stat.wire_count;
    /* Exact value may be obtain via sysctl ({CTL_HW, HW_MEMSIZE}) */
    /* Note: sysctl is 60% slower compared to host_statistics64 */
}

static long darwin_avphys_pages (void)
{
    vm_statistics64_data_t vm_stat;
    unsigned int count = HOST_VM_INFO64_COUNT;
    kern_return_t ret = host_statistics64 (mach_host_self (), HOST_VM_INFO64,
                                           (host_info64_t) &vm_stat, &count);
    if (ret != KERN_SUCCESS)
    {
        gu_error ("host_statistics64 failed with code %d", ret);
        return 0;
    }
    /* Note:
     * vm_stat.free_count == vm_page_free_count + vm_page_speculative_count */
    return vm_stat.free_count - vm_stat.speculative_count;
}

static inline size_t page_size()    { return getpagesize();          }
static inline size_t phys_pages()   { return darwin_phys_pages();    }
static inline size_t avphys_pages() { return darwin_avphys_pages();  }

#elif defined(__FreeBSD__)

#include <vm/vm_param.h> // VM_TOTAL
#include <sys/vmmeter.h> // struct vmtotal
#include <sys/sysctl.h>

static long freebsd_avphys_pages (void)
{
    /* TODO: 1) sysctlnametomib may be called once */
    /*       2) vm.stats.vm.v_cache_count is potentially free memory too */
    int mib_vm_stats_vm_v_free_count[4];
    size_t mib_sz = 4;
    int rc = sysctlnametomib ("vm.stats.vm.v_free_count",
                              mib_vm_stats_vm_v_free_count, &mib_sz);
    if (rc != 0)
    {
        gu_error ("sysctlnametomib(vm.stats.vm.v_free_count) failed, code %d",
                  rc);
        return 0;
    }

    unsigned int vm_stats_vm_v_free_count;
    size_t sz = sizeof (vm_stats_vm_v_free_count);
    rc = sysctl (mib_vm_stats_vm_v_free_count, mib_sz,
                 &vm_stats_vm_v_free_count, &sz, NULL, 0);
    if (rc != 0)
    {
        gu_error ("sysctl(vm.stats.vm.v_free_count) failed with code %d", rc);
        return 0;
    }
    return vm_stats_vm_v_free_count;
}

static inline size_t page_size()    { return sysconf(_SC_PAGESIZE);     }
static inline size_t phys_pages()   { return sysconf(_SC_PHYS_PAGES);   }
static inline size_t avphys_pages() { return freebsd_avphys_pages();    }

#elif defined(__NetBSD__)
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/vmmeter.h>

static long netbsd_avphys_pages (void)
{
    struct vmtotal total;
    static const int vmmeter_mib[] = { CTL_VM, VM_METER };
    size_t size = sizeof(total);
    if (sysctl(vmmeter_mib, 2, &total, &size, NULL, 0) == -1) {
        gu_error("Can't get vmtotals");
	return 0;
    }
    return (long)total.t_free;
}

static inline size_t page_size()    { return sysconf(_SC_PAGESIZE);     }
static inline size_t phys_pages()   { return sysconf(_SC_PHYS_PAGES);   }
static inline size_t avphys_pages() { return netbsd_avphys_pages();    }
#else /* !__APPLE__ && !__FreeBSD__ && !__NetBSD__ */

static inline size_t page_size()    { return sysconf(_SC_PAGESIZE);     }
static inline size_t phys_pages()   { return sysconf(_SC_PHYS_PAGES);   }
static inline size_t avphys_pages() { return sysconf(_SC_AVPHYS_PAGES); }

#endif /* !__APPLE__ && !__FreeBSD__ && !__NetBSD__ */

#define GU_DEFINE_FUNCTION(func)                \
    size_t gu_##func()                          \
    {                                           \
        static size_t ret = 0;                  \
        if (0 == ret) ret = func();             \
        return ret;                             \
    }

GU_DEFINE_FUNCTION(page_size)
GU_DEFINE_FUNCTION(phys_pages)
GU_DEFINE_FUNCTION(avphys_pages)
