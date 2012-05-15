//
// Copyright (C) 2011 Codership Oy <info@codership.com>
//


#include "ist.hpp"
#include "ist_proto.hpp"
#include "trx_handle.hpp"
#include "uuid.hpp"
#include "monitor.hpp"
#include "GCache.hpp"
#include "gu_arch.h"

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

    gu::Buffer buf(serial_size(m3));
    serialize(m3, &buf[0], buf.size(), 0);
    Message mu3(3);
    unserialize(&buf[0], buf.size(), 0, mu3);

    fail_unless(mu3.version() == 3);
    fail_unless(mu3.type()    == Message::T_HANDSHAKE);
    fail_unless(mu3.flags()   == 0x2);
    fail_unless(mu3.ctrl()    == 3);
    fail_unless(mu3.len()     == 1001);

    Message m4(4, Message::T_HANDSHAKE, 0x2, 3, 1001);
    fail_unless(serial_size(m4) == 12);

    buf.clear();
    buf.resize(serial_size(m4));
    serialize(m4, &buf[0], buf.size(), 0);

    Message mu4(4);
    unserialize(&buf[0], buf.size(), 0, mu4);
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
    sender_args(gcache::GCache& gcache,
                const std::string& peer,
                wsrep_seqno_t first, wsrep_seqno_t last)
        :
        gcache_(gcache),
        peer_  (peer),
        first_ (first),
        last_  (last)
    { }
};


struct receiver_args
{
    std::string   listen_addr_;
    wsrep_seqno_t first_;
    wsrep_seqno_t last_;
    size_t        n_receivers_;
    receiver_args(const std::string listen_addr,
        wsrep_seqno_t first, wsrep_seqno_t last,
                  size_t n_receivers)
        :
        listen_addr_(listen_addr),
        first_(first),
        last_(last),
        n_receivers_(n_receivers)
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
    const sender_args* sargs(reinterpret_cast<const sender_args*>(arg));
    gu::Config conf;
    pthread_barrier_wait(&start_barrier);
    galera::ist::Sender sender(conf, sargs->gcache_, sargs->peer_, 1);
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

    receiver_args* rargs(reinterpret_cast<receiver_args*>(arg));

    gu::Config conf;
    conf.set(galera::ist::Receiver::RECV_ADDR, rargs->listen_addr_);
    galera::ist::Receiver receiver(conf, 0);
    rargs->listen_addr_ = receiver.prepare(rargs->first_, rargs->last_, 1);

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


static void test_ist_common(int version)
{
    using galera::TrxHandle;
    using galera::Key;
    gu::Config conf;
    std::string gcache_file("ist_check.cache");
    conf.set("gcache.name", gcache_file);
    std::string dir(".");
    std::string receiver_addr("tcp://127.0.0.1:0");
    wsrep_uuid_t uuid;
    gu_uuid_generate(reinterpret_cast<gu_uuid_t*>(&uuid), 0, 0);

    gcache::GCache* gcache = new gcache::GCache(conf, dir);

    // populate gcache
    for (size_t i(1); i <= 10; ++i)
    {
        TrxHandle* trx(new TrxHandle(0, uuid, 1234, 5678, false));
        const wsrep_key_part_t key[2] = {
            {"key1", 4},
            {"key2", 4}
        };
        trx->append_key(Key(0, key, 2, version));
        trx->append_data("bar", 3);

        size_t trx_size(serial_size(*trx));
        gu::byte_t* ptr(reinterpret_cast<gu::byte_t*>(gcache->malloc(trx_size)));
        serialize(*trx, ptr, trx_size, 0);
        gcache->seqno_assign(ptr, i, i - 1, false);
        trx->unref();
    }

    receiver_args rargs(receiver_addr, 1, 10, 1);
    sender_args sargs(*gcache, rargs.listen_addr_, 1, 10);

    pthread_barrier_init(&start_barrier, 0, 1 + 1 + rargs.n_receivers_);


    pthread_t sender_thread, receiver_thread;
    pthread_create(&sender_thread, 0, &sender_thd, &sargs);
    pthread_create(&receiver_thread, 0, &receiver_thd, &rargs);

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

    return s;
}
