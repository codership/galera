// Copyright (C) 2013 Codership Oy <info@codership.com>

/**
 * @file system limit macros
 *
 * $Id:$
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include "gu_limits.h"
#include "gu_log.h"

#if defined(__APPLE__)

#include <sys/sysctl.h> // doesn't seem to be used directly, but jst in case
#include <mach/mach.h>

long gu_darwin_phys_pages (void)
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

long gu_darwin_avphys_pages (void)
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

#elif defined(__FreeBSD__)

#include <vm/vm_param.h> // VM_TOTAL
#include <sys/vmmeter.h> // struct vmtotal
#include <sys/sysctl.h>

long gu_freebsd_avphys_pages (void)
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

#endif /* __FreeBSD__ */
