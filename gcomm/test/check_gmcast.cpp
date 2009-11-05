
#include "check_gcomm.hpp"
#include "gcomm/protostack.hpp"

#include "../src/gmcast.cpp"

using namespace gcomm;
using namespace gu::datetime;

#include <check.h>

namespace gcomm
{
    static bool operator==(const GMCastNode& a, const GMCastNode& b)
{
    return a.is_operational() == b.is_operational() &&
        a.get_uuid() == b.get_uuid() && a.get_address() == b.get_address();
}


static bool operator==(const gcomm::GMCastMessage& a, const gcomm::GMCastMessage& b)
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
}



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
        log_info << hdr.serial_size();
        size_t ret = hdr.serialize(buf, hdr.serial_size(), 0);
        log_info << ret;
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
    log_info << "START";
    Protonet pnet;
    
    Transport* tp1 = Transport::create(
        pnet, 
        "gmcast://?gmcast.listen_addr=tcp://127.0.0.1:10001&gmcast.group=testgrp");
    
    
    pnet.insert(&tp1->get_pstack());
    tp1->connect();
    
    pnet.event_loop(Sec/4);
    tp1->close();
    
    Transport* tp2 = Transport::create(pnet, "gmcast://127.0.0.1:10001?gmcast.group=testgrp&gmcast.listen_addr=tcp://127.0.0.1:10002");
    pnet.insert(&tp2->get_pstack());
    tp1->connect();
    tp2->connect();
    
    pnet.event_loop(Sec/4);
    tp1->close();
    
    pnet.event_loop(Sec/4);
    tp2->close();
    
    
    Transport* tp3 = Transport::create(pnet, "gmcast://127.0.0.1:10002?gmcast.group=testgrp&gmcast.listen_addr=tcp://127.0.0.1:10003");
    pnet.insert(&tp3->get_pstack());
    
    tp1->connect();
    tp2->connect();
    pnet.event_loop(Sec/4);
    
    tp3->connect();
    pnet.event_loop(Sec/4);
    
    pnet.erase(&tp3->get_pstack());
    pnet.erase(&tp2->get_pstack());
    pnet.erase(&tp1->get_pstack());

    tp3->close();
    tp2->close();
    tp1->close();
    
    pnet.event_loop(Sec/4);
    
    delete tp3;
    delete tp2;
    delete tp1;
    
    pnet.event_loop(0);
    
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
        
        void handle_up(int cid, const Datagram& rb,
                       const ProtoUpMeta& um)
        {
            if (rb.get_len() < rb.get_offset() + 16)
            {
                throw FatalException("offset error");
            }
            char buf[16];
            memset(buf, 'a', sizeof(buf));
            if (memcmp(buf, &rb.get_payload()[0] + rb.get_offset(), 16) != 0)
            {
                throw FatalException("content mismatch");
            }
            recvd++;
        }
        
        size_t get_recvd() const
        {
            return recvd;
        }
        
        // Transport* get_tp() const { return tp; }
        Protostack& get_pstack() { return pstack; }
    };
    
    log_info << "START";
    Protonet pnet;

    const char* addr1 = "127.0.0.1:20001";
    const char* addr2 = "127.0.0.1:20002";
    const char* addr3 = "127.0.0.1:20003";
    const char* addr4 = "127.0.0.1:20004";

    
    User u1(pnet, addr1, 0);
    pnet.insert(&u1.get_pstack());
    
    log_info << "u1 start";
    u1.start();

    
    pnet.event_loop(Sec/10);
    
    fail_unless(u1.get_recvd() == 0);
    
    log_info << "u2 start";
    User u2(pnet, addr2, addr1);
    pnet.insert(&u2.get_pstack());
    
    u2.start();
    
    while (u1.get_recvd() <= 50 || u2.get_recvd() <= 50)
    {
        u1.handle_timer();
        u2.handle_timer();
        pnet.event_loop(Sec/10);
    }
    
    log_info << "u3 start";
    User u3(pnet, addr3, addr2);
    pnet.insert(&u3.get_pstack());
    u3.start();
    
    while (u3.get_recvd() <= 50)
    {
        u1.handle_timer();
        u2.handle_timer();
        pnet.event_loop(Sec/10);
    }
    
    log_info << "u4 start";
    User u4(pnet, addr4, addr2);
    pnet.insert(&u4.get_pstack());
    u4.start();
    
    while (u4.get_recvd() <= 50)
    {
        u1.handle_timer();
        u2.handle_timer();
        pnet.event_loop(Sec/10);
    }
    
    log_info << "u1 stop";
    u1.stop();
    pnet.erase(&u1.get_pstack());

    pnet.event_loop(3*Sec);
    
    log_info << "u1 start";
    pnet.insert(&u1.get_pstack());
    u1.start();
    
    pnet.event_loop(3*Sec);
    
    fail_unless(u1.get_recvd() != 0);
    fail_unless(u2.get_recvd() != 0);
    fail_unless(u3.get_recvd() != 0);
    fail_unless(u4.get_recvd() != 0);
    
    pnet.erase(&u4.get_pstack());
    pnet.erase(&u3.get_pstack());
    pnet.erase(&u2.get_pstack());
    pnet.erase(&u1.get_pstack());
    
    u1.stop();
    u2.stop();
    u3.stop();
    u4.stop();
    
    pnet.event_loop(0);
    
}
END_TEST

START_TEST(test_gmcast_auto_addr)
{
    log_info << "START";
    Protonet pnet;
    Transport* tp1 = Transport::create(pnet, "gmcast://?gmcast.group=test");
    Transport* tp2 = Transport::create(pnet, "gmcast://localhost:4567?gmcast.group=test&gmcast.listen_addr=tcp://127.0.0.1:10002");
    
    pnet.insert(&tp1->get_pstack());
    pnet.insert(&tp2->get_pstack());
    
    tp1->connect();
    tp2->connect();
    
    pnet.event_loop(Sec);

    pnet.erase(&tp2->get_pstack());
    pnet.erase(&tp1->get_pstack());

    tp1->close();
    tp2->close();
    
    delete tp1;
    delete tp2;
    
    pnet.event_loop(0);
    
}
END_TEST



START_TEST(test_gmcast_forget)
{
    log_info << "START";
    Protonet pnet;
    Transport* tp1 = Transport::create(pnet, "gmcast://?gmcast.group=test");
    Transport* tp2 = Transport::create(pnet, "gmcast://127.0.0.1:4567?gmcast.group=test&gmcast.listen_addr=tcp://127.0.0.1:10002");
    Transport* tp3 = Transport::create(pnet, "gmcast://127.0.0.1:4567?gmcast.group=test&gmcast.listen_addr=tcp://127.0.0.1:10003");
    

    pnet.insert(&tp1->get_pstack());
    pnet.insert(&tp2->get_pstack());
    pnet.insert(&tp3->get_pstack());

    tp1->connect();
    tp2->connect();
    tp3->connect();

    
    pnet.event_loop(Sec);
    
    UUID uuid1 = tp1->get_uuid();
    
    tp1->close();
    tp2->close(uuid1);
    tp3->close(uuid1);
    pnet.event_loop(10*Sec);
    tp1->connect();
    // @todo Implement this using User class above and verify that 
    // tp2 and tp3 communicate with each other but now with tp1
    log_info << "####";
    pnet.event_loop(Sec);

    pnet.erase(&tp3->get_pstack());
    pnet.erase(&tp2->get_pstack());
    pnet.erase(&tp1->get_pstack());
    
    tp1->close();
    tp2->close();
    tp3->close();
    delete tp1;
    delete tp2;
    delete tp3;
    
    pnet.event_loop(0);
    
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
