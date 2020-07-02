//
// Copyright (C) 2020 Codership Oy <info@codership.com>
//

/** @file gu_asio_ip_address_impl.hpp
 *
 * IP address implementation wrappers.
 */

#ifndef GU_ASIO_IP_ADDRESS_IMPL_HPP
#define GU_ASIO_IP_ADDRESS_IMPL_HPP

#ifndef GU_ASIO_IMPL
#error This header should not be included directly.
#endif // GU_ASIO_IMPL

#include "gu_asio.hpp"

#include "asio/ip/address.hpp"

namespace gu
{
    class AsioIpAddressV4::Impl
    {
    public:
        Impl() : impl_() { }
        asio::ip::address_v4& native() { return impl_; }
        const asio::ip::address_v4& native() const { return impl_; }
    private:
        asio::ip::address_v4 impl_;
    };

    class AsioIpAddressV6::Impl
    {
    public:
        Impl() : impl_() { }
        asio::ip::address_v6& native() { return impl_; }
        const asio::ip::address_v6& native() const { return impl_; }
    private:
        asio::ip::address_v6 impl_;
    };

    class gu::AsioIpAddress::Impl
    {
    public:
        Impl() : impl_() {}
        asio::ip::address& native() { return impl_; }
        const asio::ip::address& native() const { return impl_; }
    private:
        asio::ip::address impl_;
    };
}

static inline std::string escape_addr(const asio::ip::address& addr)
{
    if (gu_likely(addr.is_v4() == true))
    {
        return addr.to_v4().to_string();
    }
    else
    {
        return "[" + addr.to_v6().to_string() + "]";
    }
}

static inline asio::ip::address make_address(const std::string& addr)
{
    return asio::ip::address::from_string(gu::unescape_addr(addr));
}

static inline std::string any_addr(const asio::ip::address& addr)
{
    if (gu_likely(addr.is_v4() == true))
    {
        return addr.to_v4().any().to_string();
    }
    else
    {
        return addr.to_v6().any().to_string();
    }
}

#endif // GU_ASIO_IP_ADDRESS_IMPL_HPP
