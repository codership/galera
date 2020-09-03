// Copyright (C) 2015 Codership Oy <info@codership.com>

// $Id$


#include "../gcs.hpp"
#include "gu_uuid.hpp"
#include "gu_utils.hpp"

#include <cstdlib> // for ::free()

#include "gcs_act_cchange_test.hpp" // must be included last

START_TEST (zero_cc)
{
    gcs_act_cchange const cc;

    ck_assert(cc.uuid == GU_UUID_NIL);
    ck_assert(cc.seqno == GCS_SEQNO_ILL);
    ck_assert(cc.conf_id == -1);
    ck_assert(cc.memb.size() == 0);
    ck_assert(cc.repl_proto_ver == -1);
    ck_assert(cc.appl_proto_ver == -1);
}
END_TEST

START_TEST (serialization)
{
    gcs_act_cchange cc_src;

    void* buf(NULL);
    int size(cc_src.write(&buf));

    ck_assert(NULL != buf);
    ck_assert(size >  0);

    {
        gcs_act_cchange const cc_dst(buf, size);

        ck_assert(cc_dst == cc_src);
    }

    /* try buffer corruption, exception must be thrown */
    try
    {
        static_cast<char*>(buf)[size/2] += 1;

        gcs_act_cchange const cc_dst(buf, size);

        ck_abort_msg("exception must be thrown");
    }
    catch (gu::Exception& e)
    {}

    ::free(buf);

    cc_src.seqno = 1234567890;
    cc_src.conf_id = 234;
    cc_src.repl_proto_ver = 4;
    cc_src.appl_proto_ver = 5;

    for (int i(0); i < 128; ++i) // make really big cluster ;)
    {
        gcs_act_cchange::member m;

        gu_uuid_generate(&m.uuid_, &i, sizeof(i));
        m.name_     = std::string("node") + gu::to_string(i);
        m.incoming_ = std::string("192.168.0.") + gu::to_string(i) + ":4567";
        m.cached_   = i % 7 + 47; // some random number
        m.state_    = gcs_node_state(i % GCS_NODE_STATE_MAX);

        cc_src.memb.push_back(m);
    }

    size = cc_src.write(&buf);

    ck_assert(NULL != buf);
    ck_assert(size >  0);

    {
        gcs_act_cchange const cc_dst(buf, size);

        ck_assert(cc_dst == cc_src);

        ck_assert(cc_dst.seqno          == cc_src.seqno);
        ck_assert(cc_dst.conf_id        == cc_src.conf_id);
        ck_assert(cc_dst.memb.size()    == cc_src.memb.size());
        ck_assert(cc_dst.repl_proto_ver == cc_src.repl_proto_ver);
        ck_assert(cc_dst.appl_proto_ver == cc_src.appl_proto_ver);
    }

    /* another buffer corruption, exception must be thrown */
    try
    {
        static_cast<char*>(buf)[size/2] += 1;

        gcs_act_cchange const cc_dst(buf, size);

        ck_abort_msg("exception must be thrown");
    }
    catch (gu::Exception& e)
    {}

    ::free(buf);
}
END_TEST

Suite *gcs_act_cchange_suite(void)
{
  Suite *s  = suite_create("CC functions");
  TCase *tc = tcase_create("gcs_act_cchange");

  suite_add_tcase (s, tc);
  tcase_add_test  (tc, zero_cc);
  tcase_add_test  (tc, serialization);
  return s;
}

