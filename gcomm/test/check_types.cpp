
#include "check_gcomm.hpp"

#include "gcomm/view.hpp"
#include "gcomm/uri.hpp"
#include "gcomm/types.hpp"



#include <utility>
#include <iostream>

using std::pair;
using std::make_pair;

#include "check_templ.hpp"

#include <check.h>

using namespace gcomm;


START_TEST(test_sizes)
{
    fail_unless(sizeof(UInt8) == 1);
    fail_unless(sizeof(UInt16) == 2);
    fail_unless(sizeof(UInt32) == 4);
    fail_unless(sizeof(UInt64) == 8);

    uint8_t u8(3);
    fail_unless(make_int(u8).size() == 1);

    uint16_t u16(3);
    fail_unless(make_int(u16).size() == 2);

    uint32_t u32(3);
    fail_unless(make_int(u32).size() == 4);

    uint64_t u64(3);
    fail_unless(make_int(u64).size() == 8);
    
}
END_TEST


START_TEST(test_relops)
{
    IntType<int> a(3), b(-7);

    fail_if(a == b);
    fail_unless(a != b);
    fail_unless(b < a);
    fail_unless(b <= a);
    fail_unless(a > b);
    fail_unless(a >= b);
    
    fail_unless(a >= a);
    fail_unless(a <= a);    
}
END_TEST

START_TEST(test_serialization)
{
    check_serialization(UInt8(0xab), 1, UInt8(0));
    check_serialization(UInt16(0xabab), 2, UInt16(0));
    check_serialization(UInt32(0xabababab), 4, UInt32(0));
    check_serialization(UInt64(0xababababababababLLU), 8, UInt64(0));
}
END_TEST


START_TEST(test_uri)
{
    try
    {
        URI too_simple("http:");
        fail("too simple accepted");
    }
    catch (URISubexpNotFoundException e)
    {

    }

    URI empty_auth("http://");
    fail_unless(empty_auth.get_scheme() == "http");
    fail_unless(empty_auth.get_authority() == "");

    URI simple_valid1("http://example.com");
    fail_unless(simple_valid1.get_scheme() == "http");
    fail_unless(simple_valid1.get_authority() == "example.com");
    fail_unless(simple_valid1.get_path() == "");
    fail_unless(simple_valid1.get_query_list().size() == 0);

    URI with_path("http://example.com/path/to/file.html");
    fail_unless(with_path.get_scheme() == "http");
    fail_unless(with_path.get_authority() == "example.com");
    fail_unless(with_path.get_path() == "/path/to/file.html");
    fail_unless(with_path.get_query_list().size() == 0);

    URI with_query("http://example.com?key1=val1&key2=val2");
    fail_unless(with_query.get_scheme() == "http");

    fail_unless(with_query.get_authority() == "example.com");
    fail_unless(with_query.get_path() == "");
    const multimap<const string, string>& qlist = with_query.get_query_list();
    fail_unless(qlist.size() == 2);

    multimap<const string, string>::const_iterator i;
    i = qlist.find("key1");
    fail_unless(i != qlist.end() && i->second == "val1");
    i = qlist.find("key2");
    fail_unless(i != qlist.end() && i->second == "val2");

    URI with_uri_in_query("gcomm+gmcast://localhost:10001?gmcast.node=gcomm+tcp://localhost:10002&gmcast.node=gcomm+tcp://localhost:10003");
    fail_unless(with_uri_in_query.get_scheme() == "gcomm+gmcast");
    fail_unless(with_uri_in_query.get_authority() == "localhost:10001");
    const multimap<const string, string>& qlist2 = with_uri_in_query.get_query_list();
    fail_unless(qlist2.size() == 2);

    pair<multimap<const string, string>::const_iterator,
        multimap<const string, string>::const_iterator> ii;
    ii = qlist2.equal_range("gmcast.node");
    fail_unless(ii.first != qlist2.end());
    for (i = ii.first; i != ii.second; ++i)
    {
        fail_unless(i->first == "gmcast.node");
        URI quri(i->second);
        fail_unless(quri.get_scheme() == "gcomm+tcp");
        fail_unless(quri.get_authority().substr(0, string("localhost:1000").size()) == "localhost:1000");
    }

    try
    {
        URI invalid1("http://example.com/?key1");
        fail("invalid query accepted");
    }
    catch (URIParseException e)
    {

    }
    // Check rewriting

    URI rew("gcomm+gmcast://localhost:10001/foo/bar.txt?k1=v1&k2=v2");
    rew.set_scheme("gcomm+tcp");
    fail_unless(rew.to_string() == "gcomm+tcp://localhost:10001/foo/bar.txt?k1=v1&k2=v2");
}
END_TEST




START_TEST(test_view)
{

    ViewId vid;
    fail_unless(vid.get_uuid() == UUID());
    fail_unless(vid.get_seq() == (uint32_t)-1);

    UUID uuid(0, 0);
    
    vid = ViewId(uuid, 7);
    fail_unless(vid.get_uuid() == uuid);
    fail_unless(vid.get_seq() == 7);

    check_serialization(vid, UUID::size() + 4, ViewId());


    NodeList nl;

    for (size_t i = 0; i < 7; ++i)
    {
        nl.insert(make_pair(UUID(0, 0), "n" + make_int(i).to_string()));
    }
    
    fail_unless(nl.length() == 7);
    check_serialization(nl, 4 + 7*(UUID::size() + NodeList::node_name_size), NodeList());


    View v(View::V_TRANS, vid);

    for (size_t i = 0; i < 10; ++i)
    {
        UUID uuid(0, 0);
        string name("n" + make_int(i).to_string());
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
                        /* Header (type etc) */
                        4 
                        /* view id */
                        + ViewId::size() 
                        /* 4 times node list length */
                        + 4*4 
                        /* 10 nodes which of 3 twice */
                        + (10 + 3)*(UUID::size() + NodeList::node_name_size),
                        View());
}
END_TEST


Suite* types_suite()
{
    Suite* s = suite_create("types");
    TCase* tc;

    tc = tcase_create("test_sizes");
    tcase_add_test(tc, test_sizes);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_relops");
    tcase_add_test(tc, test_relops);
    suite_add_tcase(s, tc);
    
    tc = tcase_create("test_serialization");
    tcase_add_test(tc, test_serialization);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_uri");
    tcase_add_test(tc, test_uri);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_view");
    tcase_add_test(tc, test_view);
    suite_add_tcase(s, tc);

    return s;
}
