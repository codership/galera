
#include "gcomm/string.hpp"

BEGIN_GCOMM_NAMESPACE

vector<string> strsplit(const string& s, const int c)
{
    vector<string> ret;
    
    size_t pos, prev_pos = 0;
    while ((pos = s.find_first_of(c, prev_pos)) != string::npos)
    {
        ret.push_back(s.substr(prev_pos, pos - prev_pos));
        prev_pos = pos + 1;
    }
    ret.push_back(s.substr(prev_pos, s.length() - prev_pos));
    return ret;
}

END_GCOMM_NAMESPACE
