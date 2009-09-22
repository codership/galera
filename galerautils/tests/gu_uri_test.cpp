// Copyright (C) 2007 Codership Oy <info@codership.com>

// $Id$

#include <string>

#include "../src/gu_uri.hpp"
#include "../src/gu_exception.hpp"
#include "gu_uri_test.hpp"

using std::string;
using gu::URI;
using gu::NotSet;
using gu::NotFound;
using gu::Exception;

const string scheme("scheme");
const string user  ("user:pswd");
const string host  ("host");
const string port  ("4567");
const string path  ("/path1/path2");
const string opt1  ("opt1");
const string val1  ("val1");
const string opt2  ("opt2");
const string val2  ("val2");
const string query (opt1 + '=' + val1 + '&' + opt2 + '=' + val2);
const string frag  ("frag");

START_TEST (uri_test1) // checking normal URI
{
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

        gu::URIQueryList ql = uri._get_query_list();

        fail_if (ql.size() != 2, "Query list size %zu, expected 2", ql.size());

        gu::URIQueryList::const_iterator i = ql.begin();

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

        fail_if (uri._get_query_list().size() != 0, "Query list must be empty");
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

Suite *gu_uri_suite(void)
{
  Suite *s  = suite_create("galerautils++ URI");
  TCase *tc = tcase_create("URI");

  suite_add_tcase (s, tc);
  tcase_add_test  (tc, uri_test1);
  tcase_add_test  (tc, uri_test2);
  return s;
}

