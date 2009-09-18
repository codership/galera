#ifndef EXCEPTION_HPP
#define EXCEPTION_HPP

#include <cerrno>
#include <string>
#include <iostream>
#include <galerautils.hpp>

/*!
 * Macro to throw exceptions with debug information
 */
#define DException(_msg_)                                               \
    gu::Exception(_msg_, errno)

/*!
 * Type of exception which is recoverable.
 */
class RuntimeException : public gu::Exception {
public:
    RuntimeException(const char *msg, int err = 0) :
        gu::Exception(msg, err) {}
    RuntimeException(const char *msg, int err, const char* file, int line) :
        gu::Exception(msg, err) {}
};

#define DRuntimeException(_msg_)                        \
    RuntimeException(_msg_, errno)

/*!
 * Type of exception which is unrecoverable.
 */
class FatalException : public gu::Exception {
public:
    FatalException(const char *msg, int err = 0) :
        gu::Exception(msg, err) {}
    FatalException(const char *msg, int err, const char* file, int line) :
        gu::Exception(msg, err) {}
}; 

#define DFatalException(_msg_)                          \
    FatalException(_msg_, errno)

#endif // EXCEPTION_HPP
