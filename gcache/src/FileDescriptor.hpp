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
        const size_t      size;

    public:

        FileDescriptor (const std::string& fname);
        FileDescriptor (const std::string& fname, size_t length);

        virtual ~FileDescriptor ();

        int                get()      const throw() { return value; };
        const std::string& get_name() const throw() { return name;  };
        size_t             get_size() const throw() { return size;  };
        void               sync()     const;

    private:

        // This class is definitely non-copyable
        FileDescriptor (const FileDescriptor&);
        FileDescriptor& operator = (const FileDescriptor);

        void constructor_common();
        void prealloc(); 
    };
}

#endif /* __GCACHE_FILEDES__ */
