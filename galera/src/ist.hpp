//
// Copyright (C) 2011 Codership Oy <info@codership.com>
//


#ifndef GALERA_IST_HPP
#define GALERA_IST_HPP

#include "wsrep_api.h"
#include "gu_config.hpp"
#include "gu_lock.hpp"

#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#include "asio.hpp"

#include <stack>

namespace gcache
{
    class GCache;
}

namespace galera
{
    class TrxHandle;

    namespace ist
    {
        class Receiver
        {
        public:
            static std::string const RECV_ADDR;

            Receiver(gu::Config& conf, const char* addr);
            ~Receiver();
            std::string prepare();
            int recv(TrxHandle** trx);
            void finished();
            void run();
        private:
            gu::Config&                                   conf_;
            asio::io_service                              io_service_;
            asio::ip::tcp::acceptor                       acceptor_;
            pthread_t                                     thread_;
            gu::Mutex                                     mutex_;
            gu::Cond                                      cond_;
            class Consumer
            {
            public:
                Consumer()
                    :
                    cond_(),
                    trx_(0)
                { }
                ~Consumer() { }
                gu::Cond& cond() { return cond_; }
                void trx(TrxHandle* trx) { trx_ = trx; }
                TrxHandle* trx() const { return trx_; }
            private:
                gu::Cond   cond_;
                TrxHandle* trx_;
            };
            std::stack<Consumer*> consumers_;
            bool running_;
        };

        class Sender
        {
        public:
            Sender(gcache::GCache& gcache,
                   const std::string& peer);
            ~Sender();
            void send(wsrep_seqno_t first, wsrep_seqno_t last);
        private:
            Sender(const Sender&);
            void operator=(const Sender&);
            asio::io_service      io_service_;
            asio::ip::tcp::socket socket_;
            gcache::GCache&       gcache_;
        };
    } // namespace ist
} // namespace galera

#endif // GALERA_IST_HPP
