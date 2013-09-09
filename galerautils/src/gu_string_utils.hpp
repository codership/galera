// Copyright (C) 2009-2010 Codership Oy <info@codership.com>

#ifndef __GU_STRING_UTILS_HPP__
#define __GU_STRING_UTILS_HPP__

#include <string>
#include <vector>

namespace gu
{
    /*!
     * @brief Split string into tokens using given separator
     *
     * @param sep    token separator
     */
    std::vector<std::string> strsplit(const std::string& s, char sep = ' ');

    /*!
     * @brief Split string into tokens using given separator and escape.
     *
     * @param sep    token separator
     * @param esc    separator escape sequence ('\0' to disable escapes)
     * @param empty  whether to return empty tokens
     */
    std::vector<std::string> tokenize(const std::string& s,
                                      char sep   = ' ',
                                      char esc   = '\\',
                                      bool empty = false);

    /*! Remove non-alnum symbols from the beginning and end of string */
    void trim (std::string& s);
}

#endif /* __GU_STRING_UTILS_HPP__ */
