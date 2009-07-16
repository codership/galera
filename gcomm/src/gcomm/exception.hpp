#ifndef _GCOMM_EXCEPTION_HPP_
#define _GCOMM_EXCEPTION_HPP_

#include <gcomm/common.hpp>

#include <exception>

using std::exception;

BEGIN_GCOMM_NAMESPACE

class Exception : exception
{
    const char* msg;

    void operator=(const Exception&);
protected:
    Exception(const Exception& e) :
        exception(),
        msg()
    {
        msg = e.msg;
    }
public:
    Exception() throw() : msg("")
    {
    }
    
    Exception(const char *msg_) throw() : 
        msg(msg_)
    {
    }
    
    const char* what() const throw()
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

#endif // _GCOMM_EXCEPTION_HPP_
