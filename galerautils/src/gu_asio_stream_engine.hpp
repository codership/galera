//
// Copyright (C) 2020 Codership Oy <info@codership.com>
//

#ifndef GU_ASIO_STREAM_ENGINE_HPP
#define GU_ASIO_STREAM_ENGINE_HPP

/** @file gu_asio_stream_engine.hpp
 *
 * Interface definition for reactive stream processing engine.
 */

#ifndef GU_ASIO_IMPL
#error This header should not be included directly.
#endif // GU_ASIO_IMPL

#include "gu_asio.hpp"
#include "wsrep_tls_service.h"

#include <cassert>
#include <memory>

namespace gu
{
    class AsioIoService;
    // Stream processing engine interface.
    class AsioStreamEngine
    {
    public:
        enum op_status
        {
            /** Operation completed successfully. */
            success = 0,
            /**
             * Operation completed successfully, but the stream
             * processing engine wants to read more.
             */
            want_read,
            /**
             * Operation completed successfully, but the stream
             * processing engine wants to write more.
             */
            want_write,
            /**
             * Stream end of file was encountered.
             */
            eof,
            /**
             * Error was encountered.
             */
            error
        };

        struct op_result
        {
            /** Status code of the operation or negative error number. */
            op_status status;
            /** Bytes transferred from/to given buffer during the operation. */
            size_t bytes_transferred;
        };

        virtual ~AsioStreamEngine() { }

        AsioStreamEngine(const AsioStreamEngine&) = delete;
        AsioStreamEngine& operator=(const AsioStreamEngine&) = delete;

        /**
         * Used to assign file descriptor to engines which were
         * dependency injected when AsioStreamReact was constructured.
         * This should be never called for engines which were created
         * internally, did not override assign_fd() and fd was provided
         * during construction. Keeping assert to detect violations of
         * this convention.
         */
        virtual void assign_fd(int fd) { assert(0); }

        virtual enum op_status client_handshake() = 0;
        virtual enum op_status server_handshake() = 0;
        /**
         * Shut down the stream processing engine. This must however
         * not close the file descriptor passed on construction.
         */
        virtual void shutdown() = 0;

        /**
         *
         * @param buf Buffer to read into.
         * @param max_count Maximum number of bytes to read into buf.
         *
         * @return op_result.
         */
        virtual op_result read(void* buf, size_t max_count) = 0;

        /**
         * Write buffer.
         *
         */
        virtual op_result write(const void* buf, size_t count) = 0;

        /**
         * Return last error code.
         */
        virtual AsioErrorCode last_error() const = 0;

        /**
         * Make a new AsioStreamEngine.
         *
         * @param scheme Desired scheme for stream engine.
         * @param fd File descriptor associated to the stream.
         */
        static std::shared_ptr<AsioStreamEngine> make(
            AsioIoService&, const std::string& scheme, int fd);

    protected:
        AsioStreamEngine() { }
    };
    std::ostream& operator<<(std::ostream&, enum AsioStreamEngine::op_status);
}

#endif // GU_ASIO_STREAM_ENGINE_HPP
