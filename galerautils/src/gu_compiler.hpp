/*
 * Copyright (C) 2020 Codership Oy <info@codership.com>
 */

/** @file gu_compiler.hpp
 *
 * Compiler specific workarounds.
 *
 */
#ifndef GU_COMPILER_HPP
#define GU_COMPILER_HPP

#if (__GNUC__ == 4 && __GNUC_MINOR__ == 4)
#define GALERA_OVERRIDE
#else
#define GALERA_OVERRIDE override
#endif /* #if (__GNUC__ == 4 && __GNUC_MINOR__ == 4) */

#endif /* GU_COMPILER_HPP */
