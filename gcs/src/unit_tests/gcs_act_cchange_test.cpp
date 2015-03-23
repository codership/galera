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

    fail_unless(cc.uuid == GU_UUID_NIL);
    fail_if(cc.seqno != GCS_SEQNO_ILL);
    fail_if(cc.conf_id != -1);
    fail_if(cc.memb.size() != 0);
    fail_if(cc.repl_proto_ver != -1);
    fail_if(cc.appl_proto_ver != -1);
}
END_TEST

START_TEST (serialization)
{
    gcs_act_cchange cc_src;

    void* buf(NULL);
    int size(cc_src.write(&buf));

    fail_if(NULL == buf);
    fail_if(size <= 0);

    {
        gcs_act_cchange const cc_dst(buf, size);

        fail_unless(cc_dst == cc_src);
    }

    /* try buffer corruption, exception must be thrown */
    try
    {
        static_cast<char*>(buf)[size/2] += 1;

        gcs_act_cchange const cc_dst(buf, size);

        fail_if(true, "exception must be thrown");
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

    fail_if(NULL == buf);
    fail_if(size <= 0);

    {
        gcs_act_cchange const cc_dst(buf, size);

        fail_unless(cc_dst == cc_src);

        fail_if(cc_dst.seqno          != cc_src.seqno);
        fail_if(cc_dst.conf_id        != cc_src.conf_id);
        fail_if(cc_dst.memb.size()    != cc_src.memb.size());
        fail_if(cc_dst.repl_proto_ver != cc_src.repl_proto_ver);
        fail_if(cc_dst.appl_proto_ver != cc_src.appl_proto_ver);
    }

    /* another buffer corruption, exception must be thrown */
    try
    {
        static_cast<char*>(buf)[size/2] += 1;

        gcs_act_cchange const cc_dst(buf, size);

        fail_if(true, "exception must be thrown");
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

