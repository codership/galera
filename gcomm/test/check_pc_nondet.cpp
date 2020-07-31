/*
 * Copyright (C) 2019-2020 Codership Oy <info@codership.com>
 */

#include "check_gcomm.hpp"
#include "pc_message.hpp"
#include "pc_proto.hpp"
#include "evs_proto.hpp"
#include "gu_datetime.hpp"
#include "gcomm/transport.hpp"
#include "gu_asio.hpp"
#include <check.h>

using namespace gcomm;
using namespace gu;
using namespace gu::datetime;
using namespace std;
using namespace std::rel_ops;

class PCUser2 : public Toplay
{
    Transport* tp_;
    bool sending_;
    uint8_t my_type_;
    bool send_;
    Period send_period_;
    Date next_send_;
    PCUser2(const PCUser2&);
    void operator=(const PCUser2);
public:
    PCUser2(Protonet& net, const string& uri, const bool send = true) :
        Toplay(net.conf()),
        tp_(Transport::create(net, uri)),
        sending_(false),
        my_type_(static_cast<uint8_t>(1 + ::rand()%4)),
        send_(send),
        send_period_("PT0.05S"),
        next_send_(Date::max())
    { }

    ~PCUser2()
    {
        delete tp_;
    }

    void start()
    {
        gcomm::connect(tp_, this);
        tp_->connect();
        gcomm::disconnect(tp_, this);
        tp_->pstack().push_proto(this);
    }

    void stop()
    {
        sending_ = false;
        tp_->pstack().pop_proto(this);
        gcomm::connect(tp_, this);
        tp_->close();
        gcomm::disconnect(tp_, this);
    }

    void handle_up(const void* cid, const Datagram& rb, const ProtoUpMeta& um)
    {

        if (um.has_view())
        {
            const View& view(um.view());
            log_info << view;
            if (view.type() == V_PRIM && send_ == true)
            {
                sending_ = true;
                next_send_ = Date::monotonic() + send_period_;
            }
        }
        else
        {
            // log_debug << "received message: " << um.get_to_seq();
            ck_assert(rb.len() - rb.offset() == 16);
            if (um.source() == tp_->uuid())
            {
                ck_assert(um.user_type() == my_type_);
            }
        }
    }

    Protostack& pstack() { return tp_->pstack(); }

    Date handle_timers()
    {
        Date now(Date::monotonic());
        if (now >= next_send_)
        {
            byte_t buf[16];
            memset(buf, 0xa, sizeof(buf));
            Datagram dg(Buffer(buf, buf + sizeof(buf)));
            // dg.get_header().resize(128);
            // dg.set_header_offset(128);
            int ret = send_down(dg, ProtoDownMeta(my_type_, rand() % 10 == 0 ? O_SAFE : O_LOCAL_CAUSAL));
            if (ret != 0)
            {
                // log_debug << "send down " << ret;
            }
            next_send_ = next_send_ + send_period_;
        }
        return next_send_;
    }

    std::string listen_addr() const
    {
        return tp_->listen_addr();
    }

};

START_TEST(test_pc_transport)
{
    log_info << "START (test_pc_transport)";
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    auto_ptr<Protonet> net(Protonet::create(conf));
    PCUser2 pu1(*net,
                "pc://?"
                "evs.info_log_mask=0xff&"
                "gmcast.listen_addr=tcp://127.0.0.1:0&"
                "gmcast.group=pc&"
                "gmcast.time_wait=PT0.5S&"
                "pc.recovery=0&"
                "node.name=n1");

    gu_conf_self_tstamp_on();

    pu1.start();
    net->event_loop(5*Sec);

    PCUser2 pu2(*net,
                std::string("pc://")
                + pu1.listen_addr().erase(0, strlen("tcp://"))
                + "?evs.info_log_mask=0xff&"
                "gmcast.group=pc&"
                "gmcast.time_wait=PT0.5S&"
                "gmcast.listen_addr=tcp://127.0.0.1:0&"
                "pc.recovery=0&"
                "node.name=n2");
    PCUser2 pu3(*net,
                std::string("pc://")
                + pu1.listen_addr().erase(0, strlen("tcp://"))
                + "?evs.info_log_mask=0xff&"
                "gmcast.group=pc&"
                "gmcast.time_wait=PT0.5S&"
                "gmcast.listen_addr=tcp://127.0.0.1:0&"
                "pc.recovery=0&"
                "node.name=n3");


    pu2.start();
    net->event_loop(5*Sec);

    pu3.start();
    net->event_loop(5*Sec);

    pu3.stop();
    net->event_loop(5*Sec);

    pu2.stop();
    net->event_loop(5*Sec);

    pu1.stop();
    log_info << "cleanup";
    net->event_loop(0);
    log_info << "finished";

}
END_TEST

START_TEST(test_set_param)
{
    log_info << "START (test_pc_transport)";
    gu::Config conf;
    Protolay::sync_param_cb_t sync_param_cb;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    auto_ptr<Protonet> net(Protonet::create(conf));
    PCUser2 pu1(*net,
                "pc://?"
                "evs.info_log_mask=0xff&"
                "gmcast.listen_addr=tcp://127.0.0.1:0&"
                "gmcast.group=pc&"
                "gmcast.time_wait=PT0.5S&"
                "pc.recovery=0&"
                "node.name=n1");
    pu1.start();
    // no such a parameter
    ck_assert(net->set_param("foo.bar", "1", sync_param_cb) == false);

    const evs::seqno_t send_window(
        gu::from_string<evs::seqno_t>(conf.get("evs.send_window")));
    const evs::seqno_t user_send_window(
        gu::from_string<evs::seqno_t>(conf.get("evs.user_send_window")));

    try
    {
        net->set_param("evs.send_window", gu::to_string(user_send_window - 1), sync_param_cb);
        ck_abort_msg("exception not thrown");
    }
    catch (gu::Exception& e)
    {
        ck_assert_msg(e.get_errno() == ERANGE, "%d: %s",e.get_errno(),e.what());
    }

    try
    {
        net->set_param("evs.user_send_window",
                       gu::to_string(send_window + 1), sync_param_cb);
        ck_abort_msg("exception not thrown");
    }
    catch (gu::Exception& e)
    {
        ck_assert_msg(e.get_errno() == ERANGE, "%d: %s",e.get_errno(),e.what());
    }

    // Note: These checks may have to change if defaults are changed
    ck_assert(net->set_param(
                    "evs.send_window",
                    gu::to_string(send_window - 1), sync_param_cb) == true);
    ck_assert(gu::from_string<evs::seqno_t>(conf.get("evs.send_window")) ==
              send_window - 1);
    ck_assert(net->set_param(
                    "evs.user_send_window",
                    gu::to_string(user_send_window + 1), sync_param_cb) == true);
    ck_assert(gu::from_string<evs::seqno_t>(
                    conf.get("evs.user_send_window")) == user_send_window + 1);
    pu1.stop();
}
END_TEST


START_TEST(test_trac_599)
{
    class D : public gcomm::Toplay
    {
    public:
        D(gu::Config& conf) : gcomm::Toplay(conf) { }
        void handle_up(const void* id, const Datagram& dg,
                       const gcomm::ProtoUpMeta& um)
        {

        }
    };

    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    D d(conf);
    std::auto_ptr<gcomm::Protonet> pnet(gcomm::Protonet::create(conf));
    std::auto_ptr<gcomm::Transport> tp(
        gcomm::Transport::create
        (*pnet,"pc://?gmcast.group=test&gmcast.listen_addr=tcp://127.0.0.1:0"
         "&pc.recovery=0"));
    gcomm::connect(tp.get(), &d);
    gu::Buffer buf(10);
    Datagram dg(buf);
    int err;
    err = tp->send_down(dg, gcomm::ProtoDownMeta());
    ck_assert_msg(err == ENOTCONN, "%d", err);
    tp->connect(true);
    buf.resize(tp->mtu());
    Datagram dg2(buf);
    err = tp->send_down(dg2, gcomm::ProtoDownMeta());
    ck_assert_msg(err == 0, "%d", err);
    buf.resize(buf.size() + 1);
    Datagram dg3(buf);
    err = tp->send_down(dg3, gcomm::ProtoDownMeta());
    ck_assert_msg(err == EMSGSIZE, "%d", err);
    pnet->event_loop(gu::datetime::Sec);
    tp->close();
}
END_TEST

// test for forced teardown
START_TEST(test_trac_620)
{
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    auto_ptr<Protonet> net(Protonet::create(conf));
    Transport* tp(Transport::create(*net, "pc://?"
                                    "evs.info_log_mask=0xff&"
                                    "gmcast.listen_addr=tcp://127.0.0.1:0&"
                                    "gmcast.group=pc&"
                                    "gmcast.time_wait=PT0.5S&"
                                    "pc.recovery=0&"
                                    "node.name=n1"));
    class D : public gcomm::Toplay
    {
    public:
        D(gu::Config& conf) : gcomm::Toplay(conf) { }
        void handle_up(const void* id, const Datagram& dg,
                       const gcomm::ProtoUpMeta& um)
        {

        }
    };
    D d(conf);
    gcomm::connect(tp, &d);
    tp->connect(true);
    tp->close(true);
    gcomm::disconnect(tp, &d);
    delete tp;
}
END_TEST

Suite* pc_nondet_suite()
{
    Suite* s = suite_create("gcomm::pc_nondet");
    TCase* tc;

    tc = tcase_create("test_pc_transport");
    tcase_add_test(tc, test_pc_transport);
    tcase_set_timeout(tc, 35);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_set_param");
    tcase_add_test(tc, test_set_param);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_trac_599");
    tcase_add_test(tc, test_trac_599);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_trac_620");
    tcase_add_test(tc, test_trac_620);
    suite_add_tcase(s, tc);

    return s;
}
