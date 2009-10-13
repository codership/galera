
#include "check_gcomm.hpp"

#include "../src/gmcast.cpp"

using namespace gcomm;

#include <check.h>

BEGIN_GCOMM_NAMESPACE

static bool operator==(const GMCastNode& a, const GMCastNode& b)
{
    return a.is_operational() == b.is_operational() &&
        a.get_uuid() == b.get_uuid() && a.get_address() == b.get_address();
}

static bool operator==(const GMCastMessage& a, const GMCastMessage& b)
{
    bool ret = a.get_version() == b.get_version() &&
        a.get_type()  == b.get_type() &&
        a.get_ttl()   == b.get_ttl() &&
        a.get_flags() == b.get_flags();

    if (ret == true && a.get_flags() & GMCastMessage::F_NODE_ADDRESS)
    {
        ret = a.get_node_address() == b.get_node_address();
    }

    if (ret == true && a.get_flags() & GMCastMessage::F_GROUP_NAME)
    {
        const string& a_grp = a.get_group_name();
        const string& b_grp = b.get_group_name();
//        fail_unless(!!a_grp && !!b_grp);
        ret = ret && (a_grp == b_grp);
        // std::cerr << a_grp << "\n";
    }

    if (ret == true && a.get_flags() & GMCastMessage::F_NODE_LIST)
    {
        const std::list<GMCastNode>* alist = a.get_node_list();
        const std::list<GMCastNode>* blist = b.get_node_list();

        fail_unless(alist != 0 && blist != 0);
        ret = ret && *alist == *blist;

    }
    return ret;
}

static void event_loop(EventLoop* el, time_t secs, time_t msecs = 0)
{
    assert(msecs < 1000);
    Time stop = Time::now() + Time(secs, msecs*1000);
    do
    {
        el->poll(10);
    }
    while (stop >= Time::now());
}

END_GCOMM_NAMESPACE

START_TEST(test_gmcast_messages)
{
    /* */
    {
        GMCastMessage hdr(GMCastMessage::T_HANDSHAKE, UUID());
        byte_t* buf = new byte_t[hdr.serial_size()];
        fail_unless(hdr.serialize(buf, hdr.serial_size(), 0) == hdr.serial_size());
        GMCastMessage hdr2;
        fail_unless(hdr2.unserialize(buf, hdr.serial_size(), 0) == hdr.serial_size());
        fail_unless(hdr == hdr2);
        delete[] buf;
    }

    /* */
    {
        GMCastMessage hdr(GMCastMessage::T_HANDSHAKE_OK, UUID());
        byte_t* buf = new byte_t[hdr.serial_size()];
        fail_unless(hdr.serialize(buf, hdr.serial_size(), 0) == hdr.serial_size());
        GMCastMessage hdr2;
        fail_unless(hdr2.unserialize(buf, hdr.serial_size(), 0) == hdr.serial_size());
        fail_unless(hdr == hdr2);
        delete[] buf;

    }

    /* */
    {
        GMCastMessage hdr(GMCastMessage::T_HANDSHAKE_FAIL, UUID());
        byte_t* buf = new byte_t[hdr.serial_size()];
        fail_unless(hdr.serialize(buf, hdr.serial_size(), 0) == hdr.serial_size());
        GMCastMessage hdr2;
        fail_unless(hdr2.unserialize(buf, hdr.serial_size(), 0) == hdr.serial_size());
        fail_unless(hdr == hdr2);
        delete[] buf;
    }
    /* */
    {
        GMCastMessage hdr(GMCastMessage::T_HANDSHAKE_RESPONSE,
                          UUID(),
                          "gcomm+tcp://127.0.0.1:2112",
                          "test_group");
        byte_t* buf = new byte_t[hdr.serial_size()];
        fail_unless(hdr.serialize(buf, hdr.serial_size(), 0) == hdr.serial_size());
        GMCastMessage hdr2;
        fail_unless(hdr2.unserialize(buf, hdr.serial_size(), 0) == hdr.serial_size());
        fail_unless(hdr == hdr2);
        delete[] buf;
    }

    /* */
    {
        GMCastMessage hdr(GMCastMessage::T_USER_BASE, UUID(), 4);
        byte_t* buf = new byte_t[hdr.serial_size()];
        fail_unless(hdr.serialize(buf, hdr.serial_size(), 0) == hdr.serial_size());
        GMCastMessage hdr2;
        fail_unless(hdr2.unserialize(buf, hdr.serial_size(), 0) == hdr.serial_size());
        fail_unless(hdr == hdr2);
        delete[] buf;
    }

    /* */
    {
        std::list<GMCastNode> node_list;
        node_list.push_back(GMCastNode(true, UUID(0, 0), "gcomm+tcp://127.0.0.1:10001"));
        node_list.push_back(GMCastNode(false, UUID(0, 0), "gcomm+tcp://127.0.0.1:10002"));
        node_list.push_back(GMCastNode(true, UUID(0, 0), "gcomm+tcp://127.0.0.1:10003"));

        GMCastMessage hdr(GMCastMessage::T_TOPOLOGY_CHANGE, UUID(0, 0),
                         "foobar", node_list);
        byte_t* buf = new byte_t[hdr.serial_size()];
        fail_unless(hdr.serialize(buf, hdr.serial_size(), 0) == hdr.serial_size());
        GMCastMessage hdr2;
        fail_unless(hdr2.unserialize(buf, hdr.serial_size(), 0) == hdr.serial_size());
        fail_unless(hdr == hdr2);
        delete[] buf;
  }


}
END_TEST

START_TEST(test_gmcast)
{
    EventLoop el;


    Transport* tp1 = Transport::create("gcomm+gmcast://?gmcast.listen_addr=gcomm+tcp://127.0.0.1:10001&gmcast.group=testgrp", &el);
    
    tp1->connect();
    event_loop(&el, 0, 200);
    tp1->close();
    
    Transport* tp2 = Transport::create("gcomm+gmcast://127.0.0.1:10001?gmcast.group=testgrp&gmcast.listen_addr=gcomm+tcp://127.0.0.1:10002", &el);
    
    tp1->connect();
    tp2->connect();

    event_loop(&el, 0, 200);
    tp1->close();

    event_loop(&el, 0, 200);
    tp2->close();
    
    
    Transport* tp3 = Transport::create(URI("gcomm+gmcast://127.0.0.1:10002?gmcast.group=testgrp&gmcast.listen_addr=gcomm+tcp://127.0.0.1:10003"), &el);

    tp1->connect();
    tp2->connect();
    event_loop(&el, 0, 200);

    tp3->connect();
    event_loop(&el, 0, 200);
    
    tp3->close();
    tp2->close();
    tp1->close();

    event_loop(&el, 0, 200);

    delete tp3;
    delete tp2;
    delete tp1;

}
END_TEST

START_TEST(test_gmcast_w_user_messages)
{
    
    class User : public Toplay, EventContext
    {
        int fd;
        EventLoop* el;
        Transport* tp;
        size_t recvd;
        User(const User&);
        void operator=(User&);
    public:

        User(EventLoop* el_, const char* listen_addr, const char* remote_addr) :
            fd(PseudoFd::alloc_fd()),
            el(el_),
            tp(0),
            recvd(0)
        {
            string uri("gcomm+gmcast://");
            uri += remote_addr != 0 ? remote_addr : "";
            uri += "?";
            uri += "tcp.non_blocking=1";
            uri += "&";
            uri += "gmcast.group=testgrp";
            uri += "&gmcast.listen_addr=gcomm+tcp://";
            uri += listen_addr;

            tp = Transport::create(uri, el);
            set_down_context(tp);
            tp->set_up_context(this);
        }

        ~User()
        {
            PseudoFd::release_fd(fd);
            delete tp;
        }

        void start()
        {
            tp->connect();
            el->insert(fd, this);
            el->queue_event(fd, Event(Event::E_USER,
                                      Time::now() + Time(0, 5000)));
        }

        
        void stop()
        {
            tp->close();
            el->erase(fd);
        }

        void handle_event(const int cfd, const Event& e)
        {
            byte_t buf[16];
            memset(buf, 'a', sizeof(buf));

            WriteBuf wb(buf, sizeof(buf));

            pass_down(&wb, 0);

            el->queue_event(fd, Event(Event::E_USER,
                                      Time::now() + Time(0, 5000)));
        }
        
        void handle_up(const int cid, const ReadBuf* rb, const size_t roff,
                       const ProtoUpMeta* um)
        {
            log_debug << "msg " << roff;
            if (rb->get_len() < roff + 16)
            {
                throw FatalException("offset error");
            }
            char buf[16];
            memset(buf, 'a', sizeof(buf));
            if (memcmp(buf, rb->get_buf(roff), 16) != 0)
            {
                throw FatalException("content mismatch");
            }
            recvd++;
        }

        size_t get_recvd() const
        {
            return recvd;
        }

    };
    
    const char* addr1 = "127.0.0.1:20001";
    const char* addr2 = "127.0.0.1:20002";
    const char* addr3 = "127.0.0.1:20003";
    const char* addr4 = "127.0.0.1:20004";

    // gu_log_max_level = GU_LOG_DEBUG;
    
    EventLoop el;
    User u1(&el, addr1, 0);

    log_info << "u1 start";
    u1.start();

    event_loop(&el, 0, 100);

    fail_unless(u1.get_recvd() == 0);
    
    log_info << "u2 start";
    User u2(&el, addr2, addr1);
    u2.start();
    
    while (u1.get_recvd() == 0 || u2.get_recvd() == 0)
        el.poll(100);
    

    log_info << "u3 start";
    User u3(&el, addr3, addr2);
    u3.start();

    event_loop(&el, 0, 200);
    
    log_info << "u4 start";
    User u4(&el, addr4, addr2);
    u4.start();

    event_loop(&el, 0, 200);

    log_info << "u1 stop";
    u1.stop();

    event_loop(&el, 3, 0);
    
    log_info << "u1 start";
    u1.start();
    
    event_loop(&el, 0, 200);

    fail_unless(u1.get_recvd() != 0);
    fail_unless(u2.get_recvd() != 0);
    fail_unless(u3.get_recvd() != 0);
    fail_unless(u4.get_recvd() != 0);
    
    u1.stop();
    u2.stop();
    u3.stop();
    u4.stop();
    

}
END_TEST

START_TEST(test_gmcast_auto_addr)
{
    EventLoop el;
    Transport* tp1 = Transport::create("gcomm+gmcast://?gmcast.group=test", &el);
    Transport* tp2 = Transport::create("gcomm+gmcast://localhost:4567?gmcast.group=test&gmcast.listen_addr=gcomm+tcp://127.0.0.1:10002", &el);

    tp1->connect();
    tp2->connect();
    
    for (int i = 0; i < 50; ++i)
    {
        el.poll(10);
    }
    tp1->close();
    tp2->close();

    delete tp1;
    delete tp2;

}
END_TEST



START_TEST(test_gmcast_forget)
{
    EventLoop el;
    Transport* tp1 = Transport::create("gcomm+gmcast://?gmcast.group=test", &el);
    Transport* tp2 = Transport::create("gcomm+gmcast://127.0.0.1:4567?gmcast.group=test&gmcast.listen_addr=gcomm+tcp://127.0.0.1:10002", &el);
    Transport* tp3 = Transport::create("gcomm+gmcast://127.0.0.1:4567?gmcast.group=test&gmcast.listen_addr=gcomm+tcp://127.0.0.1:10003", &el);

    tp1->connect();
    tp2->connect();
    tp3->connect();
    event_loop(&el, 1, 0);
    
    UUID uuid1 = tp1->get_uuid();
    
    tp1->close();
    tp2->close(uuid1);
    tp3->close(uuid1);
    event_loop(&el, 10, 0);
    tp1->connect();
    log_info << "####";
    event_loop(&el, 1, 0);
    
    tp1->close();
    tp2->close();
    tp3->close();
    delete tp1;
    delete tp2;
    delete tp3;
}
END_TEST

Suite* gmcast_suite()
{

    Suite* s = suite_create("gmcast");
    TCase* tc;

    tc = tcase_create("test_gmcast_messages");
    tcase_add_test(tc, test_gmcast_messages);
    tcase_set_timeout(tc, 30);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_gmcast");
    tcase_add_test(tc, test_gmcast);
    tcase_set_timeout(tc, 30);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_gmcast_w_user_messages");
    tcase_add_test(tc, test_gmcast_w_user_messages);
    tcase_set_timeout(tc, 30);
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
