#ifndef _GCOMM_EXCEPTION_HPP_
#define _GCOMM_EXCEPTION_HPP_

#include <cerrno>
#include <galerautils.hpp>

#include "gcomm/common.hpp"

using gu::Exception;

BEGIN_GCOMM_NAMESPACE

/*!
 * Type of exception which is recoverable.
 */
class RuntimeException : public gu::Exception
{
public:

    RuntimeException(const std::string& msg, int err) :
        gu::Exception(msg, err)
    {}
};

// deprecated
#define DRuntimeException(_msg_)                \
    gcomm::RuntimeException(_msg_, errno)

/* final*/ class ThrowRuntime : public gu::ThrowBase
{
    int const err;

public:

    ThrowRuntime(const char* file, const char* func, int line, int err_) throw()
        :
        ThrowBase (file, func, line),
        err       (err_)
    {}

    ~ThrowRuntime() throw(RuntimeException)
    {
        os << ": " << err << " (" << ::strerror(err) << ')';

        RuntimeException e(os.str(), err);

        e.trace (file, func, line);

        throw e;
    }
};

/*!
 * Type of exception which is unrecoverable.
 */
class FatalException : public gu::Exception
{
public:

    FatalException(const std::string& msg) :
        gu::Exception(msg, ENOTRECOVERABLE)
    {}
}; 

//deprecated
#define DFatalException(_msg_)                  \
    gcomm::FatalException(_msg_)

/* final*/ class ThrowFatal : public gu::ThrowBase
{
public:
    ThrowFatal (const char* file, const char* func, int line) throw()
        :
        gu::ThrowBase (file, func, line)
    {}

    ~ThrowFatal () throw (FatalException)
    {
        os << " (FATAL)";
        //  assert(0);

        FatalException e(os.str());

        e.trace (file, func, line);

        throw e;
    }
};

/*!
 * Exception caused by interrupt.
 */
class InterruptedException : public gu::Exception
{
public:

    InterruptedException(const std::string& msg, int err = EINTR) :
        gu::Exception(msg, EINTR)
    {}
}; 

END_GCOMM_NAMESPACE

#define gcomm_throw_runtime(err_)                                       \
    gcomm::ThrowRuntime (__FILE__,__FUNCTION__,__LINE__,err_).msg()

#define gcomm_throw_fatal                                       \
    gcomm::ThrowFatal (__FILE__,__FUNCTION__,__LINE__).msg()

#define gcomm_assert(cond_)                    \
    if (not (cond_)) gcomm_throw_fatal << #cond_ << ": "

#endif // _GCOMM_EXCEPTION_HPP_
