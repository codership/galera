//
// Copyright (C) 2010-2016 Codership Oy <info@codership.com>
//

#ifndef GALERA_EXCEPTION_HPP
#define GALERA_EXCEPTION_HPP

#include <gu_exception.hpp>
#include <gu_logger.hpp>
#include "wsrep_api.h"

#include<cassert>

namespace galera
{
/*!
 * An exception to handle applier errors and avoid confusing wsrep error codes
 * with the standard ones
 */
class ApplyException : public gu::Exception
{
public:
    ApplyException (const std::string& msg, void* b1, const void* b2, size_t len)
        : gu::Exception(msg, -1), data_(b1), const_data_(b2),
          data_len_(len)
    {
        assert(NULL == b1 || NULL == b2);
    }

    ApplyException() : gu::Exception("", 0), data_(NULL), const_data_(NULL),
                       data_len_(0) {}

    ApplyException(const ApplyException& ae)
        : gu::Exception(ae), data_(ae.data_), const_data_(ae.const_data_),
          data_len_(ae.data_len_)
    {}

    ~ApplyException() throw() {}

    /* this is just int because we must handle any positive value */
    int    status()   const { return get_errno(); }
    const void* data() const { return const_data_ ? const_data_ : data_;       }
    size_t data_len() const { return data_len_;   }

    void   free()           { ::free(data_); data_ = NULL; }

    ApplyException& operator=(ApplyException ae)
    {
        using std::swap;
#if 1
        swap(static_cast<gu::Exception&>(*this),static_cast<gu::Exception&>(ae));
        swap(this->data_,       ae.data_);
        swap(this->const_data_, ae.const_data_);
        swap(this->data_len_,   ae.data_len_);
#else
        swap(*this, ae);
#endif
        return *this;
    }

private:
    void*  data_;
    const void* const_data_;
    size_t data_len_;
};

static inline const char* wsrep_status_str(wsrep_status_t& status)
{
    switch (status)
    {
    case WSREP_OK:          return "WSREP_OK";
    case WSREP_WARNING:     return "WSREP_WARNING";
    case WSREP_TRX_MISSING: return "WSREP_TRX_MISSING";
    case WSREP_TRX_FAIL:    return "WSREP_TRX_FAIL";
    case WSREP_BF_ABORT:    return "WSREP_BF_ABORT";
    case WSREP_CONN_FAIL:   return "WSREP_CONN_FAIL";
    case WSREP_NODE_FAIL:   return "WSREP_NODE_FAIL";
    case WSREP_FATAL:       return "WSREP_FATAL";
    case WSREP_NOT_IMPLEMENTED: return "WSREP_NOT_IMPLEMENTED";
    default: return "(unknown code)";
    }
}

/*!
 * And exception to handle replication errors
 */
class ReplException : public gu::Exception
{
public:
    ReplException (const std::string& msg, int err)
        : gu::Exception (msg, err)
    {}
};

}
#endif /* GALERA_EXCEPTION_HPP */
