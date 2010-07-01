/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#ifndef __GU_EXCEPTION__
#define __GU_EXCEPTION__

#include <string>
#include <exception>
#include <cerrno>

namespace gu {

    /*! Some utility exceptions to indicate special conditions. */
    class NotSet   {};
    class NotFound {};

    class Exception: public std::exception
    {
    public:

        enum
	{
	    E_UNSPEC = 255,                     // unspecified error
#ifdef ENOTRECOVERABLE
	    E_NOTRECOVERABLE = ENOTRECOVERABLE  // not recoverable
#else
	    E_NOTRECOVERABLE = E_UNSPEC         // not recoverable
#endif
	};

        Exception (const std::string& msg_, int err_ = E_UNSPEC) throw()
            : msg (msg_),
              err (err_)
        {}

        virtual ~Exception () throw() {}

        const char* what      () const throw() { return msg.c_str(); }

        int         get_errno () const throw() { return err; }

        void        trace (const char* file, const char* func, int line);

    private:

        std::string msg;
        const int   err;
    };
}

#ifndef NDEBUG /* enabled together with assert() */

#define gu_trace(_expr_)                                                 \
    try { _expr_; } catch (gu::Exception& e)                             \
    { e.trace(__FILE__, __FUNCTION__, __LINE__); throw; }

#else

#define gu_trace(_expr_) _expr_

#endif // NDEBUG

#endif // __GU_EXCEPTION__
