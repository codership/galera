/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#ifndef __GU_EXCEPTION__
#define __GU_EXCEPTION__

#include <string>
#include <exception>
#include "gu_errno.h"

namespace gu {

    /*! Some utility exceptions to indicate special conditions. */
    class NotSet   {};
    class NotFound {};

    class Exception: public std::exception
    {
    public:

        Exception (const std::string& msg_, int err_)
            : msg (msg_),
              err (err_)
        {}

        virtual ~Exception    () throw() {}

        const char* what      () const throw() { return msg.c_str(); }

        int         get_errno () const { return err; }

        void        trace (const char* file, const char* func, int line);

    private:

        std::string msg;
        const int   err;
    };
}

/* to mark a place where exception was caught */
#define GU_TRACE(_exception_) _exception_.trace(__FILE__, __FUNCTION__, __LINE__)

#ifndef NDEBUG /* enabled together with assert() */

#define gu_trace(_expr_)                                                 \
    try { _expr_; } catch (gu::Exception& e) { GU_TRACE(e); throw; }

#else

#define gu_trace(_expr_) _expr_

#endif // NDEBUG

#endif // __GU_EXCEPTION__
