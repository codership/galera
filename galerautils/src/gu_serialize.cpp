/*
 * Copyright (C) 2017 Codership Oy <info@codership.com>
 */

#include "gu_serialize.hpp"

#include <sstream>

namespace gu
{
    class serialization_error_message
    {
        std::ostringstream os_;
    public:
        serialization_error_message(size_t a, size_t b) : os_()
        {
            os_ << a << " > " << b;
        }

        const std::ostringstream& os() const { return os_; }
    };

    SerializationException::SerializationException(size_t a, size_t b)
        :
        Exception(serialization_error_message(a, b).os().str(), EMSGSIZE)
    {}

    class representation_error_message
    {
        std::ostringstream os_;
    public:
        representation_error_message(size_t need, size_t have) : os_()
        {
            os_ << need << " unrepresentable in " << have <<" bytes.";
        }

        const std::ostringstream& os() const { return os_; }
    };

    RepresentationException::RepresentationException(size_t need, size_t have)
        :
        Exception(representation_error_message(need, have).os().str(), ERANGE)
    {}

} /* namespace gu */

