/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#ifndef __GCACHE_FILEDES__
#define __GCACHE_FILEDES__

namespace gcache
{
    class FileDescriptor
    {

    private:

        const int         value;
        const std::string name;

    public:

        FileDescriptor (const std::string& fname, int flags, mode_t mode);

        FileDescriptor (const std::string& fname, bool create);

        virtual ~FileDescriptor ();

        int get() const throw() { return value; };
        const std::string& get_name() const throw() { return name; };

    private:

        // This class is definitely non-copyable
        FileDescriptor (const FileDescriptor&);
        FileDescriptor& operator = (const FileDescriptor);

        void constructor_common();
    };
}

#endif /* __GCACHE_FILEDES__ */
