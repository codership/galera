//
// Copyright (C) 2020 Codership Oy <info@codership.com>
//

#ifndef GU_ASIO_ERROR_CATEGORY_HPP
#define GU_ASIO_ERROR_CATEGORY_HPP

#ifndef GU_ASIO_IMPL
#error This header should not be included directly.
#endif // GU_ASIO_IMPL

#include "asio/error.hpp"

namespace gu
{
    class AsioErrorCategory
    {
    public:
        AsioErrorCategory(const asio::error_category& category)
            : category_(category)
        { }
        AsioErrorCategory(const AsioErrorCategory&) = delete;
        AsioErrorCategory& operator=(const AsioErrorCategory&) = delete;
        const asio::error_category& native() const { return category_; }
        bool operator==(const AsioErrorCategory& other) const
        {
            return (category_ == other.category_);
        }
        bool operator!=(const AsioErrorCategory& other) const
        {
            return not (*this == other);
        }
    private:
        const asio::error_category& category_;
    };
}

extern gu::AsioErrorCategory gu_asio_system_category;
extern gu::AsioErrorCategory gu_asio_misc_category;
#ifdef GALERA_HAVE_SSL
extern gu::AsioErrorCategory gu_asio_ssl_category;
#endif // GALERA_HAVE_SSL

#endif // GU_ASIO_ERROR_CATEGORY_HPP


