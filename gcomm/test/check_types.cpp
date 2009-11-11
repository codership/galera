/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */


#include "check_gcomm.hpp"

#include "gcomm/view.hpp"
#include "gcomm/types.hpp"
#include "gcomm/map.hpp"

#include <utility>
#include <iostream>
#include <stdexcept>

#include <galerautils.hpp>

using std::pair;
using std::make_pair;
using std::string;

#include "check_templ.hpp"

#include <check.h>

using namespace gu;
using namespace gu::net;

using namespace gcomm;


START_TEST(test_uuid)
{
    UUID uuid;
    fail_unless(uuid.to_string() == "00000000-0000-0000-0000-000000000000");
    for (size_t i = 0; i < 159; ++i)
    {
        UUID uuidrnd(0, 0);
        log_debug << uuidrnd;
    }

    UUID uuid1(0, 0);
    UUID uuid2(0, 0);
    
    fail_unless(uuid1 < uuid2);
    
}
END_TEST


START_TEST(test_view)
{
    ViewId v1(V_REG,   UUID(1), 1);
    ViewId v2(V_TRANS, UUID(1), 1);
    ViewId v3(V_REG,   UUID(2), 1);
    ViewId v4(V_REG,   UUID(1), 2);
    ViewId v5(V_TRANS, UUID(1), 2);
    ViewId v6(V_REG,   UUID(2), 2);
    ViewId v7(V_TRANS, UUID(3), 2);

    fail_unless(v1 < v2);
    fail_unless(v2 < v3);
    fail_unless(v3 < v4);
    fail_unless(v5 < v6);
    fail_unless(v6 < v7);



    ViewId vid;
    fail_unless(vid.get_uuid() == UUID());
    fail_unless(vid.get_seq() == 0);
    
    UUID uuid(0, 0);
    
    vid = ViewId(V_REG, uuid, 7);
    fail_unless(vid.get_uuid() == uuid);
    fail_unless(vid.get_seq() == 7);
    
    check_serialization(vid, UUID::serial_size() + sizeof(uint32_t), ViewId());
    
    
    NodeList nl;

    for (size_t i = 0; i < 7; ++i)
    {
        nl.insert(make_pair(UUID(0, 0), Node()));
    }
    
    fail_unless(nl.size() == 7);
    check_serialization(nl, 4 + 7*(UUID::serial_size() 
                                   + Node::serial_size()), NodeList());
    
    
    View v(ViewId(V_TRANS, vid));

    for (size_t i = 0; i < 10; ++i)
    {
        UUID uuid(0, 0);
        string name("n" + gu::to_string(i));
        if (i < 3)
        {
            v.add_joined(uuid, name);
        }
        if (i < 7)
        {
            v.add_member(uuid, name);
        }
        else if (i < 9)
        {
            v.add_left(uuid, name);
        }
        else
        {
            v.add_partitioned(uuid, name);
        }

    }
        
    check_serialization(v, 
                        /* view id */
                        + ViewId::serial_size() 
                        /* 4 times node list length */
                        + 4*4 
                        /* 10 nodes which of 3 twice */
                        + (10 + 3)*(UUID::serial_size() 
                                    + Node::serial_size()),
                        View());
}
END_TEST


Suite* types_suite()
{
    Suite* s = suite_create("types");
    TCase* tc;

    tc = tcase_create("test_uuid");
    tcase_add_test(tc, test_uuid);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_view");
    tcase_add_test(tc, test_view);
    suite_add_tcase(s, tc);

    return s;
}
