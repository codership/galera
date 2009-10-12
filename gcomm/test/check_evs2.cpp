/*!
 * @file Unit tests for EVS, this time serious
 */




#include "evs_message2.hpp"
#include "evs_input_map2.hpp"

#include "check_gcomm.hpp"

#include "check.h"

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
    
}
END_TEST

START_TEST(test_input_map_insert)
{
    InputMap im;
    UUID uuid1(1), uuid2(2);
    ViewId view(uuid1, 0);

    try 
    {
        im.insert(uuid1, UserMessage(view, 0));
        fail("");
    } 
    catch (...) { }
    
    im.insert_uuid(uuid1);
    
    im.insert(uuid1, UserMessage(view, 0));
    
    try 
    { 
        im.insert(uuid1, 
                  UserMessage(view, 
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
        im.insert(uuid1, UserMessage(view, s));
        im.insert(uuid2, UserMessage(view, s));
    }

    for (Seqno s = 0; s < 10; ++s)
    {
        InputMap::iterator i = im.find(uuid1, s);
        fail_if(i == im.end());
        fail_unless(InputMapMsgIndex::get_value(i).get_uuid() == uuid1);
        fail_unless(InputMapMsgIndex::get_value(i).get_msg().get_seq() == s);

        i = im.find(uuid2, s);
        fail_if(i == im.end());
        fail_unless(InputMapMsgIndex::get_value(i).get_uuid() == uuid2);
        fail_unless(InputMapMsgIndex::get_value(i).get_msg().get_seq() == s);
    }
    
}
END_TEST

START_TEST(test_input_map_find)
{
    InputMap im;
    UUID uuid1(1);
    ViewId view(uuid1, 0);
    
    im.insert_uuid(uuid1);
    
    im.insert(uuid1, UserMessage(view, 0));
    
    fail_if(im.find(uuid1, 0) == im.end());
    

    im.insert(uuid1, UserMessage(view, 2));
    im.insert(uuid1, UserMessage(view, 4));
    im.insert(uuid1, UserMessage(view, 7));

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
    
    im.insert(uuid1, UserMessage(view, 0));
    fail_unless(im.get_aru_seq() == 0);
    im.insert(uuid1, UserMessage(view, 1));
    fail_unless(im.get_aru_seq() == 1);
    im.insert(uuid1, UserMessage(view, 2));
    fail_unless(im.get_aru_seq() == 2);
    im.insert(uuid1, UserMessage(view, 3));
    fail_unless(im.get_aru_seq() == 3);
    im.insert(uuid1, UserMessage(view, 5));
    fail_unless(im.get_aru_seq() == 3);    
    
    im.insert(uuid1, UserMessage(view, 4));
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
    
    im.insert(uuid1, UserMessage(view, 7));
    try
    {
        im.set_safe_seq(uuid1, 7);
        fail("");
    } 
    catch (...) { }


}
END_TEST

START_TEST(test_input_map_erase)
{
    
}
END_TEST

START_TEST(test_input_map_overwrap)
{
    
}
END_TEST


Suite* evs2_suite()
{
    Suite* s = suite_create("evs2");
    TCase* tc;

    tc = tcase_create("test_seqno");
    tcase_add_test(tc, test_seqno);
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
