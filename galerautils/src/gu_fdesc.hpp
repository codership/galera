/*
 * Copyright (C) 2009-2016 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef __GU_FDESC_HPP__
#define __GU_FDESC_HPP__

#include "gu_exception.hpp"
#include "gu_types.hpp" // for off_t, byte_t

#include <string>

namespace gu
{

class FileDescriptor
{
public:

    /* open existing file */
    FileDescriptor (const std::string& fname,
                    bool               sync  = true);

    /* (re)create file */
    FileDescriptor (const std::string& fname,
                    size_t             length,
                    bool               allocate = true,
                    bool               sync     = true);

    ~FileDescriptor ();

    int                get()   const { return fd_;   }
    const std::string& name()  const { return name_; }
    off_t              size()  const { return size_; }

    void               sync()  const;

    void               unlink() const { ::unlink (name_.c_str()); }

private:

    std::string const name_;
    int         const fd_;
    off_t       const size_;
    bool        const sync_; // sync on close

    bool write_byte (off_t offset);
    void write_file (off_t start = 0);
    void prealloc   (off_t start = 0);

    void constructor_common();

    FileDescriptor (const FileDescriptor&);
    FileDescriptor& operator = (const FileDescriptor);
};

} /* namespace gu */

#endif /* __GU_FDESC_HPP__ */
