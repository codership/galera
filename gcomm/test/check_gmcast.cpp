/*
 * Copyright (C) 2009-2020 Codership Oy <info@codership.com>
 */

#include "check_gcomm.hpp"
#include "gcomm/protostack.hpp"
#include "gcomm/conf.hpp"

#include "gmcast.hpp"
#include "gmcast_message.hpp"

#include "gu_asio.hpp" // gu::ssl_register_params()

using namespace std;
using namespace gcomm;
using namespace gcomm::gmcast;
using namespace gu::datetime;
using gu::byte_t;
using gu::Buffer;

#include <check.h>

// Note: Multicast test(s) not run by default.
static bool test_multicast(false);
string mcast_param("gmcast.mcast_addr=239.192.0.11&gmcast.mcast_port=4567");


START_TEST(test_gmcast_multicast)
{

    string uri1("gmcast://?gmcast.group=test&gmcast.mcast_addr=239.192.0.11");
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    unique_ptr<Protonet> pnet(Protonet::create(conf));
    Transport* gm1(Transport::create(*pnet, uri1));

    gm1->connect();
    gm1->close();

    delete gm1;
}
END_TEST


START_TEST(test_gmcast_w_user_messages)
{
    class User : public Toplay
    {
        Transport* tp_;
        size_t recvd_;
        Protostack pstack_;
        explicit User(const User&);
        void operator=(User&);

    public:

        User(Protonet& pnet,
             const std::string& listen_addr,
             const std::string& remote_addr) :
            Toplay(pnet.conf()),
            tp_(0),
            recvd_(0),
            pstack_()
        {
            string uri("gmcast://");
            uri += remote_addr; // != 0 ? remote_addr : "";
            uri += "?";
            uri += "tcp.non_blocking=1";
            uri += "&";
            uri += "gmcast.group=testgrp";
            uri += "&gmcast.time_wait=PT0.5S";
            if (test_multicast == true)
            {
                uri += "&" + mcast_param;
            }
            uri += "&gmcast.listen_addr=tcp://";
            uri += listen_addr;

            tp_ = Transport::create(pnet, uri);
        }

        ~User()
        {
            delete tp_;
        }

        void start(const std::string& peer = "")
        {
            if (peer == "")
            {
                tp_->connect();
            }
            else
            {
                tp_->connect(peer);
            }
            pstack_.push_proto(tp_);
            pstack_.push_proto(this);
        }


        void stop()
        {
            pstack_.pop_proto(this);
            pstack_.pop_proto(tp_);
            tp_->close();
        }

        void handle_timer()
        {
            byte_t buf[16];
            memset(buf, 0xa5, sizeof(buf));

            Datagram dg(Buffer(buf, buf + sizeof(buf)));

            send_down(dg, ProtoDownMeta());
        }

        void handle_up(const void* cid, const Datagram& rb,
                       const ProtoUpMeta& um)
        {
            if (rb.len() < rb.offset() + 16)
            {
                gu_throw_fatal << "offset error";
            }
            char buf[16];
            memset(buf, 0xa5, sizeof(buf));
            // cppcheck-suppress uninitstring
            if (memcmp(buf, &rb.payload()[0] + rb.offset(), 16) != 0)
            {
                gu_throw_fatal << "content mismatch";
            }
            recvd_++;
        }

        size_t recvd() const
        {
            return recvd_;
        }

        void set_recvd(size_t val)
        {
            recvd_ = val;
        }

        Protostack& pstack() { return pstack_; }

        std::string listen_addr() const
        {
            return tp_->listen_addr();
        }
    };

    log_info << "START";
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    mark_point();
    unique_ptr<Protonet> pnet(Protonet::create(conf));
    mark_point();
    User u1(*pnet, "127.0.0.1:0", "");
    pnet->insert(&u1.pstack());

    log_info << "u1 start";
    u1.start();

    pnet->event_loop(Sec/10);

    ck_assert(u1.recvd() == 0);

    log_info << "u2 start";
    User u2(*pnet, "127.0.0.1:0",
            u1.listen_addr().erase(0, strlen("tcp://")));
    pnet->insert(&u2.pstack());

    u2.start();

    while (u1.recvd() <= 50 || u2.recvd() <= 50)
    {
        u1.handle_timer();
        u2.handle_timer();
        pnet->event_loop(Sec/10);
    }

    log_info << "u3 start";
    User u3(*pnet, "127.0.0.1:0",
            u2.listen_addr().erase(0, strlen("tcp://")));
    pnet->insert(&u3.pstack());
    u3.start();

    while (u3.recvd() <= 50)
    {
        u1.handle_timer();
        u2.handle_timer();
        pnet->event_loop(Sec/10);
    }

    log_info << "u4 start";
    User u4(*pnet, "127.0.0.1:0",
            u2.listen_addr().erase(0, strlen("tcp://")));
    pnet->insert(&u4.pstack());
    u4.start();

    while (u4.recvd() <= 50)
    {
        u1.handle_timer();
        u2.handle_timer();
        pnet->event_loop(Sec/10);
    }

    log_info << "u1 stop";
    u1.stop();
    pnet->erase(&u1.pstack());

    pnet->event_loop(3*Sec);

    log_info << "u1 start";
    pnet->insert(&u1.pstack());
    u1.start(u2.listen_addr());

    u1.set_recvd(0);
    u2.set_recvd(0);
    u3.set_recvd(0);
    u4.set_recvd(0);

    for (size_t i(0); i < 30; ++i)
    {
        u1.handle_timer();
        u2.handle_timer();
        pnet->event_loop(Sec/10);
    }

    ck_assert(u1.recvd() != 0);
    ck_assert(u2.recvd() != 0);
    ck_assert(u3.recvd() != 0);
    ck_assert(u4.recvd() != 0);

    pnet->erase(&u4.pstack());
    pnet->erase(&u3.pstack());
    pnet->erase(&u2.pstack());
    pnet->erase(&u1.pstack());

    u1.stop();
    u2.stop();
    u3.stop();
    u4.stop();

    pnet->event_loop(0);

}
END_TEST


// not run by default, hard coded port
START_TEST(test_gmcast_auto_addr)
{
    log_info << "START";
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    unique_ptr<Protonet> pnet(Protonet::create(conf));
    Transport* tp1 = Transport::create(*pnet, "gmcast://?gmcast.group=test");
    Transport* tp2 = Transport::create(*pnet, "gmcast://127.0.0.1:4567"
              "?gmcast.group=test&gmcast.listen_addr=tcp://127.0.0.1:10002");

    pnet->insert(&tp1->pstack());
    pnet->insert(&tp2->pstack());

    tp1->connect();
    tp2->connect();

    pnet->event_loop(Sec);

    pnet->erase(&tp2->pstack());
    pnet->erase(&tp1->pstack());

    tp1->close();
    tp2->close();

    delete tp1;
    delete tp2;

    pnet->event_loop(0);

}
END_TEST



START_TEST(test_gmcast_forget)
{
    gu_conf_self_tstamp_on();
    log_info << "START";
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    unique_ptr<Protonet> pnet(Protonet::create(conf));
    Transport* tp1 = Transport::create(*pnet, "gmcast://"
                    "?gmcast.group=test&gmcast.listen_addr=tcp://127.0.0.1:0");
    pnet->insert(&tp1->pstack());
    tp1->connect();

    Transport* tp2 = Transport::create(*pnet,
                                       std::string("gmcast://")
                                       + tp1->listen_addr().erase(
                                           0, strlen("tcp://"))
                  + "?gmcast.group=test&gmcast.listen_addr=tcp://127.0.0.1:0");
    Transport* tp3 = Transport::create(*pnet,
                                       std::string("gmcast://")
                                       + tp1->listen_addr().erase(
                                           0, strlen("tcp://"))
                  + "?gmcast.group=test&gmcast.listen_addr=tcp://127.0.0.1:0");


    pnet->insert(&tp2->pstack());
    pnet->insert(&tp3->pstack());

    tp2->connect();
    tp3->connect();

    pnet->event_loop(Sec);

    UUID uuid1 = tp1->uuid();

    tp1->close();
    tp2->close(uuid1);
    tp3->close(uuid1);
    pnet->event_loop(10*Sec);
    tp1->connect();
    // @todo Implement this using User class above and verify that
    // tp2 and tp3 communicate with each other but now with tp1
    log_info << "####";
    pnet->event_loop(Sec);

    pnet->erase(&tp3->pstack());
    pnet->erase(&tp2->pstack());
    pnet->erase(&tp1->pstack());

    tp1->close();
    tp2->close();
    tp3->close();
    delete tp1;
    delete tp2;
    delete tp3;

    pnet->event_loop(0);

}
END_TEST


// not run by default, hard coded port
START_TEST(test_trac_380)
{
    gu_conf_self_tstamp_on();
    log_info << "START (test_trac_380)";
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    std::unique_ptr<gcomm::Protonet> pnet(gcomm::Protonet::create(conf));

    // caused either assertion or exception
    gcomm::Transport* tp1(gcomm::Transport::create(
                              *pnet,
                              "gmcast://127.0.0.1:4567?"
                              "gmcast.group=test"));
    pnet->insert(&tp1->pstack());
    tp1->connect();
    try
    {
        pnet->event_loop(Sec);
    }
    catch (gu::Exception& e)
    {
        ck_assert_msg(e.get_errno() == EINVAL,
                      "unexpected errno: %d, cause %s",
                      e.get_errno(), e.what());
    }
    pnet->erase(&tp1->pstack());
    tp1->close();
    delete tp1;
    pnet->event_loop(0);
}
END_TEST


START_TEST(test_trac_828)
{
    gu_conf_self_tstamp_on();
    log_info << "START (test_trac_828)";
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    std::unique_ptr<gcomm::Protonet> pnet(gcomm::Protonet::create(conf));

    // If the bug is present, this will throw because of own address being
    // in address list.
    try
    {
        Transport* tp(gcomm::Transport::create(
                          *pnet,
                          "gmcast://127.0.0.1:4567?"
                          "gmcast.group=test&"
                          "gmcast.listen_addr=tcp://127.0.0.1:4567"));
        delete tp;
    }
    catch (gu::Exception& e)
    {
        ck_abort_msg("test_trac_828, expcetion thrown because of having own "
                     "address in address list");
    }
}
END_TEST

START_TEST(test_gmcast_ipv6)
{
    log_info << "START test_gmcast_ipv6";
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    conf.set("base_host", "ip6-localhost");
    gu_log_max_level = GU_LOG_DEBUG;
    std::unique_ptr<gcomm::Protonet> pnet(gcomm::Protonet::create(conf));

    // Without scheme
    {
        std::unique_ptr<Transport> tp(gcomm::Transport::create(
                                        *pnet,
                                        "gmcast://[::1]:4567?"
                                        "gmcast.group=test&"
                                        "gmcast.listen_addr=tcp://[::1]:4567"));
        tp->connect();
        tp->close();
    }

    {
        std::unique_ptr<Transport> tp(gcomm::Transport::create(
                                        *pnet,
                                        "gmcast://ip6-localhost:4567?"
                                        "gmcast.group=test&"
                                        "gmcast.listen_addr=tcp://ip6-localhost:4567"));
        tp->connect();
        tp->close();
    }

    {
        std::unique_ptr<Transport> tp(gcomm::Transport::create(
                                        *pnet,
                                        "gmcast://[::1]?"
                                        "gmcast.group=test&"
                                        "gmcast.listen_addr=tcp://[::1]"));
        tp->connect();
        tp->close();
    }

    {
        std::unique_ptr<Transport> tp(gcomm::Transport::create(
                                        *pnet,
                                        "gmcast://ip6-localhost?"
                                        "gmcast.group=test&"
                                        "gmcast.listen_addr=tcp://ip6-localhost"));
        tp->connect();
        tp->close();
    }
    {
        gcomm::Protolay::sync_param_cb_t spcb;
        std::unique_ptr<Transport> tp(gcomm::Transport::create(
                                        *pnet,
                                        "gmcast://ip6-localhost?"
                                        "gmcast.group=test&"
                                        "gmcast.listen_addr=tcp://[2001:db8:10:9464::233]:4567"));
        log_info << tp->configured_listen_addr();
        log_info << conf;
        ck_assert(tp->configured_listen_addr() == "tcp://[2001:db8:10:9464::233]:4567");
    }
    log_info << "END test_gmcast_ipv6";
}
END_TEST

Suite* gmcast_suite()
{

    Suite* s = suite_create("gmcast");
    TCase* tc;

    if (test_multicast == true)
    {
        tc = tcase_create("test_gmcast_multicast");
        tcase_add_test(tc, test_gmcast_multicast);
        suite_add_tcase(s, tc);
    }

    tc = tcase_create("test_gmcast_w_user_messages");
    tcase_add_test(tc, test_gmcast_w_user_messages);
    tcase_set_timeout(tc, 30);
    suite_add_tcase(s, tc);

    // not run by default, hard coded port
    tc = tcase_create("test_gmcast_auto_addr");
    tcase_add_test(tc, test_gmcast_auto_addr);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_gmcast_forget");
    tcase_add_test(tc, test_gmcast_forget);
    tcase_set_timeout(tc, 20);
    suite_add_tcase(s, tc);

    // not run by default, hard coded port
    tc = tcase_create("test_trac_380");
    tcase_add_test(tc, test_trac_380);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_trac_828");
    tcase_add_test(tc, test_trac_828);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_gmcast_ipv6");
    tcase_add_test(tc, test_gmcast_ipv6);
    suite_add_tcase(s, tc);

    return s;

}
