//
// Copyright (C) 2020 Codership Oy <info@codership.com>
//

#ifndef GU_ASIO_DEBUG_HPP
#define GU_ASIO_DEBUG_HPP

#ifndef GU_ASIO_IMPL
#error This header should not be included directly.
#endif // GU_ASIO_IMPL

// #define GU_ASIO_ENABLE_DEBUG
#ifdef GU_ASIO_ENABLE_DEBUG
#define GU_ASIO_DEBUG(msg_) log_info << msg_;
#else
#define GU_ASIO_DEBUG(msg_)
#endif /* GU_ASIO_ENABLE_DEBUG */

#endif // GU_ASIO_DEBUG_HPP
