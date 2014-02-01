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

// Message tests

START_TEST(test_ist_message)
{

    using namespace galera::ist;

    Message m3(3, Message::T_HANDSHAKE, 0x2, 3, 1001);

#if 0 /* This is a check for the old (broken) format */
#if GU_WORDSIZE == 32
    fail_unless(serial_size(m3) == 20, "serial size %zu != 20",
                serial_size(m3));
#elif GU_WORDSIZE == 64
    fail_unless(serial_size(m3) == 24, "serial size %zu != 24",
                serial_size(m3));
#endif
#endif /* 0 */

    gu::Buffer buf(m3.serial_size());
    m3.serialize(&buf[0], buf.size(), 0);
    Message mu3(3);
    mu3.unserialize(&buf[0], buf.size(), 0);

    fail_unless(mu3.version() == 3);
    fail_unless(mu3.type()    == Message::T_HANDSHAKE);
    fail_unless(mu3.flags()   == 0x2);
    fail_unless(mu3.ctrl()    == 3);
    fail_unless(mu3.len()     == 1001);

    Message m4(4, Message::T_HANDSHAKE, 0x2, 3, 1001);
    fail_unless(m4.serial_size() == 12);

    buf.clear();
    buf.resize(m4.serial_size());
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
    TestOrder(galera::TrxHandle& trx) : trx_(trx) { }
    void lock() { }
    void unlock() { }
    wsrep_seqno_t seqno() const { return trx_.global_seqno(); }
    bool condition(wsrep_seqno_t last_entered,
                   wsrep_seqno_t last_left) const
    {
        return (last_left >= trx_.depends_seqno());
    }
private:
    galera::TrxHandle& trx_;
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
    int           version_;
    receiver_args(const std::string listen_addr,
        wsrep_seqno_t first, wsrep_seqno_t last,
                  size_t n_receivers, int version)
        :
        listen_addr_(listen_addr),
        first_(first),
        last_(last),
        n_receivers_(n_receivers),
        version_(version)
    { }
};

struct trx_thread_args
{
    galera::ist::Receiver& receiver_;
    galera::Monitor<TestOrder> monitor_;
    trx_thread_args(galera::ist::Receiver& receiver)
        :
        receiver_(receiver),
        monitor_()
    { }
};

extern "C" void* sender_thd(void* arg)
{
    mark_point();

    const sender_args* sargs(reinterpret_cast<const sender_args*>(arg));
    gu::Config conf;
    galera::ReplicatorSMM::InitConfig(conf, NULL);
    pthread_barrier_wait(&start_barrier);
    galera::ist::Sender sender(conf, sargs->gcache_, sargs->peer_,
                               sargs->version_);
    mark_point();
    sender.send(sargs->first_, sargs->last_);
    return 0;
}

extern "C" void* trx_thread(void* arg)
{
    trx_thread_args* targs(reinterpret_cast<trx_thread_args*>(arg));
    pthread_barrier_wait(&start_barrier);
    targs->receiver_.ready();
    while (true)
    {
        galera::TrxHandle* trx(0);
        int err;
        if ((err = targs->receiver_.recv(&trx)) != 0)
        {
            assert(trx == 0);
            log_info << "terminated with " << err;
            return 0;
        }
        TestOrder to(*trx);
        targs->monitor_.enter(to);
        targs->monitor_.leave(to);
        trx->unref();
    }
    return 0;
}

extern "C" void* receiver_thd(void* arg)
{
    mark_point();

    receiver_args* rargs(reinterpret_cast<receiver_args*>(arg));

    gu::Config conf;
    galera::ReplicatorSMM::InitConfig(conf, NULL);

    mark_point();

    conf.set(galera::ist::Receiver::RECV_ADDR, rargs->listen_addr_);
    galera::ist::Receiver receiver(conf, 0);
    rargs->listen_addr_ = receiver.prepare(rargs->first_, rargs->last_,
                                           rargs->version_);

    mark_point();

    std::vector<pthread_t> threads(rargs->n_receivers_);
    trx_thread_args trx_thd_args(receiver);
    for (size_t i(0); i < threads.size(); ++i)
    {
        log_info << "starting trx thread " << i;
        pthread_create(&threads[0] + i, 0, &trx_thread, &trx_thd_args);
    }

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
        return 3;
    }
    fail("unknown protocol version %i", protocol_version);
    return -1;
}


static void test_ist_common(int const version)
{
    using galera::KeyData;
    using galera::TrxHandle;
    using galera::TrxHandleWithStore;
    using galera::KeyOS;
    int const trx_version(select_trx_version(version));
    galera::TrxHandle::Params const trx_params("", trx_version,
                                               galera::KeySet::MAX_VERSION);
    gu::Config conf;
    galera::ReplicatorSMM::InitConfig(conf, NULL);
    std::string gcache_file("ist_check.cache");
    conf.set("gcache.name", gcache_file);
    std::string dir(".");
    std::string receiver_addr("tcp://127.0.0.1:0");
    wsrep_uuid_t uuid;
    gu_uuid_generate(reinterpret_cast<gu_uuid_t*>(&uuid), 0, 0);

    gcache::GCache* gcache = new gcache::GCache(conf, dir);

    mark_point();

    // populate gcache
    for (size_t i(1); i <= 10; ++i)
    {
        TrxHandle* trx((new TrxHandleWithStore(
                            trx_params, uuid, 1234+i, 5678+i))->handle());

        const wsrep_buf_t key[2] = {
            {"key1", 4},
            {"key2", 4}
        };

        trx->append_key(KeyData(trx_version, key, 2, WSREP_KEY_EXCLUSIVE,true));
        trx->append_data("bar", 3, WSREP_DATA_ORDERED, true);
        assert (i > 0);
        int last_seen(i - 1);
        int pa_range(i);

        gu::byte_t* ptr(0);

        if (trx_version < 3)
        {
            trx->set_last_seen_seqno(last_seen);
            size_t trx_size(trx->serial_size());
            ptr = static_cast<gu::byte_t*>(gcache->malloc(trx_size));
            trx->serialize(ptr, trx_size, 0);
        }
        else
        {
            galera::WriteSetNG::GatherVector bufs;
            ssize_t trx_size(trx->write_set_out().gather(trx->source_id(),
                                                         trx->conn_id(),
                                                         trx->trx_id(),
                                                         bufs));
            trx->set_last_seen_seqno(last_seen);
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
            assert (wsi.pa_range()  == 0);
            wsi.set_seqno(i, pa_range);
            assert (wsi.seqno()     == int64_t(i));
            assert (wsi.pa_range()  == pa_range);
        }

        gcache->seqno_assign(ptr, i, i - pa_range);
        trx->unref();
    }

    mark_point();

    receiver_args rargs(receiver_addr, 1, 10, 1, version);
    sender_args sargs(*gcache, rargs.listen_addr_, 1, 10, version);

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

    delete gcache;

    mark_point();
    unlink(gcache_file.c_str());
}


START_TEST(test_ist_v1)
{
    test_ist_common(1);
}
END_TEST


START_TEST(test_ist_v2)
{
    test_ist_common(2);
}
END_TEST


START_TEST(test_ist_v3)
{
    test_ist_common(3);
}
END_TEST


START_TEST(test_ist_v4)
{
    test_ist_common(4);
}
END_TEST

START_TEST(test_ist_v5)
{
    test_ist_common(5);
}
END_TEST

Suite* ist_suite()
{
    Suite* s  = suite_create("ist");
    TCase* tc;

    tc = tcase_create("test_ist_message");
    tcase_add_test(tc, test_ist_message);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_ist_v1");
    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, test_ist_v1);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_ist_v2");
    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, test_ist_v2);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_ist_v3");
    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, test_ist_v3);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_ist_v4");
    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, test_ist_v4);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_ist_v5");
    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, test_ist_v5);
    suite_add_tcase(s, tc);

    return s;
}
