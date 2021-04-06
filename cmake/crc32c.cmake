#
# Copyright (C) 2020-2021 Codership Oy <info@codership.com>
#

message(STATUS "Checking for hardware CRC support for ${CMAKE_SYSTEM_PROCESSOR}")

if (${CMAKE_SYSTEM_PROCESSOR} STREQUAL x86_64 OR ${CMAKE_SYSTEM_PROCESSOR} STREQUAL amd64)
  #
  # CPU feature detection requires __get_cpuid().
  #
  check_c_source_compiles(
    "
#include <cpuid.h>
unsigned int eax, ebx, ecx, edx;
int main() { __get_cpuid(1, &eax, &ebx, &ecx, &edx); return 0; }
"
    GALERA_HAVE_CPUID)
  if (NOT GALERA_HAVE_CPUID)
    message(STATUS
      "Compiler does not support __get_cpuid(), hardware CRC not enabled")
    return()
  endif()
  check_c_compiler_flag(-msse4.2 HAVE_CRC_FLAG)
  if (HAVE_CRC_FLAG)
    set(GALERA_CRC32C_COMPILER_FLAG "-msse4.2")
    set(GALERA_CRC32C_X86_64 ON)
  endif()
elseif (${CMAKE_SYSTEM_PROCESSOR} STREQUAL aarch64)
  set(CMAKE_REQUIRED_FLAGS -march=armv8-a+crc)
  check_c_source_compiles(
"
// Here we assume that CPU feature detection and
// CRC32 support in hardware is the same thing and
// if the former is not available, the latter is
// not available as well, so we test for both at
// the same time

#include <sys/auxv.h>
#include <arm_acle.h>

int main()
{
#if defined(__linux__)
    (void)getauxval(AT_HWCAP);
#elif defined(__FreeBSD__)
    unsigned long info;
    (void)elf_aux_info(AT_HWCAP, &info, sizeof(info));
#else
    #error Hardware feature detection for OS not supported
#endif

    (void)__crc32b(0, 0);
    (void)__crc32h(0, 0);
    (void)__crc32w(0, 0);
    (void)__crc32d(0, 0);

    return 0;
}
" HAVE_CRC_FLAG)
  unset(CMAKE_REQUIRED_FLAGS)
  if (HAVE_CRC_FLAG)
    set(GALERA_CRC32C_COMPILER_FLAG "-march=armv8-a+crc")
    set(GALERA_CRC32C_ARM64 ON)
  endif()
endif()

if (NOT HAVE_CRC_FLAG)
  message(STATUS "No hardware CRC support")
  add_definitions(-DGU_CRC32C_NO_HARDWARE)
else()
  message(STATUS
    "Hardwared CRC support enabled: ${GALERA_CRC32C_COMPILER_FLAG}")
endif()
