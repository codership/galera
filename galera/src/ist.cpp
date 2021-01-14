//
// Copyright (C) 2011-2019 Codership Oy <info@codership.com>
//

#include "ist.hpp"
#include "ist_proto.hpp"

#include "gu_logger.hpp"
#include "gu_uri.hpp"
#include "gu_debug_sync.hpp"
#include "gu_progress.hpp"

#include "galera_common.hpp"
#include <boost/bind.hpp>
#include <fstream>
#include <algorithm>

namespace
{
    static std::string const CONF_KEEP_KEYS     ("ist.keep_keys");
    static bool        const CONF_KEEP_KEYS_DEFAULT (true);
}


namespace galera
{
    namespace ist
    {
        class AsyncSender : public Sender
        {
        public:
            AsyncSender(const gu::Config& conf,
                        const std::string& peer,
                        wsrep_seqno_t first,
                        wsrep_seqno_t last,
                        wsrep_seqno_t preload_start,
                        AsyncSenderMap& asmap,
                        int version)
                :
                Sender (conf, asmap.gcache(), peer, version),
                conf_  (conf),
                peer_  (peer),
                first_ (first),
                last_  (last),
                preload_start_(preload_start),
                asmap_ (asmap),
                thread_()
            { }

            const gu::Config&  conf()   { return conf_;   }
            const std::string& peer()  const { return peer_;   }
            wsrep_seqno_t      first() const { return first_;  }
            wsrep_seqno_t      last()  const { return last_;   }
            wsrep_seqno_t      preload_start() const { return preload_start_; }
            AsyncSenderMap&    asmap()  { return asmap_;  }
            gu_thread_t          thread() { return thread_; }

        private:

            friend class AsyncSenderMap;

            const gu::Config&   conf_;
            std::string const   peer_;
            wsrep_seqno_t const first_;
            wsrep_seqno_t const last_;
            wsrep_seqno_t const preload_start_;
            AsyncSenderMap&     asmap_;
            gu_thread_t        thread_;

            // GCC 4.8.5 on FreeBSD wants it
            AsyncSender(const AsyncSender&);
            AsyncSender& operator=(const AsyncSender&);
        };
    }
}


std::string const
galera::ist::Receiver::RECV_ADDR("ist.recv_addr");
std::string const
galera::ist::Receiver::RECV_BIND("ist.recv_bind");

void
galera::ist::register_params(gu::Config& conf)
{
    conf.add(Receiver::RECV_ADDR);
    conf.add(Receiver::RECV_BIND);
    conf.add(CONF_KEEP_KEYS);
}

galera::ist::Receiver::Receiver(gu::Config&           conf,
                                gcache::GCache&       gc,
                                TrxHandleSlave::Pool& slave_pool,
                                EventHandler&         handler,
                                const char*           addr)
    :
    recv_addr_    (),
    recv_bind_    (),
    io_service_   (conf),
    acceptor_     (),
    mutex_        (),
    cond_         (),
    first_seqno_  (WSREP_SEQNO_UNDEFINED),
    last_seqno_   (WSREP_SEQNO_UNDEFINED),
    current_seqno_(WSREP_SEQNO_UNDEFINED),
    conf_         (conf),
    gcache_       (gc),
    slave_pool_   (slave_pool),
    source_id_    (WSREP_UUID_UNDEFINED),
    handler_      (handler),
    thread_       (),
    error_code_   (0),
    version_      (-1),
    use_ssl_      (false),
    running_      (false),
    ready_        (false)
{
    std::string recv_addr;
    std::string recv_bind;

    try
    {
        recv_bind = conf_.get(RECV_BIND);
        // no return
    }
    catch (gu::NotSet& e) {}

    try /* check if receive address is explicitly set */
    {
        recv_addr = conf_.get(RECV_ADDR);
        return;
    }
    catch (gu::NotSet& e) {} /* if not, check the alternative.
                                TODO: try to find from system. */

    if (addr)
    {
        try
        {
            recv_addr = gu::URI(std::string("tcp://") + addr).get_host();
            conf_.set(RECV_ADDR, recv_addr);
        }
        catch (gu::NotSet& e) {}
    }
}


galera::ist::Receiver::~Receiver()
{ }


extern "C" void* run_receiver_thread(void* arg)
{
    galera::ist::Receiver* receiver(static_cast<galera::ist::Receiver*>(arg));
    receiver->run();
    return 0;
}

static void IST_fix_addr_scheme(const gu::Config& conf, std::string& addr)
{
    /* check if explicit scheme is present */
    if (addr.find("://") == std::string::npos)
    {
#ifdef GALERA_HAVE_SSL
        try
        {
            std::string ssl_key = conf.get(gu::conf::ssl_key);
            if (ssl_key.length() != 0)
            {
                addr.insert(0, "ssl://");
                return;
            }
        }
        catch (gu::NotSet&) {}
#endif // GALERA_HAVE_SSL
        addr.insert(0, "tcp://");
    }
}

static void IST_fix_addr_port(const gu::Config& conf, const gu::URI& uri,
                              std::string& addr)
{
    try /* check for explicit port,
           TODO: make it possible to use any free port (explicit 0?) */
    {
        uri.get_port();
    }
    catch (gu::NotSet&) /* use gmcast listen port + 1 */
    {
        int port(0);

        try
        {
            port = gu::from_string<uint16_t>(conf.get(galera::BASE_PORT_KEY));
        }
        catch (...)
        {
            port = gu::from_string<uint16_t>(galera::BASE_PORT_DEFAULT);
        }

        port += 1;

        addr += ":" + gu::to_string(port);
    }
}

std::string galera::IST_determine_recv_addr (gu::Config& conf)
{
    std::string recv_addr;

    try
    {
        recv_addr = conf.get(galera::ist::Receiver::RECV_ADDR);
    }
    catch (const gu::NotSet&)
    {
        try
        {
            recv_addr = conf.get(galera::BASE_HOST_KEY);
        }
        catch (const gu::NotSet&)
        {
            gu_throw_error(EINVAL)
                << "Could not determine IST receive address: '"
                << galera::ist::Receiver::RECV_ADDR << "' or '"
                << galera::BASE_HOST_KEY << "' not set.";
        }
    }

    IST_fix_addr_scheme(conf, recv_addr);
    gu::URI ra_uri(recv_addr);

    if (!conf.has(galera::BASE_HOST_KEY))
        conf.set(galera::BASE_HOST_KEY, ra_uri.get_host());

    IST_fix_addr_port(conf, ra_uri, recv_addr);

    log_info << "IST receiver addr using " << recv_addr;
    return recv_addr;
}

std::string galera::IST_determine_recv_bind(gu::Config& conf)
{
    std::string recv_bind;

    recv_bind = conf.get(galera::ist::Receiver::RECV_BIND);

    IST_fix_addr_scheme(conf, recv_bind);

    gu::URI rb_uri(recv_bind);

    IST_fix_addr_port(conf, rb_uri, recv_bind);

    log_info << "IST receiver bind using " << recv_bind;
    return recv_bind;
}

std::string
galera::ist::Receiver::prepare(wsrep_seqno_t const first_seqno,
                               wsrep_seqno_t const last_seqno,
                               int           const version,
                               const wsrep_uuid_t& source_id)
{
    ready_ = false;
    version_ = version;
    source_id_ = source_id;
    recv_addr_ = IST_determine_recv_addr(conf_);
    try
    {
        recv_bind_ = IST_determine_recv_bind(conf_);
    }
    catch (gu::NotSet&)
    {
        recv_bind_ = recv_addr_;
    }

    // uri_bind will be the real bind address which the acceptor will
    // listen. The recv_addr_ returned from this call may point to
    // other address, for example if the node is behind NATting firewall.
    gu::URI     const uri_bind(recv_bind_);
    try
    {
        if (uri_bind.get_scheme() == "ssl")
        {
            log_info << "IST receiver using ssl";
            use_ssl_ = true;
            // Protocol versions prior 7 had a bug on sender side
            // which made sender to return null cert in handshake.
            // Therefore peer cert verfification must be enabled
            // only at protocol version 7 or higher.
            // Removed in 4.x asio refactoring.
            // gu::ssl_prepare_context(conf_, ssl_ctx_, version >= 7);
        }

        acceptor_ = io_service_.make_acceptor(uri_bind);
        acceptor_->listen(uri_bind);
        // read recv_addr_ from acceptor_ in case zero port was specified
        gu::URI const uri_addr(recv_addr_);
        recv_addr_ = uri_addr.get_scheme()
            + "://"
            + uri_addr.get_host()
            + ":"
            + gu::to_string(acceptor_->listen_port());
    }
    catch (const gu::Exception& e)
    {
        recv_addr_ = "";
        gu_throw_error(e.get_errno())
            << "Failed to open IST listener at "
            << uri_bind.to_string()
            << "', asio error '" << e.what() << "'";
    }

    first_seqno_   = first_seqno;
    last_seqno_    = last_seqno;

    int err;
    if ((err = gu_thread_create(&thread_, 0, &run_receiver_thread, this)) != 0)
    {
        recv_addr_ = "";
        gu_throw_error(err) << "Unable to create receiver thread";
    }

    running_ = true;

    log_info << "Prepared IST receiver for " << first_seqno << '-'
             << last_seqno << ", listening at: "
             << acceptor_->listen_addr();

    return recv_addr_;
}


void galera::ist::Receiver::run()
{
    auto socket(acceptor_->accept());
    acceptor_->close();

    /* shall be initialized below, when we know at what seqno preload starts */
    gu::Progress<wsrep_seqno_t>* progress(NULL);

    int ec(0);

    try
    {
        bool const keep_keys(conf_.get(CONF_KEEP_KEYS, CONF_KEEP_KEYS_DEFAULT));
        Proto p(gcache_, version_, keep_keys);

        p.send_handshake(*socket);
        p.recv_handshake_response(*socket);
        p.send_ctrl(*socket, Ctrl::C_OK);

        // wait for SST to complete so that we know what is the first_seqno_
        {
            gu::Lock lock(mutex_);
            while (ready_ == false) { lock.wait(cond_); }
        }
        log_info << "####### IST applying starts with " << first_seqno_; //remove
        assert(first_seqno_ > 0);

        bool preload_started(false);
        current_seqno_ = WSREP_SEQNO_UNDEFINED;

        while (true)
        {
            std::pair<gcs_action, bool> ret;
            p.recv_ordered(*socket, ret);

            gcs_action& act(ret.first);

            // act type GCS_ACT_UNKNOWN denotes EOF
            if (gu_unlikely(act.type == GCS_ACT_UNKNOWN))
            {
                assert(0    == act.seqno_g);
                assert(NULL == act.buf);
                assert(0    == act.size);
                log_debug << "eof received, closing socket";
                break;
            }

            assert(act.seqno_g > 0);

            if (gu_unlikely(WSREP_SEQNO_UNDEFINED == current_seqno_))
            {
                assert(!progress);
                if (act.seqno_g > first_seqno_)
                {
                    log_error
                        << "IST started with wrong seqno: " << act.seqno_g
                        << ", expected <= " << first_seqno_;
                    ec = EINVAL;
                    goto err;
                }
                log_info << "####### IST current seqno initialized to "
                         << act.seqno_g;
                current_seqno_ = act.seqno_g;
                progress = new gu::Progress<wsrep_seqno_t>(
                    "Receiving IST", " events",
                    last_seqno_ - current_seqno_ + 1,
                    /* The following means reporting progress NO MORE frequently
                     * than once per BOTH 10 seconds (default) and 16 events */
                    16);
            }
            else
            {
                assert(progress);

                ++current_seqno_;

                progress->update(1);
            }

            if (act.seqno_g != current_seqno_)
            {
                log_error << "Unexpected action seqno: " << act.seqno_g
                          << " expected: " << current_seqno_;
                ec = EINVAL;
                goto err;
            }

            assert(current_seqno_ > 0);
            assert(current_seqno_ == act.seqno_g);
            assert(act.type != GCS_ACT_UNKNOWN);

            bool const must_apply(current_seqno_ >= first_seqno_);
            bool const preload(ret.second);

            if (gu_unlikely(preload == true && preload_started == false))
            {
                log_info << "IST preload starting at " << current_seqno_;
                preload_started = true;
            }

            switch (act.type)
            {
            case GCS_ACT_WRITESET:
            {
                TrxHandleSlavePtr ts(
                    TrxHandleSlavePtr(TrxHandleSlave::New(false,
                                                          slave_pool_),
                                      TrxHandleSlaveDeleter()));
                if (act.size > 0)
                {
                    gu_trace(ts->unserialize<false>(act));
                    ts->set_local(false);
                    assert(ts->global_seqno() == act.seqno_g);
                    assert(ts->depends_seqno() >= 0 || ts->nbo_end());
                    assert(ts->action().first && ts->action().second);
                    // Checksum is verified later on
                }
                else
                {
                    ts->set_global_seqno(act.seqno_g);
                    ts->mark_dummy_with_action(act.buf);
                }

                //log_info << "####### Passing WS " << act.seqno_g;
                handler_.ist_trx(ts, must_apply, preload);
                break;
            }
            case GCS_ACT_CCHANGE:
                //log_info << "####### Passing IST CC " << act.seqno_g
                //         << ", must_apply: " << must_apply
                //         << ", preload: " << preload;
                handler_.ist_cc(act, must_apply, preload);
                break;
            default:
                assert(0);
            }
        }

        if (progress /* IST actually started */) progress->finish();
    }
    catch (gu::Exception& e)
    {
        ec = e.get_errno();
        if (ec != EINTR)
        {
            log_error << "got exception while reading IST stream: " << e.what();
        }
    }

err:
    delete progress;
    gu::Lock lock(mutex_);
    socket->close();

    running_ = false;
    if (last_seqno_ > 0 && ec != EINTR && current_seqno_ < last_seqno_)
    {
        log_error << "IST didn't contain all write sets, expected last: "
                  << last_seqno_ << " last received: " << current_seqno_;
        ec = EPROTO;
    }
    if (ec != EINTR)
    {
        error_code_ = ec;
    }
    handler_.ist_end(ec);
}


void galera::ist::Receiver::ready(wsrep_seqno_t const first)
{
    assert(first > 0);

    gu::Lock lock(mutex_);

    first_seqno_ = first;
    ready_       = true;
    cond_.signal();
}




wsrep_seqno_t galera::ist::Receiver::finished()
{
    if (recv_addr_ == "")
    {
        log_debug << "IST was not prepared before calling finished()";
    }
    else
    {
        interrupt();

        int err;
        if ((err = gu_thread_join(thread_, 0)) != 0)
        {
            log_warn << "Failed to join IST receiver thread: " << err;
        }

        acceptor_->close();

        gu::Lock lock(mutex_);

        running_ = false;

        recv_addr_ = "";
    }

    return current_seqno_;
}


void galera::ist::Receiver::interrupt()
{
    gu::URI uri(recv_addr_);
    try
    {
        auto socket(io_service_.make_socket(uri));
        socket->connect(uri);
        Proto p(gcache_, version_,
                conf_.get(CONF_KEEP_KEYS, CONF_KEEP_KEYS_DEFAULT));
        p.recv_handshake(*socket);
        p.send_ctrl(*socket, Ctrl::C_EOF);
        p.recv_ctrl(*socket);
    }
    catch (const gu::Exception&)
    {
        // ignore
    }
}


galera::ist::Sender::Sender(const gu::Config&  conf,
                            gcache::GCache&    gcache,
                            const std::string& peer,
                            int                version)
    :
    io_service_(conf),
    socket_    (),
    conf_      (conf),
    gcache_    (gcache),
    version_   (version),
    use_ssl_   (false)
{
    gu::URI uri(peer);
    try
    {
        socket_ = io_service_.make_socket(uri);
        socket_->connect(uri);
    }
    catch (const gu::Exception& e)
    {
        gu_throw_error(e.get_errno()) << "IST sender, failed to connect '"
                                      << peer.c_str() << "': " << e.what();
    }
}


galera::ist::Sender::~Sender()
{
    socket_->close();
    gcache_.seqno_unlock();
}

void send_eof(galera::ist::Proto& p, gu::AsioSocket& socket)
{

    p.send_ctrl(socket, galera::ist::Ctrl::C_EOF);

    // wait until receiver closes the connection
    try
    {
        gu::byte_t b;
        size_t n;
        n = socket.read(gu::AsioMutableBuffer(&b, 1));
        if (n > 0)
        {
            log_warn << "received " << n
                     << " bytes, expected none";
        }
    }
    catch (const gu::Exception& e)
    { }
}

void galera::ist::Sender::send(wsrep_seqno_t first, wsrep_seqno_t last,
                               wsrep_seqno_t preload_start)
{
    if (first > last)
    {
        if (version_ < VER40)
        {
            assert(0);
            gu_throw_error(EINVAL) << "sender send first greater than last: "
                                   << first << " > " << last ;
        }
    }

    try
    {
        Proto p(gcache_,
                version_, conf_.get(CONF_KEEP_KEYS, CONF_KEEP_KEYS_DEFAULT));
        int32_t ctrl;

        p.recv_handshake(*socket_);
        p.send_handshake_response(*socket_);
        ctrl = p.recv_ctrl(*socket_);

        if (ctrl < 0)
        {
            gu_throw_error(EPROTO)
                << "IST handshake failed, peer reported error: " << ctrl;
        }

        // send eof even if the set or transactions sent would be empty
        if (first > last || (first == 0 && last == 0))
        {
            log_info << "IST sender notifying joiner, not sending anything";
            send_eof(p, *socket_);
            return;
        }
        else
        {
            log_info << "IST sender " << first << " -> " << last;
        }

        std::vector<gcache::GCache::Buffer> buf_vec(
            std::min(static_cast<size_t>(last - first + 1),
                     static_cast<size_t>(1024)));
        ssize_t n_read;
        while ((n_read = gcache_.seqno_get_buffers(buf_vec, first)) > 0)
        {
            GU_DBUG_SYNC_WAIT("ist_sender_send_after_get_buffers");
            //log_info << "read " << first << " + " << n_read << " from gcache";
            for (wsrep_seqno_t i(0); i < n_read; ++i)
            {
                // Preload start is the seqno of the lowest trx in
                // cert index at CC. If the cert index was completely
                // reset, preload_start will be zero and no preload flag
                // should be set.
                bool preload_flag(preload_start > 0 &&
                                  buf_vec[i].seqno_g() >= preload_start);
                //log_info << "Sender::send(): seqno " << buf_vec[i].seqno_g()
                //         << ", size " << buf_vec[i].size() << ", preload: "
                //         << preload_flag;
                p.send_ordered(*socket_, buf_vec[i], preload_flag);

                if (buf_vec[i].seqno_g() == last)
                {
                    send_eof(p, *socket_);
                    return;
                }
            }
            first += n_read;
            // resize buf_vec to avoid scanning gcache past last
            size_t next_size(std::min(static_cast<size_t>(last - first + 1),
                                      static_cast<size_t>(1024)));
            if (buf_vec.size() != next_size)
            {
                buf_vec.resize(next_size);
            }
        }
    }
    catch (const gu::Exception& e)
    {
        gu_throw_error(e.get_errno()) << "ist send failed: "
                                      << "', asio error '" << e.what()
                                      << "'";
    }
}




extern "C"
void* run_async_sender(void* arg)
{
    galera::ist::AsyncSender* as
        (reinterpret_cast<galera::ist::AsyncSender*>(arg));

    log_info << "async IST sender starting to serve " << as->peer().c_str()
             << " sending " << as->first() << "-" << as->last()
             << ", preload starts from " << as->preload_start();

    wsrep_seqno_t join_seqno;

    try
    {
        as->send(as->first(), as->last(), as->preload_start());
        join_seqno = as->last();
    }
    catch (gu::Exception& e)
    {
        log_error << "async IST sender failed to serve " << as->peer().c_str()
                  << ": " << e.what();
        join_seqno = -e.get_errno();
    }
    catch (...)
    {
        log_error << "async IST sender, failed to serve " << as->peer().c_str();
        throw;
    }

    try
    {
        as->asmap().remove(as, join_seqno);
        gu_thread_detach(as->thread());
        delete as;
    }
    catch (gu::NotFound& nf)
    {
        log_debug << "async IST sender already removed";
    }
    log_info << "async IST sender served";

    return 0;
}


void galera::ist::AsyncSenderMap::run(const gu::Config&   conf,
                                      const std::string&  peer,
                                      wsrep_seqno_t const first,
                                      wsrep_seqno_t const last,
                                      wsrep_seqno_t const preload_start,
                                      int const           version)
{
    gu::Critical crit(monitor_);
    AsyncSender* as(new AsyncSender(conf, peer, first, last, preload_start,
                                    *this, version));
    int err(gu_thread_create(&as->thread_, 0, &run_async_sender, as));
    if (err != 0)
    {
        delete as;
        gu_throw_error(err) << "failed to start sender thread";
    }
    senders_.insert(as);
}


void galera::ist::AsyncSenderMap::remove(AsyncSender* as, wsrep_seqno_t seqno)
{
    gu::Critical crit(monitor_);
    std::set<AsyncSender*>::iterator i(senders_.find(as));
    if (i == senders_.end())
    {
        throw gu::NotFound();
    }
    senders_.erase(i);
}


void galera::ist::AsyncSenderMap::cancel()
{
    gu::Critical crit(monitor_);
    while (senders_.empty() == false)
    {
        AsyncSender* as(*senders_.begin());
        senders_.erase(*senders_.begin());
        int err;
        as->cancel();
        monitor_.leave();
        if ((err = gu_thread_join(as->thread_, 0)) != 0)
        {
            log_warn << "thread_join() failed: " << err;
        }
        monitor_.enter();
        delete as;
    }

}
