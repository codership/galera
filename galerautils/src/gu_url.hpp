/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*!
 * @file gu_url.hpp
 *
 * @brief Utility to parse URLs
 *
 * @author Teemu Ollakka <teemu.ollakka@codership.com>
 */

#ifndef __GU_URL_HPP__
#define __GU_URL_HPP__

#include <string>
#include <map>

namespace gu
{

    /*!
     * @brief URLQueryList
     *
     * std::multimap is used to implement query list in URL. 
     * @todo This should be changed to real class having
     *       get_key(), get_value() methods for iterators 
     *       and to get rid of std::multimap dependency in 
     *       header.
     */
    typedef std::multimap<const std::string, std::string> URLQueryList;
    
    /*!
     * @brief Utility class to parse URLs
     */
    class URL
    {
    private:
        std::string str;       /*! URL string */
        std::string scheme;    /*! URL scheme part */
        std::string authority; /*! URL authority part */
        std::string path;      /*! URL path part */
        
        URLQueryList query_list; /*! URL query list */
        /*!
         * @brief Parse URL from str
         */
        void parse();
        /*!
         * @brief Recompose URL in str
         */
        void recompose();
    public:
        
        /*!
         * @brief Default constructor for empty URL
         */
        URL();

        /*!
         * @brief Construct URL from string
         *
         * @throws std::invalid_argument if URL is not valid
         * @throws std::logic_error in case of internal error
         */
        URL(const std::string&);
        
        /*!
         * @brief Get URL string
         * @return URL string
         */
        const std::string& to_string() const;

        /*!
         * @brief Reset URL scheme
         *
         * @param scheme New URL scheme
         */
        void set_scheme(const std::string& scheme);

        /*!
         * @brief Get URL scheme
         *
         * @return URL scheme
         */
        const std::string& get_scheme() const;

        /*!
         * @brief Get URL authority
         *
         * @return URL authority
         */
        const std::string& get_authority() const;

        /*!
         * @brief Get URL path
         *
         * @return URL path
         */
        const std::string& get_path() const;

        /*!
         * @brief Add query param to URL
         */
        void set_query_param(const std::string&, const std::string&);

        /*!
         * @brief Get URL query list
         */
        const URLQueryList& get_query_list() const;
    };
}

#endif /* __GU_URL_HPP__ */
