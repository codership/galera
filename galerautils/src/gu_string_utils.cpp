// Copyright (C) 2009-2010 Codership Oy <info@codership.com>

#include "gu_string_utils.hpp"
#include "gu_assert.hpp"

#include <sys/types.h>

using std::string;
using std::vector;

vector<string> gu::strsplit(const string& s, char sep)
{
    vector<string> ret;

    size_t pos, prev_pos = 0;

    while ((pos = s.find_first_of(sep, prev_pos)) != string::npos)
    {
        ret.push_back(s.substr(prev_pos, pos - prev_pos));
        prev_pos = pos + 1;
    }

    if (s.length() > prev_pos)
    {
        ret.push_back(s.substr(prev_pos, s.length() - prev_pos));
    }

    return ret;
}

vector<string> gu::tokenize(const string& s,
                            const char sep, const char esc, const bool empty)
{
    vector<string> ret;
    size_t pos, prev_pos, search_pos;

    prev_pos = search_pos = 0;

    while ((pos = s.find_first_of(sep, search_pos)) != string::npos)
    {
        assert (pos >= prev_pos);

        if (esc != '\0' && pos > search_pos && esc == s[pos - 1])
        {
            search_pos = pos + 1;
            continue;
        }

        if (pos > prev_pos || empty)
        {
            string t = s.substr(prev_pos, pos - prev_pos);

            // get rid of escapes
            size_t p, search_p = 0;
            while ((p = t.find_first_of(esc, search_p)) != string::npos &&
                   esc != '\0')
            {
                if (p > search_p)
                {
                    t.erase(p, 1);
                    search_p = p + 1;
                }
            }

            ret.push_back(t);
        }

        prev_pos = search_pos = pos + 1;
    }

    if (s.length() > prev_pos)
    {
        ret.push_back(s.substr(prev_pos, s.length() - prev_pos));
    }
    else if (s.length() == prev_pos && empty)
    {
        assert(0 == prev_pos || s[prev_pos - 1] == sep);
        ret.push_back("");
    }

    return ret;
}

void gu::trim (string& s)
{
    const ssize_t s_length = s.length();

    for (ssize_t begin = 0; begin < s_length; ++begin)
    {
        if (!isspace(s[begin]))
        {
            for (ssize_t end = s_length - 1; end >= begin; --end)
            {
                if (!isspace(s[end]))
                {
                    s = s.substr(begin, end - begin + 1);
                    return;
                }
            }

            assert(0);
        }
    }

    s.clear();
}

