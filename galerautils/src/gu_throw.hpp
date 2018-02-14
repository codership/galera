/*
 * Copyright (C) 2009-2017 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * @file Classes to allow throwing more verbose exceptions. Should be only
 * used from one-line macros below. Concrete classes intended to be final.
 */

#ifndef __GU_THROW__
#define __GU_THROW__

#include <cstring>
#include <cerrno>
#include <cstring>
#include <sstream>

#include "gu_macros.hpp"
#include "gu_exception.hpp"

namespace gu
{
    /*! "base" class */
    class ThrowBase
    {
    protected:

        const char* const  file;
        const char* const  func;
        int         const  line;
        std::ostringstream os;

        ThrowBase (const char* file_, const char* func_, int line_)
            :
            file (file_),
            func (func_),
            line (line_),
            os   ()
        {}

    private:

        ThrowBase (const ThrowBase&);
        ThrowBase& operator= (const ThrowBase&);

        friend class ThrowError;
        friend class ThrowFatal;
    };

    /* final */ class ThrowError
    {
    public:

        ThrowError (const char* file_,
                    const char* func_,
                    int         line_,
                    int         err_)
            :
            base (file_, func_, line_),
            err  (err_)
        {}

        ~ThrowError() GU_NOEXCEPT(false) GU_NORETURN
        {
            base.os << ": " << err << " (" << ::strerror(err) << ')';

            Exception e(base.os.str(), err);

            e.trace (base.file, base.func, base.line);
            // cppcheck-suppress exceptThrowInDestructor
            throw e;
        }

        std::ostringstream& msg () { return base.os; }

    private:

        ThrowBase base;
        int const err;
    };

    /* final */ class ThrowFatal
    {
    public:

        ThrowFatal (const char* file, const char* func, int line)
            :
            base (file, func, line)
        {}

        ~ThrowFatal () GU_NOEXCEPT(false) GU_NORETURN
        {
            base.os << " (FATAL)";

            Exception e(base.os.str(), ENOTRECOVERABLE);

            e.trace (base.file, base.func, base.line);
            // cppcheck-suppress exceptThrowInDestructor
            throw e;
        }

        std::ostringstream& msg () { return base.os; }

    private:

        ThrowBase base;
    };
}

// Usage: gu_throw_xxxxx << msg1 << msg2 << msg3;

#define gu_throw_error(err_)                                    \
    gu::ThrowError(__FILE__, __FUNCTION__, __LINE__, err_).msg()

#define gu_throw_fatal                                          \
    gu::ThrowFatal(__FILE__, __FUNCTION__, __LINE__).msg()

#endif // __GU_THROW__
