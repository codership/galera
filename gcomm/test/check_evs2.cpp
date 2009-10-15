/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * @file Unit tests for refactored EVS
 */




#include "evs_message2.hpp"
#include "evs_input_map2.hpp"

#include "check_gcomm.hpp"
#include "check_templ.hpp"

#include <vector>

#include "check.h"


using namespace std;
using namespace gcomm;
using namespace gcomm::evs;


START_TEST(test_seqno)
{
    Seqno s0(0), s1(1), 
        sk(static_cast<uint16_t>(Seqno::max().get()/2)), 
        sn(static_cast<uint16_t>(Seqno::max().get() - 1));
    
    fail_unless(s0 - 1 == sn);
    
    fail_unless(s1 == s1);
    fail_unless(s0 != s1);
    
    fail_unless(s0 < s1);
    fail_unless(s0 <= s1);
    fail_unless(s1 > s0);
    fail_unless(s1 >= s0);
    
    fail_unless(s1 >= s1);
    fail_unless(s1 <= s1);
    
    fail_unless(sn < s0);
    fail_unless(sn <= s0);
    fail_unless(s0 > sn);
    fail_unless(s0 >= sn);
    
    fail_unless(sk > s0);
    fail_unless(sk < sn);
    
    Seqno ss(0x7aba);
    check_serialization(ss, 2, Seqno());
    
}
END_TEST

START_TEST(test_range)
{
    Range r(3, 6);

    check_serialization(r, 2*Seqno::serial_size(), Range());

}
END_TEST

START_TEST(test_message)
{
    UUID uuid1(0, 0);
    ViewId view_id(uuid1, 4567);
    Seqno seq(478), aru_seq(456), seq_range(7);

    UserMessage um(uuid1, view_id, seq, aru_seq, seq_range, SP_SAFE, 0xab,
                   Message::F_SOURCE);
    
    check_serialization(um, um.serial_size(), UserMessage());

    DelegateMessage dm(uuid1, view_id);
    dm.set_source(uuid1);
    check_serialization(dm, dm.serial_size(), DelegateMessage());

    MessageNodeList node_list;
    node_list.insert(make_pair(uuid1, MessageNode()));
    node_list.insert(make_pair(UUID(2), MessageNode(true, true, ViewId(), 5,
                                                    Range(7, 8))));
    JoinMessage jm(uuid1, view_id, 8, 5, 27, &node_list);
    jm.set_source(uuid1);
    check_serialization(jm, jm.serial_size(), JoinMessage());

    InstallMessage im(uuid1, view_id, 8, 5, 27, &node_list);
    im.set_source(uuid1);
    check_serialization(im, im.serial_size(), InstallMessage());

    LeaveMessage lm(uuid1, view_id, 45, 88, 3456);
    lm.set_source(uuid1);
    check_serialization(lm, lm.serial_size(), LeaveMessage());

}
END_TEST

START_TEST(test_input_map_insert)
{
    InputMap im;
    UUID uuid1(1), uuid2(2);
    ViewId view(uuid1, 0);

    try 
    {
        im.insert(uuid1, UserMessage(uuid1, view, 0));
        fail("");
    } 
    catch (...) { }
    
    im.insert_uuid(uuid1);
    
    im.insert(uuid1, UserMessage(uuid1, view, 0));
    
    try 
    { 
        im.insert(uuid1, 
                  UserMessage(uuid1, view, 
                              static_cast<uint16_t>(Seqno::max().get() - 1))); 
        fail("");
    }
    catch (...) { }
    
    try
    {
        im.insert_uuid(uuid2);
        fail("");
    }
    catch (...) { }

    im.clear();

    im.insert_uuid(uuid1);
    im.insert_uuid(uuid2);

    for (Seqno s = 0; s < 10; ++s)
    {
        im.insert(uuid1, UserMessage(uuid1, view, s));
        im.insert(uuid2, UserMessage(uuid1, view, s));
    }

    for (Seqno s = 0; s < 10; ++s)
    {
        InputMap::iterator i = im.find(uuid1, s);
        fail_if(i == im.end());
        fail_unless(InputMap::MsgIndex::get_value(i).get_uuid() == uuid1);
        fail_unless(InputMap::MsgIndex::get_value(i).get_msg().get_seq() == s);

        i = im.find(uuid2, s);
        fail_if(i == im.end());
        fail_unless(InputMap::MsgIndex::get_value(i).get_uuid() == uuid2);
        fail_unless(InputMap::MsgIndex::get_value(i).get_msg().get_seq() == s);
    }
    
}
END_TEST

START_TEST(test_input_map_find)
{
    InputMap im;
    UUID uuid1(1);
    ViewId view(uuid1, 0);
    
    im.insert_uuid(uuid1);
    
    im.insert(uuid1, UserMessage(uuid1, view, 0));
    
    fail_if(im.find(uuid1, 0) == im.end());
    

    im.insert(uuid1, UserMessage(uuid1, view, 2));
    im.insert(uuid1, UserMessage(uuid1, view, 4));
    im.insert(uuid1, UserMessage(uuid1, view, 7));

    fail_if(im.find(uuid1, 2) == im.end());
    fail_if(im.find(uuid1, 4) == im.end());
    fail_if(im.find(uuid1, 7) == im.end());

    fail_unless(im.find(uuid1, 3) == im.end());
    fail_unless(im.find(uuid1, 5) == im.end());
    fail_unless(im.find(uuid1, 6) == im.end());
    fail_unless(im.find(uuid1, 8) == im.end());
}
END_TEST

START_TEST(test_input_map_safety)
{
    InputMap im;
    UUID uuid1(1);
    ViewId view(uuid1, 0);
    
    im.insert_uuid(uuid1);
    
    im.insert(uuid1, UserMessage(uuid1, view, 0));
    fail_unless(im.get_aru_seq() == 0);
    im.insert(uuid1, UserMessage(uuid1, view, 1));
    fail_unless(im.get_aru_seq() == 1);
    im.insert(uuid1, UserMessage(uuid1, view, 2));
    fail_unless(im.get_aru_seq() == 2);
    im.insert(uuid1, UserMessage(uuid1, view, 3));
    fail_unless(im.get_aru_seq() == 3);
    im.insert(uuid1, UserMessage(uuid1, view, 5));
    fail_unless(im.get_aru_seq() == 3);    
    
    im.insert(uuid1, UserMessage(uuid1, view, 4));
    fail_unless(im.get_aru_seq() == 5);
    
    InputMap::iterator i = im.find(uuid1, 0);
    fail_unless(im.is_fifo(i) == true);
    fail_unless(im.is_agreed(i) == true);
    fail_if(im.is_safe(i) == true);
    im.set_safe_seq(uuid1, 0);
    fail_unless(im.is_safe(i) == true);
    
    im.set_safe_seq(uuid1, 5);
    i = im.find(uuid1, 5);
    fail_unless(im.is_safe(i) == true);
    
    im.insert(uuid1, UserMessage(uuid1, view, 7));
    im.set_safe_seq(uuid1, im.get_aru_seq());
    i = im.find(uuid1, 7);
    fail_if(im.is_safe(i) == true);

}
END_TEST

START_TEST(test_input_map_erase)
{
    InputMap im;
    UUID uuid1(1);
    ViewId view(uuid1, 1);
    im.insert_uuid(uuid1);

    for (Seqno s = 0; s < 10; ++s)
    {
        im.insert(uuid1, UserMessage(uuid1, view, s));
    }
    
    for (Seqno s = 0; s < 10; ++s)
    {
        InputMap::iterator i = im.find(uuid1, s);
        fail_unless(i != im.end());
        im.erase(i);
        i = im.find(uuid1, s);
        fail_unless(i == im.end());
        (void)im.recover(uuid1, s);
    }
    im.set_safe_seq(uuid1, 9);
    try
    {
        im.recover(uuid1, 9);
        fail("");
    }
    catch (...) { }
}
END_TEST

START_TEST(test_input_map_overwrap)
{
    InputMap im;
    
    ViewId view(UUID(1), 1);
    vector<UUID> uuids;
    for (uint32_t n = 1; n <= 5; ++n)
    {
        uuids.push_back(UUID(n));
    }

    for (vector<UUID>::const_iterator i = uuids.begin(); i != uuids.end(); ++i)
    {
        im.insert_uuid(*i);
    }
    
    Time start(Time::now());
    size_t cnt = 0;
    Seqno last_safe(Seqno::max());
    for (size_t n = 0; n < 100000; ++n)
    {

        Seqno seq(static_cast<uint16_t>(n % Seqno::max().get()));
        for (vector<UUID>::const_iterator i = uuids.begin(); i != uuids.end();
             ++i)
        {
            UserMessage um(*i, view, seq);
            (void)im.insert(*i, um);
            if ((n + 5) % 10 == 0)
            {
                last_safe = um.get_seq() - 3;
                im.set_safe_seq(*i, last_safe);
                for (InputMap::iterator ii = im.begin(); 
                     ii != im.end() && im.is_safe(ii) == true;
                     ii = im.begin())
                {
                    im.erase(ii);
                }
            }
            cnt++;
        }
        fail_unless(im.get_aru_seq() == seq);
        fail_unless(im.get_safe_seq() == last_safe);
    }
    Time stop(Time::now());
    
    log_info << "input map msg rate " << double(cnt)/(stop - start).to_double();

}
END_TEST


Suite* evs2_suite()
{
    Suite* s = suite_create("evs2");
    TCase* tc;

    tc = tcase_create("test_seqno");
    tcase_add_test(tc, test_seqno);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_range");
    tcase_add_test(tc, test_range);
    suite_add_tcase(s, tc);
    
    tc = tcase_create("test_message");
    tcase_add_test(tc, test_message);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_input_map_insert");
    tcase_add_test(tc, test_input_map_insert);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_input_map_find");
    tcase_add_test(tc, test_input_map_find);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_input_map_safety");
    tcase_add_test(tc, test_input_map_safety);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_input_map_erase");
    tcase_add_test(tc, test_input_map_erase);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_input_map_overwrap");
    tcase_add_test(tc, test_input_map_overwrap);
    suite_add_tcase(s, tc);
    
    return s;
}
