
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
        a.get_type() == b.get_type() &&
        a.get_ttl() == b.get_ttl() &&
        a.get_flags() == b.get_flags();

    if (ret == true && a.get_flags() & GMCastMessage::F_NODE_ADDRESS)
    {
        ret = a.get_node_address() == b.get_node_address();
    }

    if (ret == true && a.get_flags() & GMCastMessage::F_GROUP_NAME)
    {
        const char* a_grp = a.get_group_name();
        const char* b_grp = b.get_group_name();
        fail_unless(!!a_grp && !!b_grp);
        ret = ret && strcmp(a_grp, b_grp) == 0;
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

END_GCOMM_NAMESPACE

START_TEST(test_gmcast_messages)
{
    /* */
    {
        GMCastMessage hdr(GMCastMessage::P_HANDSHAKE, UUID());
        byte_t* buf = new byte_t[hdr.size()];
        fail_unless(hdr.write(buf, hdr.size(), 0) == hdr.size());
        GMCastMessage hdr2;
        fail_unless(hdr2.read(buf, hdr.size(), 0) == hdr.size());
        fail_unless(hdr == hdr2);
        delete[] buf;
    }

    /* */
    {
        GMCastMessage hdr(GMCastMessage::P_HANDSHAKE_OK, UUID());
        byte_t* buf = new byte_t[hdr.size()];
        fail_unless(hdr.write(buf, hdr.size(), 0) == hdr.size());
        GMCastMessage hdr2;
        fail_unless(hdr2.read(buf, hdr.size(), 0) == hdr.size());
        fail_unless(hdr == hdr2);
        delete[] buf;

    }

    /* */
    {
        GMCastMessage hdr(GMCastMessage::P_HANDSHAKE_FAIL, UUID());
        byte_t* buf = new byte_t[hdr.size()];
        fail_unless(hdr.write(buf, hdr.size(), 0) == hdr.size());
        GMCastMessage hdr2;
        fail_unless(hdr2.read(buf, hdr.size(), 0) == hdr.size());
        fail_unless(hdr == hdr2);
        delete[] buf;
    }
    /* */
    {
        GMCastMessage hdr(GMCastMessage::P_HANDSHAKE_RESPONSE,
                          UUID(),
                          "gcomm+tcp://127.0.0.1:2112",
                          "test_group");
        byte_t* buf = new byte_t[hdr.size()];
        fail_unless(hdr.write(buf, hdr.size(), 0) == hdr.size());
        GMCastMessage hdr2;
        fail_unless(hdr2.read(buf, hdr.size(), 0) == hdr.size());
        fail_unless(hdr == hdr2);
        delete[] buf;
    }

    /* */
    {
        GMCastMessage hdr(GMCastMessage::P_USER_BASE, UUID(), 4);
        byte_t* buf = new byte_t[hdr.size()];
        fail_unless(hdr.write(buf, hdr.size(), 0) == hdr.size());
        GMCastMessage hdr2;
        fail_unless(hdr2.read(buf, hdr.size(), 0) == hdr.size());
        fail_unless(hdr == hdr2);
        delete[] buf;
    }

    /* */
    {
        std::list<GMCastNode> node_list;
        node_list.push_back(GMCastNode(true, UUID(0, 0), "gcomm+tcp://127.0.0.1:10001"));
        node_list.push_back(GMCastNode(false, UUID(0, 0), "gcomm+tcp://127.0.0.1:10002"));
        node_list.push_back(GMCastNode(true, UUID(0, 0), "gcomm+tcp://127.0.0.1:10003"));

        GMCastMessage hdr(GMCastMessage::P_TOPOLOGY_CHANGE, UUID(0, 0),
                         "foobar", node_list);
        byte_t* buf = new byte_t[hdr.size()];
        fail_unless(hdr.write(buf, hdr.size(), 0) == hdr.size());
        GMCastMessage hdr2;
        fail_unless(hdr2.read(buf, hdr.size(), 0) == hdr.size());
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
    
    for (int i = 0; i < 15; ++i)
    {
        el.poll(200);
    }

    tp1->close();

    Transport* tp2 = Transport::create("gcomm+gmcast://127.0.0.1:10001?gmcast.group=testgrp&gmcast.listen_addr=gcomm+tcp://127.0.0.1:10002", &el);
    
    tp1->connect();
    tp2->connect();


    for (int i = 0; i < 15; ++i)
    {
        el.poll(200);
    }

    tp1->close();

    for (int i = 0; i < 15; ++i)
    {
        el.poll(200);
    }
    tp2->close();


    Transport* tp3 = Transport::create(URI("gcomm+gmcast://127.0.0.1:10002?gmcast.group=testgrp&gmcast.listen_addr=gcomm+tcp://127.0.0.1:10003"), &el);

    tp1->connect();
    tp2->connect();

    for (int i = 0; i < 15; ++i)
    {
        el.poll(200);
    }

    tp3->connect();

    for (int i = 0; i < 15; ++i)
    {
        el.poll(200);
    }

    tp3->close();
    tp2->close();
    tp1->close();


    for (int i = 0; i < 15; ++i)
    {
        el.poll(200);
    }

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
            LOG_DEBUG("msg " + make_int(roff).to_string());
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
    
    EventLoop el;
    User u1(&el, addr1, 0);
    u1.start();

    for (int i = 0; i < 10; ++i)
        el.poll(100);

    fail_unless(u1.get_recvd() == 0);

    User u2(&el, addr2, addr1);
    u2.start();

    for (int i = 0; i < 15; ++i)
        el.poll(100);

    fail_unless(u1.get_recvd() != 0);
    fail_unless(u2.get_recvd() != 0);

    User u3(&el, addr3, addr2);
    u3.start();

    for (int i = 0; i < 20; ++i)
        el.poll(100);

    User u4(&el, addr4, addr2);
    u4.start();
    for (int i = 0; i < 50; ++i)
        el.poll(100);

    u1.stop();
    for (int i = 0; i < 250; ++i)
        el.poll(100);

    u1.start();

    for (int i = 0; i < 250; ++i)
        el.poll(100);

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

    return s;

}
