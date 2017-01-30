//
// Copyright (C) 2011-2014 Codership Oy <info@codership.com>
//


#ifndef GALERA_IST_HPP
#define GALERA_IST_HPP

#include <pthread.h>

#include "wsrep_api.h"
#include "galera_gcs.hpp"
#include "trx_handle.hpp"
#include "gu_config.hpp"
#include "gu_lock.hpp"
#include "gu_monitor.hpp"
#include "gu_asio.hpp"

#include <stack>
#include <set>

namespace gcache
{
    class GCache;
}

namespace galera
{
    class TrxHandle;

    namespace ist
    {
        void register_params(gu::Config& conf);

        class Receiver
        {
        public:
            static std::string const RECV_ADDR;
            static std::string const RECV_BIND;

            Receiver(gu::Config& conf, TrxHandle::SlavePool&, const char* addr);
            ~Receiver();

            std::string   prepare(wsrep_seqno_t, wsrep_seqno_t, int);
            void          ready();
            int           recv(TrxHandle** trx);
            wsrep_seqno_t finished();
            void          run();

        private:

            void interrupt();

            std::string                                   recv_addr_;
            std::string                                   recv_bind_;
            asio::io_service                              io_service_;
            asio::ip::tcp::acceptor                       acceptor_;
            asio::ssl::context                            ssl_ctx_;
#ifdef HAVE_PSI_INTERFACE
            gu::MutexWithPFS                              mutex_;
            gu::CondWithPFS                               cond_;
#else
            gu::Mutex                                     mutex_;
            gu::Cond                                      cond_;
#endif /* HAVE_PSI_INTERFACE */

            class Consumer
            {
            public:

#ifdef HAVE_PSI_INTERFACE
                Consumer()
                    :
                    cond_(WSREP_PFS_INSTR_TAG_IST_CONSUMER_CONDVAR),
                    trx_(0)
                { }
#else
                Consumer() : cond_(), trx_(0) { }
#endif
                ~Consumer() { }

#ifdef HAVE_PSI_INTERFACE
                gu::CondWithPFS&  cond()        { return cond_; }
#else
                gu::Cond&  cond()              { return cond_; }
#endif /* HAVE_PSI_INTERFACE */
                void       trx(TrxHandle* trx) { trx_ = trx;   }
                TrxHandle* trx() const         { return trx_;  }

            private:

                // Non-copyable
                Consumer(const Consumer&);
                Consumer& operator=(const Consumer&);

#ifdef HAVE_PSI_INTERFACE
                gu::CondWithPFS   cond_;
#else
                gu::Cond   cond_;
#endif /* HAVE_PSI_INTERFACE */
                TrxHandle* trx_;
            };

            std::stack<Consumer*> consumers_;
            wsrep_seqno_t         current_seqno_;
            wsrep_seqno_t         last_seqno_;
            gu::Config&           conf_;
            TrxHandle::SlavePool& trx_pool_;
            pthread_t             thread_;
            int                   error_code_;
            int                   version_;
            bool                  use_ssl_;
            bool                  running_;
            bool                  interrupted_;
            bool                  ready_;

            // GCC 4.8.5 on FreeBSD wants this
            Receiver(const Receiver&);
            Receiver& operator=(const Receiver&);
        };

        class Sender
        {
        public:

            Sender(const gu::Config& conf,
                   gcache::GCache& gcache,
                   const std::string& peer,
                   int version);
            virtual ~Sender();

            void send(wsrep_seqno_t first, wsrep_seqno_t last);

            void cancel()
            {
                if (use_ssl_ == true)
                {
                    ssl_stream_->lowest_layer().close();
                }
                else
                {
                    socket_.close();
                }
            }

        private:

            asio::io_service                          io_service_;
            asio::ip::tcp::socket                     socket_;
            asio::ssl::context                        ssl_ctx_;
            asio::ssl::stream<asio::ip::tcp::socket>* ssl_stream_;
            const gu::Config&                         conf_;
            gcache::GCache&                           gcache_;
            int                                       version_;
            bool                                      use_ssl_;

            Sender(const Sender&);
            void operator=(const Sender&);
        };


        class AsyncSender;
        class AsyncSenderMap
        {
        public:
            AsyncSenderMap(GCS_IMPL& gcs, gcache::GCache& gcache)
                :
                senders_(),
#ifdef HAVE_PSI_INTERFACE
                monitor_(WSREP_PFS_INSTR_TAG_ASYNC_SENDER_MONITOR_MUTEX,
                         WSREP_PFS_INSTR_TAG_ASYNC_SENDER_MONITOR_CONDVAR),
#else
                monitor_(),
#endif /* HAVE_PSI_INTERFACE */
                gcache_(gcache) { }
            void run(const gu::Config& conf,
                     const std::string& peer,
                     wsrep_seqno_t,
                     wsrep_seqno_t,
                     int);
            void remove(AsyncSender*, wsrep_seqno_t);
            void cancel();
            gcache::GCache& gcache() { return gcache_; }
        private:
            std::set<AsyncSender*> senders_;
            // use monitor instead of mutex, it provides cancellation point
            gu::Monitor            monitor_;
            gcache::GCache&        gcache_;
        };


    } // namespace ist
} // namespace galera

#endif // GALERA_IST_HPP
