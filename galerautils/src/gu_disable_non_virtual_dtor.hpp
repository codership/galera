//
// Copyright (C) 2020 Codership Oy <info@codership.com>
//

// Note that there are no usual header guards because this header
// may have to be included several times for compilation unit.

/**
 * @file gu_disable_non_virtual_dtor.hpp
 *
 * This file accompanied with gu_enable_non_virtual_dtor.hpp
 * can be used to disable/enable -Wnon-virtual-dtor compiler warning
 * temporarily when it is not desirable to disable the warning completely
 * for compilation.
 *
 * This can be useful when using public inheritance from standard
 * library classes, especially std::enable_shared_from_this.
 */

#if defined(__GNUG__)
# if (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || (__GNUC__ > 4)
#  pragma GCC diagnostic push
# endif // (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || (__GNUC__ > 4)
# pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif
