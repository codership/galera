/*
 * Copyright (C) 2010-2017 Codership Oy <info@codership.com>
 */

/*! @file memory operations interface */

#ifndef _gcache_memops_hpp_
#define _gcache_memops_hpp_

#include <gu_arch.h>
#include <gu_macros.h>
#include <gu_limits.h> // GU_MIN_ALIGNMENT
#include <stdint.h>

namespace gcache
{
    struct BufferHeader;

    class MemOps
    {
    public:
        /* although size value passed to GCache should be representable by
         * a signed integer type, internally the buffer allocated will also
         * incur header overhead, so it has to be represented by unsigned int.
         * However the difference between two internal sizes should never exceed
         * signed representation. */
        typedef          int ssize_type; // size passed to GCache
        typedef unsigned int size_type;  // internal size representation
        typedef ssize_type   diff_type;  // difference between two size_types

        MemOps() {}
        virtual ~MemOps() {}

        virtual void*
        malloc  (size_type size)          = 0;

        virtual void*
        realloc (void* ptr, size_type size) = 0;

        virtual void
        free    (BufferHeader* bh)        = 0;

        virtual void
        repossess(BufferHeader* bh)       = 0; /* "unfree" */

        virtual void
        discard (BufferHeader* bh)        = 0;

        virtual void
        reset   ()                        = 0;

        /* GCache 3.x is not supposed to be portable between platforms */
        static size_type const ALIGNMENT  = GU_MIN_ALIGNMENT;

        static inline size_type align_size(size_type s)
        {
            return align<size_type>(s);
        }

        static inline uint8_t* align_ptr(uint8_t* p)
        {
            return reinterpret_cast<uint8_t*>(align<uintptr_t>(uintptr_t(p)));
        }

    private:

        template <typename T>
        static inline T align(T s) { return GU_ALIGN(s, ALIGNMENT); }
    };
}

#endif /* _gcache_memops_hpp_ */
