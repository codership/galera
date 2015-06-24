//
// Copyright (C) 2011-2014 Codership Oy <info@codership.com>
//


#include "ist.hpp"
#include "ist_proto.hpp"
#include "trx_handle.hpp"
#include "uuid.hpp"
#include "monitor.hpp"
#include "GCache.hpp"
#include "gu_arch.h"
#include "replicator_smm.hpp"
#include <check.h>

using namespace galera;

// Message tests

START_TEST(test_ist_message)
{

    using namespace galera::ist;

#if 0 /* This is a check for the old (broken) format */
    Message m3(3, Message::T_HANDSHAKE, 0x2, 3, 1001);

#if GU_WORDSIZE == 32
    fail_unless(serial_size(m3) == 20, "serial size %zu != 20",
                serial_size(m3));
#elif GU_WORDSIZE == 64
    fail_unless(serial_size(m3) == 24, "serial size %zu != 24",
                serial_size(m3));
#endif

    gu::Buffer buf(m3.serial_size());
    m3.serialize(&buf[0], buf.size(), 0);
    Message mu3(3);
    mu3.unserialize(&buf[0], buf.size(), 0);

    fail_unless(mu3.version() == 3);
    fail_unless(mu3.type()    == Message::T_HANDSHAKE);
    fail_unless(mu3.flags()   == 0x2);
    fail_unless(mu3.ctrl()    == 3);
    fail_unless(mu3.len()     == 1001);
#endif /* 0 */

    Message m4(4, Message::T_HANDSHAKE, 0x2, 3, 1001);
    fail_unless(m4.serial_size() == 12);

    gu::Buffer buf(m4.serial_size());
//    buf.clear();
//    buf.resize(m4.serial_size());
    m4.serialize(&buf[0], buf.size(), 0);

    Message mu4(4);
    mu4.unserialize(&buf[0], buf.size(), 0);
    fail_unless(mu4.version() == 4);
    fail_unless(mu4.type()    == Message::T_HANDSHAKE);
    fail_unless(mu4.flags()   == 0x2);
    fail_unless(mu4.ctrl()    == 3);
    fail_unless(mu4.len()     == 1001);
}
END_TEST

// IST tests

static pthread_barrier_t start_barrier;

class TestOrder
{
public:
    TestOrder(galera::TrxHandleSlave& trx) : trx_(trx) { }
    void lock() { }
    void unlock() { }
    wsrep_seqno_t seqno() const { return trx_.global_seqno(); }
    bool condition(wsrep_seqno_t last_entered,
                   wsrep_seqno_t last_left) const
    {
        return (last_left >= trx_.depends_seqno());
    }
#ifdef GU_DBUG_ON
    void debug_sync(gu::Mutex&) { }
#endif // GU_DBUG_ON
private:
    galera::TrxHandleSlave& trx_;
};

struct sender_args
{
    gcache::GCache& gcache_;
    const std::string& peer_;
    wsrep_seqno_t first_;
    wsrep_seqno_t last_;
    int version_;
    sender_args(gcache::GCache& gcache,
                const std::string& peer,
                wsrep_seqno_t first, wsrep_seqno_t last,
                int version)
        :
        gcache_(gcache),
        peer_  (peer),
        first_ (first),
        last_  (last),
        version_(version)
    { }
};


struct receiver_args
{
    std::string   listen_addr_;
    wsrep_seqno_t first_;
    wsrep_seqno_t last_;
    size_t        n_receivers_;
    TrxHandleSlave::Pool& trx_pool_;
    gcache::GCache& gcache_;
    int           version_;

    receiver_args(const std::string listen_addr,
                  wsrep_seqno_t first, wsrep_seqno_t last,
                  size_t n_receivers, TrxHandleSlave::Pool& sp,
                  gcache::GCache& gc, int version)
        :
        listen_addr_(listen_addr),
        first_      (first),
        last_       (last),
        n_receivers_(n_receivers),
        trx_pool_   (sp),
        gcache_     (gc),
        version_    (version)
    { }
};

struct trx_thread_args
{
    galera::ist::Receiver& receiver_;
    galera::TrxHandleSlave::Pool& pool_;
    galera::Monitor<TestOrder> monitor_;
    trx_thread_args(galera::ist::Receiver& receiver,
                    galera::TrxHandleSlave::Pool& pool)
        :
        receiver_(receiver),
        pool_(pool),
        monitor_()
    { }
};

extern "C" void* sender_thd(void* arg)
{
    mark_point();

    const sender_args* sargs(reinterpret_cast<const sender_args*>(arg));

    gu::Config conf;
    galera::ReplicatorSMM::InitConfig(conf, NULL, NULL);
    pthread_barrier_wait(&start_barrier);
    galera::ist::Sender sender(conf, sargs->gcache_, sargs->peer_,
                               sargs->version_);
    mark_point();
    sender.send(sargs->first_, sargs->last_, sargs->first_);
    mark_point();
    return 0;
}

extern "C" void* trx_thread(void* arg)
{
    trx_thread_args* targs(reinterpret_cast<trx_thread_args*>(arg));
    pthread_barrier_wait(&start_barrier);
    targs->receiver_.ready(targs->receiver_.first_seqno());

    while (true)
    {
        gcs_action act;
        int err;

        if ((err = targs->receiver_.recv(act)) != 0 ||
            GCS_ACT_UNKNOWN == act.type /* EOF */)
        {
            assert(act.buf == NULL);
            assert(act.size == 0);
            log_info << "terminated with " << err;
            return 0;
        }

        galera::TrxHandleSlave* trx(TrxHandleSlave::New(false, targs->pool_));

        if (GCS_ACT_WRITESET == act.type)
        {
            if (act.size > 0)
            {
                assert(act.buf != NULL);

                gu_trace(trx->unserialize(
                         static_cast<const gu::byte_t*>(act.buf), act.size, 0));

                trx->verify_checksum();
                trx->set_state(TrxHandle::S_CERTIFYING);
            }
            else
            {
                assert(act.buf == NULL);

                trx->set_received(NULL, -1, act.seqno_g);
                trx->set_depends_seqno(0);
            }
        }
        else
        {
            assert(act.type == GCS_ACT_CCHANGE);

            gcs_act_cchange const cc(act.buf, act.size);

            assert(act.seqno_g == cc.seqno);

            trx->set_received(NULL, -1, cc.seqno);
            trx->set_depends_seqno(cc.seqno - 1);
        }

        TestOrder to(*trx);
        targs->monitor_.enter(to);
        targs->monitor_.leave(to);
        trx->unref();
    }
    return 0;
}

class PreIST : public galera::ist::ActionHandler
{
public:
    void preload_index(const gcs_action& act) { }
    void wait(const wsrep_seqno_t& upto) { }
    void drain_monitors(const wsrep_seqno_t& upto) { }
    virtual ~PreIST() {}
};

extern "C" void* receiver_thd(void* arg)
{
    mark_point();

    receiver_args* rargs(reinterpret_cast<receiver_args*>(arg));

    gu::Config conf;
    galera::ReplicatorSMM::InitConfig(conf, NULL, NULL);

    mark_point();

    conf.set(galera::ist::Receiver::RECV_ADDR, rargs->listen_addr_);
    PreIST pre_ist;
    galera::ist::Receiver receiver(conf, rargs->gcache_, pre_ist, 0);
    rargs->listen_addr_ = receiver.prepare(rargs->first_, rargs->last_,
                                           rargs->version_);

    mark_point();

    std::vector<pthread_t> threads(rargs->n_receivers_);
    trx_thread_args trx_thd_args(receiver, rargs->trx_pool_);
    for (size_t i(0); i < threads.size(); ++i)
    {
        log_info << "starting trx thread " << i;
        pthread_create(&threads[0] + i, 0, &trx_thread, &trx_thd_args);
    }

    mark_point();

    trx_thd_args.monitor_.set_initial_position(rargs->first_ - 1);
    pthread_barrier_wait(&start_barrier);
    trx_thd_args.monitor_.wait(rargs->last_);

    for (size_t i(0); i < threads.size(); ++i)
    {
        log_info << "joining trx thread " << i;
        pthread_join(threads[i], 0);
    }

    receiver.finished();
    return 0;
}


static int select_trx_version(int protocol_version)
{
    // see protocol version table in replicator_smm.hpp
    switch (protocol_version)
    {
    case 1:
    case 2:
        return 1;
    case 3:
    case 4:
        return 2;
    case 5:
    case 6:
    case 7:
        return 3;
    case 8:
        return 4;
    default:
        fail("unsupported replicator protocol version: %n", protocol_version);
    }

    return -1;
}

static void store_trx(gcache::GCache* const gcache,
                      TrxHandleMaster::Pool& lp,
                      const TrxHandleMaster::Params& trx_params,
                      const wsrep_uuid_t& uuid,
                      int const i)
{
    TrxHandleMaster* trx(TrxHandleMaster::New(lp, trx_params, uuid, 1234+i,
                                              5678+i));

    const wsrep_buf_t key[2] = {
        {"key1", 4},
        {"key2", 4}
    };

    trx->append_key(KeyData(trx_params.version_, key, 2, WSREP_KEY_EXCLUSIVE,
                            true));
    trx->append_data("bar", 3, WSREP_DATA_ORDERED, true);
    assert (i > 0);
    int last_seen(i - 1);
    int pa_range(i);

    gu::byte_t* ptr(0);

    if (trx_params.version_ < 3)
    {
        fail("WS version %d not supported any more", trx_params.version_);
    }
    else
    {
        galera::WriteSetNG::GatherVector bufs;
        ssize_t trx_size(trx->write_set_out().gather(trx->source_id(),
                                                     trx->conn_id(),
                                                     trx->trx_id(),
                                                     bufs));
        trx->finalize(last_seen);
        ptr = static_cast<gu::byte_t*>(gcache->malloc(trx_size));

        /* concatenate buffer vector */
        gu::byte_t* p(ptr);
        for (size_t k(0); k < bufs->size(); ++k)
        {
            ::memcpy(p, bufs[k].ptr, bufs[k].size); p += bufs[k].size;
        }
        assert ((p - ptr) == trx_size);

        gu::Buf ws_buf = { ptr, trx_size };
        galera::WriteSetIn wsi(ws_buf);
        assert (wsi.last_seen() == last_seen);
        assert (wsi.pa_range()  == (wsi.version() < WriteSetNG::VER4 ?
                                    0 : WriteSetNG::MAX_PA_RANGE));
        wsi.set_seqno(i, pa_range);
        assert (wsi.seqno()     == int64_t(i));
        assert (wsi.pa_range()  == pa_range);
    }

    gcache->seqno_assign(ptr, i, GCS_ACT_WRITESET, (i - pa_range) <= 0);
    trx->unref();
}

static void store_cc(gcache::GCache* const gcache,
                      const wsrep_uuid_t& uuid,
                     int const i)
{
    static int conf_id(0);

    gcs_act_cchange cc;

    ::memcpy(&cc.uuid, &uuid, sizeof(uuid));

    cc.seqno = i;
    cc.conf_id = conf_id++;

    void* tmp;
    int   const cc_size(cc.write(&tmp));
    void* const cc_ptr(gcache->malloc(cc_size));

    fail_if(NULL == cc_ptr);
    memcpy(cc_ptr, tmp, cc_size);

    gcache->seqno_assign(cc_ptr, i, GCS_ACT_CCHANGE, i > 0);
}

static void test_ist_common(int const version)
{
    using galera::KeyData;
    using galera::TrxHandle;
    using galera::KeyOS;

    TrxHandleMaster::Pool lp(TrxHandleMaster::LOCAL_STORAGE_SIZE,4,"ist_common");
    TrxHandleSlave::Pool sp(sizeof(TrxHandleSlave), 4, "ist_common");

    int const trx_version(select_trx_version(version));
    TrxHandleMaster::Params const trx_params("", trx_version,
                                       galera::KeySet::MAX_VERSION);
    std::string const dir(".");

    gu::Config conf_sender;
    galera::ReplicatorSMM::InitConfig(conf_sender, NULL, NULL);
    std::string const gcache_sender_file("ist_sender.cache");
    conf_sender.set("gcache.name", gcache_sender_file);
    conf_sender.set("gcache.size", "16M");
    gcache::GCache* gcache_sender = new gcache::GCache(conf_sender, dir);

    gu::Config conf_receiver;
    galera::ReplicatorSMM::InitConfig(conf_receiver, NULL, NULL);
    std::string const gcache_receiver_file("ist_receiver.cache");
    conf_receiver.set("gcache.name", gcache_receiver_file);
    conf_receiver.set("gcache.size", "16M");
    gcache::GCache* gcache_receiver = new gcache::GCache(conf_receiver, dir);

    std::string receiver_addr("tcp://127.0.0.1:0");
    wsrep_uuid_t uuid;
    gu_uuid_generate(reinterpret_cast<gu_uuid_t*>(&uuid), 0, 0);

    mark_point();

    // populate gcache
    for (size_t i(1); i <= 10; ++i)
    {
        if (i % 3)
        {
            store_trx(gcache_sender, lp, trx_params, uuid, i);
        }
        else
        {
            store_cc(gcache_sender, uuid, i);
        }
    }

    mark_point();

    receiver_args rargs(receiver_addr, 1, 10, 1, sp, *gcache_receiver, version);
    sender_args sargs(*gcache_sender, rargs.listen_addr_, 1, 10, version);

    pthread_barrier_init(&start_barrier, 0, 1 + 1 + rargs.n_receivers_);

    pthread_t sender_thread, receiver_thread;

    pthread_create(&sender_thread, 0, &sender_thd, &sargs);
    mark_point();
    usleep(100000);
    pthread_create(&receiver_thread, 0, &receiver_thd, &rargs);
    mark_point();

    pthread_join(sender_thread, 0);
    pthread_join(receiver_thread, 0);

    mark_point();

    delete gcache_sender;
    delete gcache_receiver;

    mark_point();
    unlink(gcache_sender_file.c_str());
    unlink(gcache_receiver_file.c_str());
}

START_TEST(test_ist_v5)
{
    test_ist_common(5);
}
END_TEST

START_TEST(test_ist_v7)
{
    test_ist_common(7);
}
END_TEST

START_TEST(test_ist_v8)
{
    test_ist_common(8);
}
END_TEST

Suite* ist_suite()
{
    Suite* s  = suite_create("ist");
    TCase* tc;

    tc = tcase_create("test_ist_message");
    tcase_add_test(tc, test_ist_message);
    suite_add_tcase(s, tc);
    tc = tcase_create("test_ist_v5");
    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, test_ist_v5);
    suite_add_tcase(s, tc);
    tc = tcase_create("test_ist_v7");
    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, test_ist_v7);
    tc = tcase_create("test_ist_v8");
    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, test_ist_v8);
    suite_add_tcase(s, tc);

    return s;
}
