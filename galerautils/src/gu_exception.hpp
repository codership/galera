/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#ifndef __GU_EXCEPTION__
#define __GU_EXCEPTION__

#include <string>
#include <exception>

namespace gu
{
    class Exception: public std::exception
    {
    private:

        std::string msg;
        const int   err;

    public:

        enum { E_UNSPEC = 255 }; // unspecified error

        Exception (const std::string& msg_, int err_ = E_UNSPEC) throw()
            : msg (msg_),
              err (err_)
        {}

        virtual ~Exception () throw() {}

        const char* what      () const throw() { return msg.c_str(); }

        int         get_errno () const throw() { return err; }

        void        trace (const char* file, const char* func, int line);
    };
}

#ifndef NDEBUG /* enabled together with assert() */

#define gu_trace(expr_)                                                 \
    try { expr_; } catch (gu::Exception& e)                             \
    { e.trace(__FILE__, __FUNCTION__, __LINE__); throw; }

#else

#define gu_trace(expr_) expr_

#endif // NDEBUG

#endif // __GU_EXCEPTION__
