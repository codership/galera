#ifndef _GCOMM_STRING_HPP_
#define _GCOMM_STRING_HPP_

#include <string>
#include <vector>

#include <gcomm/common.hpp>

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


#endif // _GCOMM_STRING_HPP_
