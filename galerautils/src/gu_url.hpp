/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*!
 * @file gu_url.hpp
 */

#ifndef __GU_URL_HPP__
#define __GU_URL_HPP__

#include <string>
#include <map>

namespace gu
{
    typedef std::multimap<const std::string, std::string> URLQueryList;

    class URL
    {
    private:
        std::string str;
        std::string scheme;
        std::string authority;
        std::string path;
        
        URLQueryList query_list;
        void parse();
        void recompose();
    public:
        
        URL();
        URL(const std::string&);
        
        const std::string& to_string() const;
        void set_scheme(const std::string&);
        const std::string& get_scheme() const;
        const std::string& get_authority() const;
        const std::string& get_path() const;
        void set_query_param(const std::string&, const std::string&);
        const URLQueryList& get_query_list() const;
    };
}

#endif /* __GU_URL_HPP__ */
