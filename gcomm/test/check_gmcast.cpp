/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "check_gcomm.hpp"
#include "gcomm/protostack.hpp"

#include "gmcast.hpp"
#include "gmcast_message.hpp"

using namespace std;
using namespace gcomm;
using namespace gcomm::gmcast;
using namespace gu;
using namespace gu::net;
using namespace gu::datetime;

#include <check.h>


static bool test_multicast(false);
string mcast_param("gmcast.mcast_addr=239.192.0.11&gmcast.mcast_port=4567");
string pnet_backend("gu");

START_TEST(test_gmcast_messages)
{

}
END_TEST


START_TEST(test_gmcast_multicast)
{

    string uri1("gmcast://?gmcast.group=test&gmcast.mcast_addr=239.192.0.11");
    auto_ptr<Protonet> pnet(Protonet::create(pnet_backend));
    Transport* gm1(Transport::create(*pnet, uri1));

    gm1->connect();
    gm1->close();

    delete gm1;
}
END_TEST


START_TEST(test_gmcast)
{
    log_info << "START";
    auto_ptr<Protonet> pnet(Protonet::create(pnet_backend));

    Transport* tp1 = Transport::create(
        *pnet,
        "gmcast://?gmcast.listen_addr=tcp://127.0.0.1:10001&gmcast.group=testgrp");


    pnet->insert(&tp1->get_pstack());
    tp1->connect();

    pnet->event_loop(Sec/4);
    tp1->close();

    Transport* tp2 = Transport::create(*pnet, "gmcast://127.0.0.1:10001?gmcast.group=testgrp&gmcast.listen_addr=tcp://127.0.0.1:10002");
    pnet->insert(&tp2->get_pstack());
    tp1->connect();
    tp2->connect();

    pnet->event_loop(Sec/4);
    tp1->close();

    pnet->event_loop(Sec/4);
    tp2->close();


    Transport* tp3 = Transport::create(*pnet, "gmcast://127.0.0.1:10002?gmcast.group=testgrp&gmcast.listen_addr=tcp://127.0.0.1:10003");
    pnet->insert(&tp3->get_pstack());

    tp1->connect();
    tp2->connect();
    pnet->event_loop(Sec/4);

    tp3->connect();
    pnet->event_loop(Sec/4);

    pnet->erase(&tp3->get_pstack());
    pnet->erase(&tp2->get_pstack());
    pnet->erase(&tp1->get_pstack());

    tp3->close();
    tp2->close();
    tp1->close();

    pnet->event_loop(Sec/4);

    delete tp3;
    delete tp2;
    delete tp1;

    pnet->event_loop(0);

}
END_TEST

START_TEST(test_gmcast_w_user_messages)
{

    class User : public Toplay
    {
        Transport* tp;
        size_t recvd;
        Protostack pstack;
        User(const User&);
        void operator=(User&);
    public:

        User(Protonet& pnet, const char* listen_addr, const char* remote_addr) :
            tp(0),
            recvd(0),
            pstack()
        {
            string uri("gmcast://");
            uri += remote_addr != 0 ? remote_addr : "";
            uri += "?";
            uri += "tcp.non_blocking=1";
            uri += "&";
            uri += "gmcast.group=testgrp";
            if (test_multicast == true)
            {
                uri += "&" + mcast_param;
            }
            uri += "&gmcast.listen_addr=tcp://";
            uri += listen_addr;

            tp = Transport::create(pnet, uri);
        }

        ~User()
        {
            delete tp;
        }

        void start()
        {
            tp->connect();
            pstack.push_proto(tp);
            pstack.push_proto(this);
        }


        void stop()
        {
            pstack.pop_proto(this);
            pstack.pop_proto(tp);
            tp->close();
        }

        void handle_timer()
        {
            byte_t buf[16];
            memset(buf, 'a', sizeof(buf));

            Datagram dg(Buffer(buf, buf + sizeof(buf)));

            send_down(dg, ProtoDownMeta());
        }

        void handle_up(const void* cid, const Datagram& rb,
                       const ProtoUpMeta& um)
        {
            if (rb.get_len() < rb.get_offset() + 16)
            {
                gu_throw_fatal << "offset error";
            }
            char buf[16];
            memset(buf, 'a', sizeof(buf));
            if (memcmp(buf, &rb.get_payload()[0] + rb.get_offset(), 16) != 0)
            {
                gu_throw_fatal << "content mismatch";
            }
            recvd++;
        }

        size_t get_recvd() const
        {
            return recvd;
        }

        Protostack& get_pstack() { return pstack; }
    };

    log_info << "START";
    auto_ptr<Protonet> pnet(Protonet::create(pnet_backend));

    const char* addr1 = "127.0.0.1:20001";
    const char* addr2 = "127.0.0.1:20002";
    const char* addr3 = "127.0.0.1:20003";
    const char* addr4 = "127.0.0.1:20004";


    User u1(*pnet, addr1, 0);
    pnet->insert(&u1.get_pstack());

    log_info << "u1 start";
    u1.start();


    pnet->event_loop(Sec/10);

    fail_unless(u1.get_recvd() == 0);

    log_info << "u2 start";
    User u2(*pnet, addr2, addr1);
    pnet->insert(&u2.get_pstack());

    u2.start();

    while (u1.get_recvd() <= 50 || u2.get_recvd() <= 50)
    {
        u1.handle_timer();
        u2.handle_timer();
        pnet->event_loop(Sec/10);
    }

    log_info << "u3 start";
    User u3(*pnet, addr3, addr2);
    pnet->insert(&u3.get_pstack());
    u3.start();

    while (u3.get_recvd() <= 50)
    {
        u1.handle_timer();
        u2.handle_timer();
        pnet->event_loop(Sec/10);
    }

    log_info << "u4 start";
    User u4(*pnet, addr4, addr2);
    pnet->insert(&u4.get_pstack());
    u4.start();

    while (u4.get_recvd() <= 50)
    {
        u1.handle_timer();
        u2.handle_timer();
        pnet->event_loop(Sec/10);
    }

    log_info << "u1 stop";
    u1.stop();
    pnet->erase(&u1.get_pstack());

    pnet->event_loop(3*Sec);

    log_info << "u1 start";
    pnet->insert(&u1.get_pstack());
    u1.start();

    pnet->event_loop(3*Sec);

    fail_unless(u1.get_recvd() != 0);
    fail_unless(u2.get_recvd() != 0);
    fail_unless(u3.get_recvd() != 0);
    fail_unless(u4.get_recvd() != 0);

    pnet->erase(&u4.get_pstack());
    pnet->erase(&u3.get_pstack());
    pnet->erase(&u2.get_pstack());
    pnet->erase(&u1.get_pstack());

    u1.stop();
    u2.stop();
    u3.stop();
    u4.stop();

    pnet->event_loop(0);

}
END_TEST

START_TEST(test_gmcast_auto_addr)
{
    log_info << "START";
    auto_ptr<Protonet> pnet(Protonet::create(pnet_backend));
    Transport* tp1 = Transport::create(*pnet, "gmcast://?gmcast.group=test");
    Transport* tp2 = Transport::create(*pnet, "gmcast://127.0.0.1:4567?gmcast.group=test&gmcast.listen_addr=tcp://127.0.0.1:10002");

    pnet->insert(&tp1->get_pstack());
    pnet->insert(&tp2->get_pstack());

    tp1->connect();
    tp2->connect();

    pnet->event_loop(Sec);

    pnet->erase(&tp2->get_pstack());
    pnet->erase(&tp1->get_pstack());

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

    auto_ptr<Protonet> pnet(Protonet::create(pnet_backend));
    Transport* tp1 = Transport::create(*pnet, "gmcast://?gmcast.group=test");
    Transport* tp2 = Transport::create(*pnet, "gmcast://127.0.0.1:4567?gmcast.group=test&gmcast.listen_addr=tcp://127.0.0.1:10002");
    Transport* tp3 = Transport::create(*pnet, "gmcast://127.0.0.1:4567?gmcast.group=test&gmcast.listen_addr=tcp://127.0.0.1:10003");

    pnet->insert(&tp1->get_pstack());
    pnet->insert(&tp2->get_pstack());
    pnet->insert(&tp3->get_pstack());

    tp1->connect();
    tp2->connect();
    tp3->connect();


    pnet->event_loop(Sec);

    UUID uuid1 = tp1->get_uuid();

    tp1->close();
    tp2->close(uuid1);
    tp3->close(uuid1);
    pnet->event_loop(10*Sec);
    tp1->connect();
    // @todo Implement this using User class above and verify that
    // tp2 and tp3 communicate with each other but now with tp1
    log_info << "####";
    pnet->event_loop(Sec);

    pnet->erase(&tp3->get_pstack());
    pnet->erase(&tp2->get_pstack());
    pnet->erase(&tp1->get_pstack());

    tp1->close();
    tp2->close();
    tp3->close();
    delete tp1;
    delete tp2;
    delete tp3;

    pnet->event_loop(0);

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

    tc = tcase_create("test_gmcast_messages");
    tcase_add_test(tc, test_gmcast_messages);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_gmcast");
    tcase_add_test(tc, test_gmcast);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_gmcast_w_user_messages");
    tcase_add_test(tc, test_gmcast_w_user_messages);
    tcase_set_timeout(tc, 20);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_gmcast_auto_addr");
    tcase_add_test(tc, test_gmcast_auto_addr);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_gmcast_forget");
    tcase_add_test(tc, test_gmcast_forget);
    tcase_set_timeout(tc, 20);
    suite_add_tcase(s, tc);

    return s;

}
