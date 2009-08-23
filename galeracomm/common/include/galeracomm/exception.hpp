#ifndef EXCEPTION_HPP
#define EXCEPTION_HPP

#include <string>
#include <iostream>
#include <galerautils.hpp>

/*!
 * Macro to throw exceptions with debug information
 */
#define DException(_msg_)                                       \
    gu::Exception(_msg_, 255, __FILE__, __FUNCTION__, __LINE__)

/*!
 * Type of exception which is recoverable.
 */
class RuntimeException : public gu::Exception {
public:
    RuntimeException(const char *msg, int err = 0) :
        gu::Exception(msg, err) {}
};

/*!
 * Type of exception which is unrecoverable.
 */
class FatalException : public gu::Exception {
public:
    FatalException(const char *msg, int err = 0) :
        gu::Exception(msg, err) {}
}; 

#endif // EXCEPTION_HPP
