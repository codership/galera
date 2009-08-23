// Copyright (C) 2009 Codership Oy <info@codership.com>

#include "gu_assert.hpp"
#include "gu_string.hpp"

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

vector<string> gu::tokenize(const string& s, char sep, char esc, bool empty)
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
            ret.push_back(s.substr(prev_pos, pos - prev_pos));
        }

        prev_pos = search_pos = pos + 1;
    }

    if (s.length() > prev_pos)
    {
        ret.push_back(s.substr(prev_pos, s.length() - prev_pos));
    }

    return ret;
}

