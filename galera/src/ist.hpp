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
    namespace ist
    {
        void register_params(gu::Config& conf);

        // Interface to handle cert index preload events.
        // These include trxs and configuration changes received
        // from donor that have global seqno below IST start.
        class ActionHandler
        {
        public:
            virtual void preload_index(const gcs_action&) = 0;
            virtual void wait(const wsrep_seqno_t&) = 0;
            virtual void drain_monitors(const wsrep_seqno_t&) = 0;
        protected:
            ~ActionHandler() {}
        };

        class Receiver
        {
        public:
            static std::string const RECV_ADDR;

            Receiver(gu::Config& conf, TrxHandleSlave::Pool&, gcache::GCache&,
                     ActionHandler&, const char* addr);
            ~Receiver();

            std::string   prepare(wsrep_seqno_t, wsrep_seqno_t, int);

            // this must be called AFTER SST is processed and we know
            // the starting point.
            void          ready(wsrep_seqno_t first);

            int           recv(gcs_action& act);
            wsrep_seqno_t finished();
            void          run();

            wsrep_seqno_t first_seqno() const { return first_seqno_; }

        private:

            void interrupt();

            std::string                                   recv_addr_;
            asio::io_service                              io_service_;
            asio::ip::tcp::acceptor                       acceptor_;
            asio::ssl::context                            ssl_ctx_;
            gu::Mutex                                     mutex_;
            gu::Cond                                      cond_;

            class Consumer
            {
            public:

                Consumer() : cond_(), act_() { }
                ~Consumer() { }

                gu::Cond&         cond()                     { return cond_; }
                void              act(const gcs_action& act) { act_ = act;   }
                const gcs_action& act() const                { return act_;  }

            private:

                gu::Cond        cond_;
                gcs_action      act_;
            };

            std::stack<Consumer*> consumers_;
            wsrep_seqno_t         first_seqno_;
            wsrep_seqno_t         last_seqno_;
            wsrep_seqno_t         current_seqno_;
            gu::Config&           conf_;
            TrxHandleSlave::Pool& trx_pool_;
            gcache::GCache&       gcache_;
            ActionHandler&        act_handler_;
            pthread_t             thread_;
            int                   error_code_;
            int                   version_;
            bool                  use_ssl_;
            bool                  running_;
            bool                  ready_;
        };

        class Sender
        {
        public:

            Sender(const gu::Config& conf,
                   gcache::GCache& gcache,
                   const std::string& peer,
                   int version);
            ~Sender();

            // first - first trx seqno
            // last  - last trx seqno
            // preload_start - the seqno from which sent transactions
            // are accompanied with index preload flag
            void send(wsrep_seqno_t first, wsrep_seqno_t last,
                      wsrep_seqno_t preload_start);

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
                monitor_(),
                gcs_(gcs),
                gcache_(gcache)
            { }

            void run(const gu::Config& conf,
                     const std::string& peer,
                     wsrep_seqno_t first,
                     wsrep_seqno_t last,
                     wsrep_seqno_t preload_start,
                     int           version);

            void remove(AsyncSender*, wsrep_seqno_t);
            void cancel();
            gcache::GCache& gcache() { return gcache_; }
        private:
            std::set<AsyncSender*> senders_;
            // use monitor instead of mutex, it provides cancellation point
            gu::Monitor            monitor_;
            GCS_IMPL&              gcs_;
            gcache::GCache&        gcache_;
        };


    } // namespace ist
} // namespace galera

#endif // GALERA_IST_HPP
