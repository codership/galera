/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#include <cerrno>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "Exception.hpp"
#include "Logger.hpp"
#include "FileDescriptor.hpp"

namespace gcache
{
    FileDescriptor::FileDescriptor (std::string& fname, int flags, mode_t mode)
        : value(open (fname.c_str(), flags, mode)), name(fname)
    {
        if (value < 0) {
            int err = errno;
            std::string msg = "Failed to open cache file '" + name +
                "': " + strerror (err);
            throw Exception (msg.c_str(), err);
        }

        log_info << "Opened file '" << name << "'";
        log_debug << "File descriptor: " << value;
    }

    FileDescriptor::~FileDescriptor ()
    {
        int err = 0;
        std::string msg;

        log_info << "Syncing file '" << name << "'";

        if (fsync(value) == 0) {
            if (close(value) == 0) {
                log_info << "Closed  file '" << name << "'";
                return;
            }
            else {
                err = errno;
                msg = "Failed to close file '" + name + "': " + strerror(err);
            }
        }
        else {
            err = errno;
            msg = "Failed to flush file '" + name + "': " + strerror(err);
        }

        throw Exception (msg.c_str(), err);
    }
}
