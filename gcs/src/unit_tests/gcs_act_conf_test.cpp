// Copyright (C) 2007 Codership Oy <info@codership.com>

// $Id$


#include <check.h>

#undef fail

#include "gcs_act_conf_test.hpp"
#include "../gcs.hpp"
#include "gu_uuid.hpp"


#include <cstdlib>

START_TEST (zero_cc)
{
    gcs_act_conf const cc;

    fail_unless(cc.uuid  == GU_UUID_NIL);
    fail_if(cc.seqno != GCS_SEQNO_ILL);
    fail_if(cc.conf_id != -1);
    fail_if(cc.memb != NULL);
    fail_if(cc.memb_size != 0);
    fail_if(cc.memb_num != 0);
    fail_if(cc.my_idx != -1);
    fail_if(cc.repl_proto_ver != -1);
    fail_if(cc.appl_proto_ver != -1);
    fail_if(cc.my_state != GCS_NODE_STATE_NON_PRIM);
}
END_TEST

START_TEST (serialization)
{
    gcs_act_conf cc_src;

    void* buf(NULL);
    int size(cc_src.write(&buf));

    fail_if(NULL == buf);
    fail_if(size <= 0);

    {
        gcs_act_conf const cc_dst(buf, size);

        fail_unless(cc_dst == cc_src);
    }

    /* try buffer corruption, exception must be thrown */
    try
    {
        static_cast<char*>(buf)[size/2] += 1;

        gcs_act_conf const cc_dst(buf, size);

        fail_if(true, "exception must be thrown");
    }
    catch (gu::Exception& e)
    {}

    ::free(buf);

    cc_src.seqno = 1234567890;
    cc_src.conf_id = 234;
    cc_src.memb_num = 3;
    cc_src.my_idx = 1;
    cc_src.repl_proto_ver = 4;
    cc_src.appl_proto_ver = 5;
    cc_src.my_state = GCS_NODE_STATE_JOINER;

    // TODO - add memb array

    size = cc_src.write(&buf);

    fail_if(NULL == buf);
    fail_if(size <= 0);

    {
        gcs_act_conf const cc_dst(buf, size);

        fail_unless(cc_dst == cc_src);

        fail_if(cc_dst.seqno          != cc_src.seqno);
        fail_if(cc_dst.conf_id        != cc_src.conf_id);
        fail_if(cc_dst.memb_num       != cc_src.memb_num);
        fail_if(cc_dst.my_idx         != cc_src.my_idx);
        fail_if(cc_dst.repl_proto_ver != cc_src.repl_proto_ver);
        fail_if(cc_dst.appl_proto_ver != cc_src.appl_proto_ver);
        fail_if(cc_dst.my_state       != cc_src.my_state);
    }
}
END_TEST

Suite *gcs_act_conf_suite(void)
{
  Suite *s  = suite_create("CC functions");
  TCase *tc = tcase_create("gcs_act_conf");

  suite_add_tcase (s, tc);
  tcase_add_test  (tc, zero_cc);
  tcase_add_test  (tc, serialization);
  return s;
}

