// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// This file adds defines about the platform we're currently building on.
//  Operating System:
//    OS_WIN / OS_MACOSX / OS_LINUX / OS_POSIX (MACOSX or LINUX)
//  Compiler:
//    COMPILER_MSVC / COMPILER_GCC
//  Processor:
//    ARCH_CPU_X86 / ARCH_CPU_X86_64 / ARCH_CPU_X86_FAMILY (X86 or X86_64)
//    ARCH_CPU_32_BITS / ARCH_CPU_64_BITS
#ifndef BUILD_BUILD_CONFIG_H_
#define BUILD_BUILD_CONFIG_H_
// A set of macros to use for platform detection.
#if defined(__native_client__)
#define OS_NACL 1
#elif defined(__ANDROID__)
#define OS_ANDROID 1
#endif

// Compiler detection.
#if defined(__GNUC__)
#define COMPILER_GCC 1
#elif defined(_MSC_VER)
#define COMPILER_MSVC 1
#else
#error Please add support for your compiler in build/build_config.h
#endif

// Processor architecture detection.  For more info on what's defined, see:
//   http://msdn.microsoft.com/en-us/library/b0084kay.aspx
//   http://www.agner.org/optimize/calling_conventions.pdf
//   or with gcc, run: "echo | gcc -E -dM -"
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
#define ARCH_CPU_X86_FAMILY 1
#endif

#endif  // BUILD_BUILD_CONFIG_H_
