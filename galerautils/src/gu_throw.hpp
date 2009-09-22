/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * @file Classes to allow throwing more verbose exceptions. Should be only
 * used from one-line macros below. Concrete classes intended to be final.
 */

#ifndef __GU_THROW__
#define __GU_THROW__

#include <cerrno>
#include <sstream>

#include "gu_exception.hpp"

namespace gu
{
    /*! Abstract base class */
    class ThrowBase
    {
        ThrowBase (const ThrowBase&);
        ThrowBase& operator= (const ThrowBase&);

    protected:

        const char* const  file;
        const char* const  func;
        int         const  line;
        std::ostringstream os;

        ThrowBase (const char* file_, const char* func_, int line_) throw()
            :
            file (file_),
            func (func_),
            line (line_),
            os   ()
        {}
        
    public:

        virtual ~ThrowBase () {}

        std::ostringstream& msg () throw() { return os; }
    };

    /* final*/ class ThrowError : public ThrowBase
    {
        int const err;

    public:

        ThrowError (const char* file_,
                    const char* func_,
                    int         line_,
                    int         err_ = Exception::E_UNSPEC) throw()
            :
            ThrowBase (file_, func_, line_),
            err       (err_)
        {}

        ~ThrowError () throw (Exception)
        {
            os << ": " << err << " (" << ::strerror(err) << ')';

            Exception e(os.str(), err);

            e.trace (file, func, line);

            throw e;
        }
    };

    /* final*/ class ThrowFatal : public ThrowBase
    {
    public:

        ThrowFatal (const char* file, const char* func, int line) throw()
            :
            ThrowBase (file, func, line)
        {}
        
        ~ThrowFatal () throw (Exception)
        {
            os << " (FATAL)";

            Exception e(os.str(), ENOTRECOVERABLE);

            e.trace (file, func, line);

            throw e;
        }
    };
}

// Usage: gu_throw_xxxxx << msg1 << msg2 << msg3;

#define gu_throw_error(err_)                                    \
    gu::ThrowError(__FILE__, __FUNCTION__, __LINE__, err_).msg()

#define gu_throw_fatal                                          \
    gu::ThrowFatal(__FILE__, __FUNCTION__, __LINE__).msg()

#endif // __GU_THROW__
