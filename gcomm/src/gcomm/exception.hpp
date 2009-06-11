#ifndef EXCEPTION_HPP
#define EXCEPTION_HPP

#include <gcomm/common.hpp>

#include <exception>

using std::exception;

BEGIN_GCOMM_NAMESPACE

class Exception : exception
{
    const char* msg;
public:
    Exception() throw() : msg("")
    {
    }
    
    Exception(const char *msg_) throw() : 
        msg(msg_)
    {
    }
    
    const char* what()
    {
        return msg;
    }
};

/*!
 * Type of exception which is recoverable.
 */
struct RuntimeException : Exception
{
    RuntimeException(const char *msg) : 
        Exception(msg)
    {
    }
};

/*!
 * Type of exception which is unrecoverable.
 */
struct FatalException : Exception
{
    FatalException(const char *msg) : 
        Exception(msg)
    {
    }
}; 

struct InterruptedException : Exception
{
    InterruptedException() :
        Exception("interrupted")
    {
    }
};
    
END_GCOMM_NAMESPACE

#endif // EXCEPTION_HPP
