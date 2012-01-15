// Copyright (C) 2007 Codership Oy <info@codership.com>

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
            fail_if (scheme != uri.get_scheme(), "Scheme '%s' != '%s'",
                     scheme.c_str(), uri.get_scheme().c_str());
        }
        catch (NotSet&)
        {
            fail ("Scheme not set in '%s'", uri_str.c_str());
        }

        try
        {
            fail_if (user != uri.get_user(), "User info '%s' != '%s'",
                     user.c_str(), uri.get_user().c_str());
        }
        catch (NotSet&)
        {
            fail ("User info not set in '%s'", uri_str.c_str());
        }

        try
        {
            fail_if (host != uri.get_host(), "Host '%s' != '%s'",
                     host.c_str(), uri.get_host().c_str());
        }
        catch (NotSet&)
        {
            fail ("Host not set in '%s'", uri_str.c_str());
        }

        try
        {
            fail_if (port != uri.get_port(), "Port '%s' != '%s'",
                     port.c_str(), uri.get_port().c_str());
        }
        catch (NotSet&)
        {
            fail ("Port not set in '%s'", uri_str.c_str());
        }

        try
        {
            fail_if (path != uri.get_path(), "Path '%s' != '%s'",
                     path.c_str(), uri.get_path().c_str());
        }
        catch (NotSet&)
        {
            fail ("Path not set in '%s'", uri_str.c_str());
        }

        try
        {
            fail_if (frag != uri.get_fragment(), "Fragment '%s' != '%s'",
                     frag.c_str(), uri.get_fragment().c_str());
        }
        catch (NotSet&)
        {
            fail ("Fragment not set in '%s'", uri_str.c_str());
        }

        try
        {
            fail_if (auth != uri.get_authority(), "Authority '%s' != '%s'",
                     auth.c_str(), uri.get_authority().c_str());
        }
        catch (NotSet&)
        {
            fail ("Authority not set in '%s'", uri_str.c_str());
        }

        URIQueryList ql = uri.get_query_list();

        fail_if (ql.size() != 2, "Query list size %zu, expected 2", ql.size());

        URIQueryList::const_iterator i = ql.begin();

        fail_if (i->first != opt1, "got option '%s', expected '%s'",
                 i->first.c_str(), opt1.c_str());
        fail_if (i->second != val1, "got value '%s', expected '%s'",
                 i->second.c_str(), val1.c_str());

        ++i;

        fail_if (i->first != opt2, "got option '%s', expected '%s'",
                 i->first.c_str(), opt2.c_str());
        fail_if (i->second != val2, "got value '%s', expected '%s'",
                 i->second.c_str(), val2.c_str());

        fail_if (val1 != uri.get_option(opt1));
        fail_if (val2 != uri.get_option(opt2));

        try { uri.get_option("xxx"); fail ("Expected NotFound exception"); }
        catch (NotFound&) {}

        URI simple ("gcomm+pc://192.168.0.1");
    }
    catch (Exception& e)
    {
        fail (e.what());
    }
}
END_TEST

START_TEST (uri_test2) // checking corner cases
{
    try { URI uri(""); fail ("URI should have failed."); }
    catch (Exception& e) {}

    try { URI uri("scheme:"); }
    catch (Exception& e) { fail ("URI should be valid."); }

    try { URI uri(":path"); fail ("URI should have failed."); }
    catch (Exception& e) {}

    try { URI uri("a://b:c?d=e#f"); fail ("URI should have failed."); }
    catch (Exception& e) {}

    try { URI uri("a://b:99999?d=e#f"); fail ("URI should have failed."); }
    catch (Exception& e) {}

    try { URI uri("?query"); fail ("URI should have failed."); }
    catch (Exception& e) {}

    try
    {
        URI uri("scheme:path");

        try             { uri.get_user(); fail ("User should be unset"); }
        catch (NotSet&) {}

        try             { uri.get_host(); fail ("Host should be unset"); }
        catch (NotSet&) {}

        try             { uri.get_port(); fail ("Port should be unset"); }
        catch (NotSet&) {}

        try { uri.get_authority(); fail ("Authority should be unset"); }
        catch (NotSet&) {}

        try { uri.get_fragment(); fail ("Fragment should be unset"); }
        catch (NotSet&) {}

        fail_if (uri.get_query_list().size() != 0, "Query list must be empty");
    }
    catch (Exception& e)
    {
        fail (e.what());
    }

    try
    {
        URI uri("scheme:///path");

        try             { fail_if (uri.get_authority() != ""); }
        catch (NotSet&) { fail ("Authority should be set"); }

        try             { fail_if (uri.get_host() != ""); }
        catch (NotSet&) { fail ("Host should be set"); }

        try             { uri.get_user(); fail ("User should be unset"); }
        catch (NotSet&) {}

        try             { uri.get_port(); fail ("Port should be unset"); }
        catch (NotSet&) {}

        try             { fail_if (uri.get_path().length() != 5); }
        catch (NotSet&) { fail ("Path should be 5 characters long"); }
    }
    catch (Exception& e)
    {
        fail (e.what());
    }

    try
    {
        URI uri("scheme://@/path");

        try             { fail_if (uri.get_authority() != "@"); }
        catch (NotSet&) { fail ("Authority should be set"); }

        try             { fail_if (uri.get_user() != ""); }
        catch (NotSet&) { fail ("User should be set"); }

        try             { fail_if (uri.get_host() != ""); }
        catch (NotSet&) { fail ("Host should be set"); }

        try             { uri.get_port(); fail ("Port should be unset"); }
        catch (NotSet&) {}
    }
    catch (Exception& e)
    {
        fail (e.what());
    }

    try
    {
        URI uri("scheme://@:/path");

        try             { fail_if (uri.get_authority() != "@"); }
        catch (NotSet&) { fail ("Authority should be set"); }

        try             { fail_if (uri.get_user() != ""); }
        catch (NotSet&) { fail ("User should be set"); }

        try             { fail_if (uri.get_host() != ""); }
        catch (NotSet&) { fail ("Host should be set"); }

        try             { uri.get_port(); fail ("Port should be unset"); }
        catch (NotSet&) {}
    }
    catch (Exception& e)
    {
        fail (e.what());
    }

    try
    {
        URI uri("scheme://");

        try             { fail_if (uri.get_authority() != ""); }
        catch (NotSet&) { fail ("Authority should be set"); }

        try             { uri.get_user(); fail ("User should be unset"); }
        catch (NotSet&) {}

        try             { fail_if (uri.get_host() != ""); }
        catch (NotSet&) { fail ("Host should be set"); }

        try             { uri.get_port(); fail ("Port should be unset"); }
        catch (NotSet&) {}

        // According to http://tools.ietf.org/html/rfc3986#section-3.3
        try             { fail_if (uri.get_path() != ""); }
        catch (NotSet&) { fail ("Path should be set to empty"); }
    }
    catch (Exception& e)
    {
        fail (e.what());
    }
}
END_TEST

START_TEST (uri_test3) // Test from gcomm
{
    try
    {
        URI too_simple("http");
        fail("too simple accepted");
    }
    catch (gu::Exception& e)
    {
        fail_if (e.get_errno() != EINVAL);
    }

    URI empty_auth("http://");
    fail_unless(empty_auth.get_scheme()    == "http");
    fail_unless(empty_auth.get_authority() == "");

    URI simple_valid1("http://example.com");
    fail_unless(simple_valid1.get_scheme()    == "http");
    fail_unless(simple_valid1.get_authority() == "example.com");
    fail_unless(simple_valid1.get_path()      == "");
    fail_unless(simple_valid1.get_query_list().size() == 0);

    URI with_path("http://example.com/path/to/file.html");
    fail_unless(with_path.get_scheme()    == "http");
    fail_unless(with_path.get_authority() == "example.com");
    fail_unless(with_path.get_path()      == "/path/to/file.html");
    fail_unless(with_path.get_query_list().size() == 0);

    URI with_query("http://example.com?key1=val1&key2=val2");
    fail_unless(with_query.get_scheme()    == "http");
    fail_unless(with_query.get_authority() == "example.com");
    fail_unless(with_query.get_path()      == "");

    const URIQueryList& qlist = with_query.get_query_list();
    fail_unless(qlist.size() == 2);

    URIQueryList::const_iterator i;
    i = qlist.find("key1");
    fail_unless(i != qlist.end() && i->second == "val1");
    i = qlist.find("key2");
    fail_unless(i != qlist.end() && i->second == "val2");

    URI with_uri_in_query("gcomm+gmcast://localhost:10001?gmcast.node=gcomm+tcp://localhost:10002&gmcast.node=gcomm+tcp://localhost:10003");
    fail_unless(with_uri_in_query.get_scheme()    == "gcomm+gmcast");
    fail_unless(with_uri_in_query.get_authority() == "localhost:10001");

    const URIQueryList& qlist2 = with_uri_in_query.get_query_list();
    fail_unless(qlist2.size() == 2);

    pair<URIQueryList::const_iterator, URIQueryList::const_iterator> ii;
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
    catch (gu::Exception& e)
    {
        fail_if (e.get_errno() != EINVAL);
    }

    // Check rewriting
    URI rew("gcomm+gmcast://localhost:10001/foo/bar.txt?k1=v1&k2=v2");
    rew._set_scheme("gcomm+tcp");
    fail_unless(rew.to_string() == "gcomm+tcp://localhost:10001/foo/bar.txt?k1=v1&k2=v2");

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
        fail("Strict mode passed without scheme");
    }
    catch (gu::Exception& e)
    {
        fail_if (e.get_errno() != EINVAL, "Expected errno %d, got %d",
                 EINVAL, e.get_errno());
    }

    try
    {
        URI u(addr, false);

        fail_if (u.get_host() != ip);
        fail_if (u.get_port() != port);

        try
        {
            u.get_scheme();
            fail("Scheme is '%s', should be unset", u.get_scheme().c_str());
        }
        catch (gu::NotSet&) {}
    }
    catch (gu::Exception& e)
    {
        fail_if (e.get_errno() != EINVAL);
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
  return s;
}

