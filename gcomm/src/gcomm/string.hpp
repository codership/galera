
#ifndef STRING_HPP
#define STRING_HPP

#include <gcomm/common.hpp>

#include <string>
#include <vector>

using std::string;
using std::vector;

BEGIN_GCOMM_NAMESPACE

/**
 * Split string 
 *
 * \param s String to be split
 * \param c Delimiter character
 *
 * \return vector<string> containing result 
 */
vector<string> strsplit(const string& s, const int c);

END_GCOMM_NAMESPACE


#endif // STRING_HPP
