/*
 * Copyright (C) 2025 Codership Oy <info@galeracluster.com>
 */

#include "check_gcomm.hpp"

#include "gmcast.hpp"

#include <check.h>

namespace {
    struct FakeSocket : public gcomm::Socket
    {
        void connect(const gu::URI& uri) override {}
        void close() override {}
        void set_option(const std::string& key, const std::string& val) override
        {
        }
        int send(int segment, const gcomm::Datagram& dg) override { return 0; }
        void async_receive() override {}
        size_t mtu() const override { return 1024; }
        std::string remote_addr() const override { return "127.0.0.1:2"; }
        std::string local_addr() const override { return "127.0.0.1:1"; }
        gcomm::Socket::State state() const override
        {
            return gcomm::Socket::S_CONNECTED;
        }
        gcomm::SocketId id() const override
        {
            return reinterpret_cast<gcomm::SocketId>(this);
        }
        gcomm::SocketStats stats() const override
        {
            return gcomm::SocketStats();
        }
        FakeSocket()
            : Socket(gu::URI("tcp://127.0.0.1:1"))
        {
        }
        ~FakeSocket() override = default;
    };

    struct RelaySetFixture : public gcomm::gmcast::ProtoContext
    {
        std::set<gcomm::gmcast::Proto*> proto_set{};
        std::set<gcomm::UUID> nonlive_uuids{};
        gcomm::GMCast::RelaySet relay_set{};

        const gcomm::UUID uuid1{ 1 };
        const gcomm::UUID uuid2{ 2 };
        const gcomm::UUID uuid3{ 3 };
        const gcomm::UUID uuid4{ 4 };
        const gcomm::UUID uuid5{ 5 };
        const gcomm::UUID uuid6{ 6 };
        const gcomm::UUID uuid7{ 7 };
        const gcomm::UUID uuid8{ 8 };
        const gcomm::UUID uuid9{ 9 };

        /* Begin of ProtoContext implementation */
        /* Uuid1 is the local node */
        const gcomm::UUID& node_uuid() const override { return uuid1; }
        bool is_own(const gcomm::gmcast::Proto* proto) const override
        {
            return proto->remote_uuid() == node_uuid();
        }
        void blacklist(const gcomm::gmcast::Proto* proto) override {
            gu_throw_fatal << "Not implemented";
        }
        bool is_not_own_and_duplicate_exists(
            const gcomm::gmcast::Proto*) const override
        {
            return false;
        }
        bool is_proto_evicted(const gcomm::gmcast::Proto* proto) const override
        {
            gu_throw_fatal << "Not implemented";
            return false;
        }
        bool prim_view_reached() const override
        {
            gu_throw_fatal << "Not implemented";
            return false;
        }
        void remove_viewstate_file() const override
        {
            gu_throw_fatal << "Not implemented";
        }
        std::string self_string() const override { return "node1"; }
        /* End of ProtoContext implementation */

        void add_proto(int idx)
        {
            const gcomm::UUID uuid{idx};
            std::string remote_addr{"127.0.0.1:" + std::to_string(idx)};
            auto proto
                = new gcomm::gmcast::Proto{ *this /* context */,
                                            0 /* version */,
                                            std::make_shared<
                                                FakeSocket>() /* socket */,
                                            "127.0.0.1:1" /* local_addr */,
                                            remote_addr /* remote_addr */,
                                            "" /* mcast_addr */,
                                            0 /* local_segment */,
                                            "test" /* group_name */ };
            proto->wait_handshake();
            gcomm::gmcast::Message
                handshake_msg{ 0 /* version */,
                              gcomm::gmcast::Message::Type::GMCAST_T_HANDSHAKE,
                              uuid, uuid, 0 /* segment_id */ };
            proto->handle_handshake(handshake_msg);
            ck_assert(proto->state()
                      == gcomm::gmcast::Proto::S_HANDSHAKE_RESPONSE_SENT);
            gcomm::gmcast::Message ok_msg{ 0 /* version */,
                                          gcomm::gmcast::Message::Type::GMCAST_T_OK,
                                          uuid, 0, "" };
            proto->handle_ok(ok_msg);
            ck_assert(proto->state() == gcomm::gmcast::Proto::S_OK);
            ck_assert(proto->remote_uuid() == uuid);
            proto_set.insert(proto);
        }
        /* Add link from src to dst. The link is added to the proto with uuid
         * src. */
        void add_link(int src, int dst)
        {
            auto src_proto = std::find_if(proto_set.begin(), proto_set.end(),
                                           [src](const gcomm::gmcast::Proto* p) {
                                               return p->remote_uuid() == gcomm::UUID(src);
                                           });
            auto dst_proto = std::find_if(proto_set.begin(), proto_set.end(),
                                           [dst](const gcomm::gmcast::Proto* p) {
                                               return p->remote_uuid() == gcomm::UUID(dst);
                                           });
            ck_assert(src_proto != proto_set.end());
            ck_assert(dst_proto != proto_set.end());

            gcomm::gmcast::Message::NodeList nl;
            nl.insert(std::make_pair(gcomm::UUID(dst),
                                     gcomm::gmcast::Node("127.0.0.1:" + std::to_string(dst))));
            gcomm::gmcast::Message msg{ 0 /* version */,
                                        gcomm::gmcast::Message::Type::GMCAST_T_TOPOLOGY_CHANGE,
                                        (*src_proto)->remote_uuid(),
                                        "test" /* group_name */,
                                        nl };
            (*src_proto)->handle_topology_change(msg);
        }

};
} /* namespace */

START_TEST(test_gmcast_empty_relay_set)
{
    log_info << "START test_gmcast_empty_relay_set";

    RelaySetFixture f;
    gcomm::GMCast::RelaySet relay_set
        = gcomm::GMCast::compute_relay_set(f.proto_set, f.nonlive_uuids, 0);

    ck_assert(relay_set.empty());
}
END_TEST

START_TEST(test_gmcast_relay_set_same_segment)
{
    log_info << "START test_gmcast_relay_set_same_segment";

    RelaySetFixture f;
    f.add_proto(2);
    f.add_proto(3);
    f.add_proto(4);
    f.add_proto(5);

    /* Add link from 3 to 2 so that 2 is reachable via 3 */
    f.add_link(3, 2);

    /* No direct link from 1 to 2 */
    f.nonlive_uuids.insert(f.uuid2);

    auto relay_set
        = gcomm::GMCast::compute_relay_set(f.proto_set, f.nonlive_uuids, 0);
    ck_assert(relay_set.size() == 1);
    ck_assert(relay_set.begin()->proto->remote_uuid() == f.uuid3);
    ck_assert(f.nonlive_uuids.empty());
}
END_TEST

START_TEST(test_gmcast_relay_set_same_segment_multiple_paths)
{
    log_info << "START test_gmcast_relay_set_same_segment_multiple_paths";

    RelaySetFixture f;
    f.add_proto(2);
    f.add_proto(3);
    f.add_proto(4);
    f.add_proto(5);

    /* Add links from 2, 3, 4 to 5 */
    f.add_link(2, 5);
    f.add_link(3, 5);
    f.add_link(4, 5);

    f.nonlive_uuids.insert(f.uuid5);

    auto relay_set
        = gcomm::GMCast::compute_relay_set(f.proto_set, f.nonlive_uuids, 0);
    ck_assert(relay_set.size() == 1);
    ck_assert(relay_set.begin()->proto->remote_uuid() == f.uuid2
              || relay_set.begin()->proto->remote_uuid() == f.uuid3
              || relay_set.begin()->proto->remote_uuid() == f.uuid4);
    ck_assert(f.nonlive_uuids.empty());
}
END_TEST

Suite* gmcast_suite()
{
    Suite* s = suite_create("gmcast");
    TCase* tc;

    tc = tcase_create("test_gmcast_empty_relay_set");
    tcase_add_test(tc, test_gmcast_empty_relay_set);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_gmcast_relay_set_same_segment");
    tcase_add_test(tc, test_gmcast_relay_set_same_segment);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_gmcast_relay_set_same_segment_multiple_paths");
    tcase_add_test(tc, test_gmcast_relay_set_same_segment_multiple_paths);
    suite_add_tcase(s, tc);

    return s;
}
