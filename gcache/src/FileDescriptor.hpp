/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#ifndef __GCACHE_FILEDES__
#define __GCACHE_FILEDES__

#include <fcntl.h> // for flags and mode macros

namespace gcache
{
    class FileDescriptor
    {

    private:

        const int         value;
        const std::string name;

    public:

        FileDescriptor (std::string& fname, int flags, mode_t mode);

        virtual ~FileDescriptor ();

        int get() { return value; };

    private:

        // This class is definitely non-copyable
        FileDescriptor (const FileDescriptor&);
        FileDescriptor& operator = (const FileDescriptor);
    };
}

#endif /* __GCACHE_FILEDES__ */
