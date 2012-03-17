/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#ifndef __GCACHE_FILEDES__
#define __GCACHE_FILEDES__

#include <galerautils.hpp>

#include <string>

namespace gcache
{
    class FileDescriptor
    {
    public:

        /* open existing file */
        FileDescriptor (const std::string& fname,
                        bool               sync  = true)
            throw (gu::Exception);

        /* (re)create file */
        FileDescriptor (const std::string& fname,
                        size_t             length,
                        bool               allocate = true,
                        bool               sync     = true)
            throw (gu::Exception);

        virtual ~FileDescriptor ();

        int                get()      const throw() { return value; };
        const std::string& get_name() const throw() { return name;  };
        off_t              get_size() const throw() { return size;  };

        void               flush()    const throw (gu::Exception);

    private:

        const int         value;
        const std::string name;
        const off_t       size;
        const bool        sync; // sync on close

        bool write_byte (off_t offset)    throw (gu::Exception);
        void write_file (off_t start = 0) throw (gu::Exception);
        void prealloc   (off_t start = 0) throw (gu::Exception); 

        void constructor_common() throw (gu::Exception);

        FileDescriptor (const FileDescriptor&);
        FileDescriptor& operator = (const FileDescriptor);
    };
}

#endif /* __GCACHE_FILEDES__ */
