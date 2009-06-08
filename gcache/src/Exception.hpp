/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#ifndef __GCACHE_EXCEPTION__
#define __GCACHE_EXCEPTION__

#include <exception>

namespace gcache
{
    class Exception: public std::exception
    {

    private:

#define EXCEPTION_MSG_SIZE 256
        char msg[EXCEPTION_MSG_SIZE];
        const int _errno;

    public:

        Exception (const char* msg_str, int) throw();
        Exception (const char* msg_str) throw();
        virtual ~Exception () throw() {};

        virtual const char* what () const throw() { return msg; };
        int get_errno () const throw() { return _errno; };
    };
}

#endif // __GCACHE_EXCEPTION__
