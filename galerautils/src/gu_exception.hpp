/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#ifndef __GU_EXCEPTION__
#define __GU_EXCEPTION__

#include <exception>

namespace gu
{
    class Exception: public std::exception
    {

    private:

#define GU_EXCEPTION_MSG_SIZE 256
        char msg[GU_EXCEPTION_MSG_SIZE];
        const int _errno;

    public:

        Exception (const char* msg, int err = 0) throw();
        virtual ~Exception () throw() {};

        virtual const char* what () const throw() { return msg; };
        int get_errno () const throw() { return _errno; };
    };
}

#endif // __GU_EXCEPTION__
