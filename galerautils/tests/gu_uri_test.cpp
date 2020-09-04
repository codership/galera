// Copyright (C) 2007-2020 Codership Oy <info@codership.com>

// $Id$

#include <cerrno>
#include <string>

#include "../src/gu_uri.hpp"
#include "../src/gu_exception.hpp"
#include "../src/gu_logger.hpp"
#include "gu_uri_test.hpp"

using std::string;
using std::pair;

using gu::URI;
using gu::URIQueryList;
using gu::NotSet;
using gu::NotFound;
using gu::Exception;

START_TEST (uri_test1) // checking normal URI
{
    const string scheme("scheme");
    const string user  ("user:pswd");
    const string host  ("[::ffff:192.168.0.1]"); // IPv4 over IPv6
    const string port  ("4567");
    const string path  ("/path1/path2");
    const string opt1  ("opt1");
    const string val1  ("val1");
    const string opt2  ("opt2");
    const string val2  ("val2");
    const string query (opt1 + '=' + val1 + '&' + opt2 + '=' + val2);
    const string frag  ("frag");

    string auth    = user + "@" + host + ":" + port;
    string uri_str = scheme + "://" + auth + path + "?" + query + "#" + frag;

    try
    {
        URI uri(uri_str);

        try
        {
            ck_assert_msg(scheme == uri.get_scheme(), "Scheme '%s' != '%s'",
                     scheme.c_str(), uri.get_scheme().c_str());
        }
        catch (NotSet&)
        {
            ck_abort_msg("Scheme not set in '%s'", uri_str.c_str());
        }

        try
        {
            ck_assert_msg(user == uri.get_user(), "User info '%s' != '%s'",
                     user.c_str(), uri.get_user().c_str());
        }
        catch (NotSet&)
        {
            ck_abort_msg("User info not set in '%s'", uri_str.c_str());
        }

        try
        {
            ck_assert_msg(host == uri.get_host(), "Host '%s' != '%s'",
                     host.c_str(), uri.get_host().c_str());
        }
        catch (NotSet&)
        {
            ck_abort_msg("Host not set in '%s'", uri_str.c_str());
        }

        try
        {
            ck_assert_msg(port == uri.get_port(), "Port '%s' != '%s'",
                     port.c_str(), uri.get_port().c_str());
        }
        catch (NotSet&)
        {
            ck_abort_msg("Port not set in '%s'", uri_str.c_str());
        }

        try
        {
            ck_assert_msg(path == uri.get_path(), "Path '%s' != '%s'",
                     path.c_str(), uri.get_path().c_str());
        }
        catch (NotSet&)
        {
            ck_abort_msg("Path not set in '%s'", uri_str.c_str());
        }

        try
        {
            ck_assert_msg(frag == uri.get_fragment(), "Fragment '%s' != '%s'",
                     frag.c_str(), uri.get_fragment().c_str());
        }
        catch (NotSet&)
        {
            ck_abort_msg("Fragment not set in '%s'", uri_str.c_str());
        }

        try
        {
            ck_assert_msg(auth == uri.get_authority(), "Authority '%s' != '%s'",
                     auth.c_str(), uri.get_authority().c_str());
        }
        catch (NotSet&)
        {
            ck_abort_msg("Authority not set in '%s'", uri_str.c_str());
        }

        URIQueryList ql = uri.get_query_list();

        ck_assert_msg(ql.size() == 2, "Query list size %zu, expected 2", ql.size());

        URIQueryList::const_iterator i = ql.begin();

        ck_assert_msg(i->first == opt1, "got option '%s', expected '%s'",
                 i->first.c_str(), opt1.c_str());
        ck_assert_msg(i->second == val1, "got value '%s', expected '%s'",
                 i->second.c_str(), val1.c_str());

        ++i;

        ck_assert_msg(i->first == opt2, "got option '%s', expected '%s'",
                 i->first.c_str(), opt2.c_str());
        ck_assert_msg(i->second == val2, "got value '%s', expected '%s'",
                 i->second.c_str(), val2.c_str());

        ck_assert(val1 == uri.get_option(opt1));
        ck_assert(val2 == uri.get_option(opt2));

        try { uri.get_option("xxx"); ck_abort_msg("Expected NotFound exception"); }
        catch (NotFound&) {}

        URI simple ("gcomm+pc://192.168.0.1");
    }
    catch (Exception& e)
    {
        ck_abort_msg("%s", e.what());
    }
}
END_TEST

START_TEST (uri_test2) // checking corner cases
{
#ifdef NDEBUG
    try { URI uri(""); ck_abort_msg("URI should have failed."); }
    catch (Exception& e) {}
#endif
    mark_point();

    try { URI uri("scheme:"); }
    catch (Exception& e) { ck_abort_msg("URI should be valid."); }

    mark_point();
#ifdef NDEBUG
    try { URI uri(":path"); ck_abort_msg("URI should have failed."); }
    catch (Exception& e) {}
#endif
    mark_point();

    try { URI uri("a://b:c?d=e#f"); ck_abort_msg("URI should have failed."); }
    catch (Exception& e) {}

    mark_point();

    try { URI uri("a://b:99999?d=e#f"); ck_abort_msg("URI should have failed."); }
    catch (Exception& e) {}

    mark_point();
#ifdef NDEBUG
    try { URI uri("?query"); ck_abort_msg("URI should have failed."); }
    catch (Exception& e) {}
#endif
    mark_point();

    try
    {
        URI uri("scheme:path");

        try             { uri.get_user(); ck_abort_msg("User should be unset"); }
        catch (NotSet&) {}

        try             { uri.get_host(); ck_abort_msg("Host should be unset"); }
        catch (NotSet&) {}

        try             { uri.get_port(); ck_abort_msg("Port should be unset"); }
        catch (NotSet&) {}

        try { uri.get_authority(); ck_abort_msg("Authority should be unset"); }
        catch (NotSet&) {}

        try { uri.get_fragment(); ck_abort_msg("Fragment should be unset"); }
        catch (NotSet&) {}

        ck_assert_msg(uri.get_query_list().size() == 0, "Query list must be empty");
    }
    catch (Exception& e)
    {
        ck_abort_msg("%s", e.what());
    }

    mark_point();

    try
    {
        URI uri("scheme:///path");

        try             { ck_assert(uri.get_authority() == ""); }
        catch (NotSet&) { ck_abort_msg("Authority should be set"); }

        try { uri.get_host(); ck_abort_msg("Host should be unset"); }
        catch (NotSet&) { }

        try             { uri.get_user(); ck_abort_msg("User should be unset"); }
        catch (NotSet&) {}

        try             { uri.get_port(); ck_abort_msg("Port should be unset"); }
        catch (NotSet&) {}

        try             { ck_assert(uri.get_path().length() == 5); }
        catch (NotSet&) { ck_abort_msg("Path should be 5 characters long"); }
    }
    catch (Exception& e)
    {
        ck_abort_msg("%s", e.what());
    }

    mark_point();

    try
    {
        URI uri("scheme://@/path");

        try             { ck_assert(uri.get_authority() == "@"); }
        catch (NotSet&) { ck_abort_msg("Authority should be set"); }

        try             { ck_assert(uri.get_user() == ""); }
        catch (NotSet&) { ck_abort_msg("User should be set"); }

        try             { ck_assert(uri.get_host() == ""); }
        catch (NotSet&) { ck_abort_msg("Host should be set"); }

        try             { uri.get_port(); ck_abort_msg("Port should be unset"); }
        catch (NotSet&) {}
    }
    catch (Exception& e)
    {
        ck_abort_msg("%s", e.what());
    }

    mark_point();

    try
    {
        URI uri("scheme://@:/path");

        try             { ck_assert(uri.get_authority() == "@"); }
        catch (NotSet&) { ck_abort_msg("Authority should be set"); }

        try             { ck_assert(uri.get_user() == ""); }
        catch (NotSet&) { ck_abort_msg("User should be set"); }

        try             { ck_assert(uri.get_host() == ""); }
        catch (NotSet&) { ck_abort_msg("Host should be set"); }

        try             { uri.get_port(); ck_abort_msg("Port should be unset"); }
        catch (NotSet&) {}
    }
    catch (Exception& e)
    {
        ck_abort_msg("%s", e.what());
    }

    mark_point();

    try
    {
        URI uri("scheme://");

        try             { ck_assert(uri.get_authority() == ""); }
        catch (NotSet&) { ck_abort_msg("Authority should be set"); }

        try             { uri.get_user(); ck_abort_msg("User should be unset"); }
        catch (NotSet&) {}

        try { uri.get_host(); ck_abort_msg("Host should be unset"); }
        catch (NotSet&) { }

        try             { uri.get_port(); ck_abort_msg("Port should be unset"); }
        catch (NotSet&) {}

        // According to http://tools.ietf.org/html/rfc3986#section-3.3
        try             { ck_assert(uri.get_path() == ""); }
        catch (NotSet&) { ck_abort_msg("Path should be set to empty"); }
    }
    catch (Exception& e)
    {
        ck_abort_msg("%s", e.what());
    }
}
END_TEST

START_TEST (uri_test3) // Test from gcomm
{
#ifdef NDEBUG
    try
    {
        URI too_simple("http");
        ck_abort_msg("too simple accepted");
    }
    catch (gu::Exception& e)
    {
        ck_assert(e.get_errno() == EINVAL);
    }
#endif
    URI empty_auth("http://");
    ck_assert(empty_auth.get_scheme()    == "http");
    ck_assert(empty_auth.get_authority() == "");

    URI simple_valid1("http://example.com");
    ck_assert(simple_valid1.get_scheme()    == "http");
    ck_assert(simple_valid1.get_authority() == "example.com");
    ck_assert(simple_valid1.get_path()      == "");
    ck_assert(simple_valid1.get_query_list().size() == 0);

    URI with_path("http://example.com/path/to/file.html");
    ck_assert(with_path.get_scheme()    == "http");
    ck_assert(with_path.get_authority() == "example.com");
    ck_assert(with_path.get_path()      == "/path/to/file.html");
    ck_assert(with_path.get_query_list().size() == 0);

    URI with_query("http://example.com?key1=val1&key2=val2");
    ck_assert(with_query.get_scheme()    == "http");
    ck_assert(with_query.get_authority() == "example.com");
    ck_assert(with_query.get_path()      == "");

    const URIQueryList& qlist = with_query.get_query_list();
    ck_assert(qlist.size() == 2);

    URIQueryList::const_iterator i;
    i = qlist.find("key1");
    ck_assert(i != qlist.end() && i->second == "val1");
    i = qlist.find("key2");
    ck_assert(i != qlist.end() && i->second == "val2");

    URI with_uri_in_query("gcomm+gmcast://localhost:10001?gmcast.node=gcomm+tcp://localhost:10002&gmcast.node=gcomm+tcp://localhost:10003");
    ck_assert(with_uri_in_query.get_scheme()    == "gcomm+gmcast");
    ck_assert(with_uri_in_query.get_authority() == "localhost:10001");

    const URIQueryList& qlist2 = with_uri_in_query.get_query_list();
    ck_assert(qlist2.size() == 2);

    pair<URIQueryList::const_iterator, URIQueryList::const_iterator> ii;
    ii = qlist2.equal_range("gmcast.node");
    ck_assert(ii.first != qlist2.end());
    for (i = ii.first; i != ii.second; ++i)
    {
        ck_assert(i->first == "gmcast.node");
        URI quri(i->second);
        ck_assert(quri.get_scheme() == "gcomm+tcp");
        ck_assert(quri.get_authority().substr(0, string("localhost:1000").size()) == "localhost:1000");
    }

    try
    {
        URI invalid1("http://example.com/?key1");
        ck_abort_msg("invalid query accepted");
    }
    catch (gu::Exception& e)
    {
        ck_assert(e.get_errno() == EINVAL);
    }
}
END_TEST

START_TEST(uri_non_strict)
{
    std::string const ip("1.2.3.4");
    std::string const port("789");
    std::string const addr(ip + ':' + port);

    try
    {
        URI u(ip);
        ck_abort_msg("Strict mode passed without scheme");
    }
    catch (gu::Exception& e)
    {
        ck_assert_msg(e.get_errno() == EINVAL, "Expected errno %d, got %d",
                 EINVAL, e.get_errno());
    }

    try
    {
        URI u(addr, false);

        ck_assert(u.get_host() == ip);
        ck_assert(u.get_port() == port);

        try
        {
            u.get_scheme();
            ck_abort_msg("Scheme is '%s', should be unset", u.get_scheme().c_str());
        }
        catch (gu::NotSet&) {}
    }
    catch (gu::Exception& e)
    {
        ck_assert(e.get_errno() == EINVAL);
    }
}
END_TEST

START_TEST(uri_test_multihost)
{
    try
    {
        gu::URI uri("tcp://host1,host2");

        ck_assert(uri.get_authority_list().size() == 2);
        try
        {
            uri.get_authority_list()[0].user();
            ck_abort_msg("User should not be set");
        }
        catch (NotSet&) { }
        ck_assert(uri.get_authority_list()[0].host() == "host1");
        try
        {
            uri.get_authority_list()[0].port();
            ck_abort_msg("Port should not be set");
        }
        catch (NotSet&) { }

        ck_assert(uri.get_authority_list()[1].host() == "host2");
    }
    catch (gu::Exception& e)
    {
        ck_abort_msg("%s", e.what());
    }

    try
    {
        gu::URI uri("tcp://host1:1234,host2:,host3:3456");

        ck_assert(uri.get_authority_list().size() == 3);
        try
        {
            uri.get_authority_list()[0].user();
            ck_abort_msg("User should not be set");
        }
        catch (NotSet&) { }
        ck_assert(uri.get_authority_list()[0].host() == "host1");
        ck_assert(uri.get_authority_list()[0].port() == "1234");

        ck_assert(uri.get_authority_list()[1].host() == "host2");
    }
    catch (gu::Exception& e)
    {
        ck_abort_msg("%s", e.what());
    }


}
END_TEST

START_TEST(uri_IPv6)
{
    std::string const ip("[2001:db8:85a3::8a2e:370:7334]");
    std::string const ip_unescaped("2001:db8:85a3::8a2e:370:7334");
    std::string const port("789");
    std::string const addr(ip + ':' + port);
    std::string const localhost("[::1]");
    std::string const localhost_unescaped("::1");
    std::string const default_unescaped("::");
    std::string const invalid("[2001:db8:85a3::8a2e:370:7334[:789");
    std::string const link_local_with_scheme("[fe80::fc87:f2ff:fe85:6ba6%lxdbr0]");

    try
    {
        URI u(ip, false);
        ck_assert(u.get_host() == ip);
    }
    catch (gu::Exception& e)
    {
        ck_abort_msg("%s", e.what());
    }

    try
    {
        URI u(ip_unescaped, false);
        ck_assert(u.get_host() == ip_unescaped);
    }
    catch (gu::Exception& e)
    {
        ck_abort_msg("%s", e.what());
    }

    try
    {
        URI u(addr, false);
        ck_assert(u.get_host() == ip);
        ck_assert(u.get_port() == port);
    }
    catch (gu::Exception& e)
    {
        ck_abort_msg("%s", e.what());
    }

    try
    {
        URI u(localhost, false);
        ck_assert(u.get_host() == localhost);
    }
    catch (gu::Exception& e)
    {
        ck_abort_msg("%s", e.what());
    }

    try
    {
        URI u(localhost_unescaped, false);
        ck_assert(u.get_host() == localhost_unescaped);
        log_info << "host: " <<  u.get_host();
    }
    catch (gu::Exception& e)
    {
        ck_abort_msg("%s", e.what());
    }

    try
    {
        URI u(default_unescaped, false);
        ck_assert(u.get_host() == default_unescaped);
    }
    catch (gu::Exception& e)
    {
        ck_abort_msg("%s", e.what());
    }

    try
    {
        URI u(invalid, false);
        ck_abort_msg("invalid uri accepted");
    }
    catch (gu::Exception& e)
    {
        ck_assert(e.get_errno() == EINVAL);
    }

    try
    {
        URI u(link_local_with_scheme, false);
        ck_assert(u.get_host() == link_local_with_scheme);
    }
    catch (const gu::Exception& e)
    {
        ck_abort_msg("%s", e.what());
    }
}
END_TEST

Suite *gu_uri_suite(void)
{
  Suite *s  = suite_create("galerautils++ URI");
  TCase *tc = tcase_create("URI");

  suite_add_tcase (s, tc);
  tcase_add_test  (tc, uri_test1);
  tcase_add_test  (tc, uri_test2);
  tcase_add_test  (tc, uri_test3);
  tcase_add_test  (tc, uri_non_strict);
  tcase_add_test  (tc, uri_test_multihost);
  tcase_add_test  (tc, uri_IPv6);

  return s;
}

