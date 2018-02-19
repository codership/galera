/*
 * Copyright (C) 2017 Codership Oy <info@codership.com>
 */

#include "gu_uuid.hpp"

#include <sstream>

namespace
{
    class scan_error_message
    {
        std::ostringstream os_;
    public:
        scan_error_message(const std::string& s) : os_()
        {
            os_ << "could not parse UUID from '" << s << '\'';
        }

        const std::ostringstream& os() const { return os_; }
    };

    class serialization_error_message
    {
        std::ostringstream os_;
    public:
        serialization_error_message(size_t need, size_t have) : os_()
        {
            os_ << need << " > " << have;
        }

        const std::ostringstream& os() const { return os_; }
    };
}

namespace gu
{
    UUIDScanException::UUIDScanException(const std::string& s)
        :
        Exception(scan_error_message(s).os().str(), EINVAL)
    {}

    UUIDSerializeException::UUIDSerializeException(size_t a, size_t b)
        :
        Exception(serialization_error_message(a, b).os().str(), EMSGSIZE)
    {}
}
