//
// Copyright (C) 2011-2017 Codership Oy <info@codership.com>
//


#ifndef GALERA_IST_HPP
#define GALERA_IST_HPP

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


        // IST event handler interface
        class EventHandler
        {
        public:
            // Process transaction from IST
            virtual void ist_trx(const TrxHandleSlavePtr&, bool must_apply,
                                 bool preload) = 0;
            // Process conf change from IST
            virtual void ist_cc(const gcs_action&, bool must_apply,
                                bool preload) = 0;
            // Report IST end
            virtual void ist_end(int error) = 0;
        protected:
            virtual ~EventHandler() {}
        };

        class Receiver
        {
        public:
            static std::string const RECV_ADDR;
            static std::string const RECV_BIND;

            Receiver(gu::Config& conf, gcache::GCache&,
                     TrxHandleSlave::Pool& slave_pool,
                     EventHandler&, const char* addr);
            ~Receiver();

            std::string   prepare(wsrep_seqno_t       first_seqno,
                                  wsrep_seqno_t       last_seqno,
                                  int                 protocol_version,
                                  const wsrep_uuid_t& source_id);

            // this must be called AFTER SST is processed and we know
            // the starting point.
            void          ready(wsrep_seqno_t first);

            wsrep_seqno_t finished();
            void          run();

            wsrep_seqno_t first_seqno() const { return first_seqno_; }

        private:

            void interrupt();

            std::string                                   recv_addr_;
            std::string                                   recv_bind_;
            gu::AsioIoService                             io_service_;
            std::shared_ptr<gu::AsioAcceptor>             acceptor_;
            gu::Mutex                                     mutex_;
            gu::Cond                                      cond_;

            wsrep_seqno_t         first_seqno_;
            wsrep_seqno_t         last_seqno_;
            wsrep_seqno_t         current_seqno_;
            gu::Config&           conf_;
            gcache::GCache&       gcache_;
            TrxHandleSlave::Pool& slave_pool_;
            wsrep_uuid_t          source_id_;
            EventHandler&         handler_;
            gu_thread_t           thread_;
            int                   error_code_;
            int                   version_;
            bool                  use_ssl_;
            bool                  running_;
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

            // first - first trx seqno
            // last  - last trx seqno
            // preload_start - the seqno from which sent transactions
            // are accompanied with index preload flag
            void send(wsrep_seqno_t first, wsrep_seqno_t last,
                      wsrep_seqno_t preload_start);

            void cancel()
            {
                socket_->close();
            }

        private:

            gu::AsioIoService                         io_service_;
            std::shared_ptr<gu::AsioSocket>           socket_;
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
            AsyncSenderMap(gcache::GCache& gcache)
                :
                senders_(),
                monitor_(),
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
            gcache::GCache&        gcache_;
        };


    } // namespace ist

    // Helpers to determine receive addr and receive bind. Public for
    // testing.
    std::string IST_determine_recv_addr(gu::Config& conf);
    std::string IST_determine_recv_bind(gu::Config& conf);

} // namespace galera

#endif // GALERA_IST_HPP
