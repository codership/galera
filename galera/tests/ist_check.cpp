//
// Copyright (C) 2011-2020 Codership Oy <info@codership.com>
//


#include "ist.hpp"
#include "ist_proto.hpp"
#include "trx_handle.hpp"
#include "monitor.hpp"
#include "replicator_smm.hpp"

#include <GCache.hpp>
#include <gu_arch.h>
#include <check.h>

using namespace galera;

// Message tests

START_TEST(test_ist_message)
{

    using namespace galera::ist;

#if 0 /* This is a check for the old (broken) format */
    Message m3(3, Message::T_HANDSHAKE, 0x2, 3, 1001);

#if GU_WORDSIZE == 32
    ck_assert_msg(serial_size(m3) == 20, "serial size %zu != 20",
                  serial_size(m3));
#elif GU_WORDSIZE == 64
    ck_assert_msg(serial_size(m3) == 24, "serial size %zu != 24",
                  serial_size(m3));
#endif

    gu::Buffer buf(m3.serial_size());
    m3.serialize(&buf[0], buf.size(), 0);
    Message mu3(3);
    mu3.unserialize(&buf[0], buf.size(), 0);

    ck_assert(mu3.version() == 3);
    ck_assert(mu3.type()    == Message::T_HANDSHAKE);
    ck_assert(mu3.flags()   == 0x2);
    ck_assert(mu3.ctrl()    == 3);
    ck_assert(mu3.len()     == 1001);
#endif /* 0 */

    Message const m2(VER21, Message::T_HANDSHAKE, 0x2, 3, 1001);
    size_t const s2(12);
    ck_assert_msg(m2.serial_size() == s2,
                  "Expected m2.serial_size() = %zd, got %zd",
                  s2, m2.serial_size());

    gu::Buffer buf2(m2.serial_size());
    m2.serialize(&buf2[0], buf2.size(), 0);

    Message mu2(VER21);
    mu2.unserialize(&buf2[0], buf2.size(), 0);
    ck_assert(mu2.version() == VER21);
    ck_assert(mu2.type()    == Message::T_HANDSHAKE);
    ck_assert(mu2.flags()   == 0x2);
    ck_assert(mu2.ctrl()    == 3);
    ck_assert(mu2.len()     == 1001);

    Message const m4(VER40, Message::T_HANDSHAKE, 0x2, 3, 1001);
    size_t const s4(16 + sizeof(uint64_t /* Message::checksum_t */));
    ck_assert_msg(m4.serial_size() == s4,
                  "Expected m3.serial_size() = %zd, got %zd",
                  s4, m4.serial_size());

    gu::Buffer buf4(m4.serial_size());
    m4.serialize(&buf4[0], buf4.size(), 0);

    Message mu4(VER40);
    mu4.unserialize(&buf4[0], buf4.size(), 0);
    ck_assert(mu4.version() == VER40);
    ck_assert(mu4.type()    == Message::T_HANDSHAKE);
    ck_assert(mu4.flags()   == 0x2);
    ck_assert(mu4.ctrl()    == 3);
    ck_assert(mu4.len()     == 1001);
}
END_TEST

// IST tests

static gu_barrier_t start_barrier;

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
    TrxHandleSlave::Pool& trx_pool_;
    gcache::GCache& gcache_;
    int           version_;

    receiver_args(const std::string listen_addr,
                  wsrep_seqno_t first, wsrep_seqno_t last,
                  TrxHandleSlave::Pool& sp,
                  gcache::GCache& gc, int version)
        :
        listen_addr_(listen_addr),
        first_      (first),
        last_       (last),
        trx_pool_   (sp),
        gcache_     (gc),
        version_    (version)
    { }
};


extern "C" void* sender_thd(void* arg)
{
    mark_point();

    const sender_args* sargs(reinterpret_cast<const sender_args*>(arg));

    gu::Config conf;
    galera::ReplicatorSMM::InitConfig(conf, NULL, NULL);
    gu_barrier_wait(&start_barrier);
    sargs->gcache_.seqno_lock(sargs->first_); // unlocked in sender dtor
    galera::ist::Sender sender(conf, sargs->gcache_, sargs->peer_,
                               sargs->version_);
    mark_point();
    sender.send(sargs->first_, sargs->last_, sargs->first_);
    mark_point();
    return 0;
}


namespace
{
    class ISTHandler : public galera::ist::EventHandler
    {
    public:
        ISTHandler() :
            mutex_(),
            cond_(),
            seqno_(0),
            eof_(false),
            error_(0)
        { }

        ~ISTHandler() {}

        void ist_trx(const TrxHandleSlavePtr& ts, bool must_apply, bool preload)
        {
            assert(ts != 0);
            ts->verify_checksum();

            if (ts->state() == TrxHandle::S_ABORTING)
            {
                log_info << "ist_trx: aborting: " << ts->global_seqno();
            }
            else
            {
                log_info << "ist_trx: " << *ts;
                ts->set_state(TrxHandle::S_CERTIFYING);
            }

            if (preload == false)
            {
                assert(seqno_ + 1 == ts->global_seqno());
            }
            else
            {
                assert(seqno_ < ts->global_seqno());
            }
            seqno_ = ts->global_seqno();
        }

        void ist_cc(const gcs_action& act, bool must_apply, bool preload)
        {
            gcs_act_cchange const cc(act.buf, act.size);
            assert(act.seqno_g == cc.seqno);

            log_info << "ist_cc" << cc.seqno;
            if (preload == false)
            {
                assert(seqno_ + 1 == cc.seqno);
            }
            else
            {
                assert(seqno_ < cc.seqno);
            }
        }

        void ist_end(int error)
        {
            log_info << "IST ended with status: " << error;
            gu::Lock lock(mutex_);
            error_ = error;
            eof_ = true;
            cond_.signal();
        }

        int wait()
        {
            gu::Lock lock(mutex_);
            while (eof_ == false)
            {
                lock.wait(cond_);
            }
            return error_;
        }

    private:
        gu::Mutex mutex_;
        gu::Cond  cond_;
        wsrep_seqno_t seqno_;
        bool eof_;
        int error_;
    };
}

extern "C" void* receiver_thd(void* arg)
{
    mark_point();

    receiver_args* rargs(reinterpret_cast<receiver_args*>(arg));

    gu::Config conf;
    TrxHandleSlave::Pool slave_pool(sizeof(TrxHandleSlave), 1024,
                                    "TrxHandleSlave");
    galera::ReplicatorSMM::InitConfig(conf, NULL, NULL);

    mark_point();

    conf.set(galera::ist::Receiver::RECV_ADDR, rargs->listen_addr_);
    ISTHandler isth;
    galera::ist::Receiver receiver(conf, rargs->gcache_, slave_pool,
                                   isth, 0);

    // Prepare starts IST receiver thread
    rargs->listen_addr_ = receiver.prepare(rargs->first_, rargs->last_,
                                           rargs->version_,
                                           WSREP_UUID_UNDEFINED);

    gu_barrier_wait(&start_barrier);
    mark_point();

    receiver.ready(rargs->first_);

    log_info << "IST wait finished with status: " << isth.wait();

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
    case 8:
        return 3;
    case 9:
        return 4;
    case 10:
        return 5;
    default:
        ck_abort_msg("unsupported replicator protocol version: %d",
                     protocol_version);
    }

    return -1;
}

static void store_trx(gcache::GCache* const gcache,
                      TrxHandleMaster::Pool& lp,
                      const TrxHandleMaster::Params& trx_params,
                      const wsrep_uuid_t& uuid,
                      int const i)
{
    TrxHandleMasterPtr trx(TrxHandleMaster::New(lp, trx_params, uuid, 1234+i,
                                                5678+i),
                           TrxHandleMasterDeleter());

    const wsrep_buf_t key[3] = {
        {"key1", 4},
        {"key2", 4},
        {"key3", 4}
    };

    trx->append_key(KeyData(trx_params.version_, key, 3, WSREP_KEY_EXCLUSIVE,
                            true));
    trx->append_data("bar", 3, WSREP_DATA_ORDERED, true);
    assert (i > 0);
    int last_seen(i - 1);
    int pa_range(i);

    gu::byte_t* ptr(0);

    if (trx_params.version_ < 3)
    {
        ck_abort_msg("WS version %d not supported any more",
                     trx_params.version_);
    }
    else
    {
        galera::WriteSetNG::GatherVector bufs;
        ssize_t trx_size(trx->gather(bufs));
        mark_point();
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
        mark_point();
        galera::WriteSetIn wsi(ws_buf);
        assert (wsi.last_seen() == last_seen);
        assert (wsi.pa_range()  == (wsi.version() < WriteSetNG::VER5 ?
                                    0 : WriteSetNG::MAX_PA_RANGE));
        wsi.set_seqno(i, pa_range);
        assert (wsi.seqno()     == int64_t(i));
        assert (wsi.pa_range()  == pa_range);
    }

    gcache->seqno_assign(ptr, i, GCS_ACT_WRITESET, (i - pa_range) <= 0);
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

    ck_assert(NULL != cc_ptr);
    memcpy(cc_ptr, tmp, cc_size);
    free(tmp);
    gcache->seqno_assign(cc_ptr, i, GCS_ACT_CCHANGE, i > 0);
}

static void test_ist_common(int const version)
{
    using galera::KeyData;
    using galera::TrxHandle;
    using galera::KeyOS;

    TrxHandleMaster::Pool lp(TrxHandleMaster::LOCAL_STORAGE_SIZE(), 4,
                             "ist_common");
    TrxHandleSlave::Pool sp(sizeof(TrxHandleSlave), 4, "ist_common");

    int const trx_version(select_trx_version(version));
    TrxHandleMaster::Params const trx_params("", trx_version,
                                       galera::KeySet::MAX_VERSION);
    std::string const dir(".");

    gu::Config conf_sender;
    galera::ReplicatorSMM::InitConfig(conf_sender, NULL, NULL);
    std::string const gcache_sender_file("ist_sender.cache");
    conf_sender.set("gcache.name", gcache_sender_file);
    conf_sender.set("gcache.size", "1M");
    gcache::GCache* gcache_sender = new gcache::GCache(conf_sender, dir);

    gu::Config conf_receiver;
    galera::ReplicatorSMM::InitConfig(conf_receiver, NULL, NULL);
    std::string const gcache_receiver_file("ist_receiver.cache");
    conf_receiver.set("gcache.name", gcache_receiver_file);
    conf_receiver.set("gcache.size", "1M");
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

    receiver_args rargs(receiver_addr, 1, 10, sp, *gcache_receiver, version);
    sender_args sargs(*gcache_sender, rargs.listen_addr_, 1, 10, version);

    gu_barrier_init(&start_barrier, 0, 2);

    gu_thread_t sender_thread, receiver_thread;

    gu_thread_create(&sender_thread, 0, &sender_thd, &sargs);
    mark_point();
    usleep(100000);
    gu_thread_create(&receiver_thread, 0, &receiver_thd, &rargs);
    mark_point();

    gu_thread_join(sender_thread, 0);
    gu_thread_join(receiver_thread, 0);

    mark_point();

    delete gcache_sender;
    delete gcache_receiver;

    mark_point();
    unlink(gcache_sender_file.c_str());
    unlink(gcache_receiver_file.c_str());
}

START_TEST(test_ist_v7)
{
    test_ist_common(7);      /* trx ver: 3, STR ver: 2, alignment: none */
}
END_TEST

START_TEST(test_ist_v8)
{
    test_ist_common(8);      /* trx ver: 3, STR ver: 2, alignment: 8    */
}
END_TEST

START_TEST(test_ist_v9)
{
    test_ist_common(9);      /* trx ver: 4, STR ver: 2, alignment: 8    */
}
END_TEST

START_TEST(test_ist_v10)
{
    test_ist_common(10);     /* trx ver: 5, STR ver: 3, alignment: 8    */
}
END_TEST

Suite* ist_suite()
{
    Suite* s  = suite_create("ist");
    TCase* tc;

    tc = tcase_create("test_ist_message");
    tcase_add_test(tc, test_ist_message);
    suite_add_tcase(s, tc);
    tc = tcase_create("test_ist_v7");
    tcase_set_timeout(tc, 60);
    suite_add_tcase(s, tc);
    tcase_add_test(tc, test_ist_v7);
    tc = tcase_create("test_ist_v8");
    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, test_ist_v8);
    suite_add_tcase(s, tc);
    tc = tcase_create("test_ist_v9");
    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, test_ist_v9);
    suite_add_tcase(s, tc);
    tc = tcase_create("test_ist_v10");
    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, test_ist_v10);
    suite_add_tcase(s, tc);

    return s;
}
