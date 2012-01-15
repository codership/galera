/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*!
 * @file gu_url.hpp
 *
 * @brief Utility to parse URIs
 *
 * Follows http://tools.ietf.org/html/rfc3986
 *
 * @author Teemu Ollakka <teemu.ollakka@codership.com>
 */

#ifndef __GU_URI_HPP__
#define __GU_URI_HPP__

#include <string>
#include <map>

#include "gu_utils.hpp"
#include "gu_regex.hpp"

namespace gu
{
    /*!
     * @brief URIQueryList
     *
     * std::multimap is used to implement query list in URI.
     * @todo This should be changed to real class having get_key(),
     *       get_value() methods for iterators and to get rid of std::multimap
     *       dependency in header.
     */
    typedef std::multimap<std::string, std::string> URIQueryList;

    /*!
     * @brief Utility class to parse URIs
     */
    class URI
    {
    public:

        /*!
         * @brief Construct URI from string
         *
         * @param strict if true, throw exception when scheme is not found,
         *               else use a default one
         * @throws std::invalid_argument if URI is not valid
         * @throws std::logic_error in case of internal error
         */
        URI (const std::string&, bool strict = true) throw (Exception);

        /*!
         * @brief Get URI string
         * @return URI string
         */
        const std::string& to_string() const
        {
            if (modified) recompose();
            return str;
        }

        /*!
         * @brief Reset URI scheme
         *
         * @param scheme New URI scheme
         */
        void _set_scheme(const std::string& scheme);

        /*!
         * @brief Get URI scheme
         *
         * @return URI scheme (always set)
         */
        const std::string& get_scheme() const throw (NotSet)
        {
            return scheme.str();
        }

        void _set_authority(const std::string& auth);

        /*!
         * @brief Get URI authority component
         *
         * @return URI authority substring
         */
        std::string get_authority() const throw (NotSet);

        /*!
         * @brief Get "user" part of authority
         *
         * @return user substring
         */
        const std::string& get_user() const throw (NotSet)
        {
            return user.str();
        }

        /*!
         * @brief Get "host" part of authority
         *
         * @return host substring
         */
        const std::string& get_host() const throw (NotSet)
        {
            return host.str();
        }

        /*!
         * @brief Get "port" part of authority
         *
         * @return port
         */
        const std::string& get_port() const throw (NotSet)
        {
            return port.str();
        }

        /*!
         * @brief Get URI path
         *
         * @return URI path (always set)
         */
        const std::string& get_path() const throw()
        {
            return path.str();
        }

        /*!
         * @brief Get URI path
         *
         * @return URI path
         */
        const std::string& get_fragment() const throw(NotSet)
        {
            return fragment.str();
        }

        /*!
         * @brief Add query param to URI
         */
        void set_query_param(const std::string&, const std::string&,
                             bool override);
        void set_option(const std::string& key, const std::string& val)
        {
            set_query_param(key, val, true);
        }
        void append_option(const std::string& key, const std::string& val)
        {
            set_query_param(key, val, false);
        }
        /*!
         * @brief Get URI query list
         */
        const URIQueryList& get_query_list() const { return query_list; }

        /*!
         * @brief return opton by name
         */
        const std::string& get_option(const std::string&) const
            throw (NotFound);

        const std::string& get_option(const std::string& opt,
                                      const std::string& def) const
        {
            try                { return get_option(opt); }
            catch (NotFound& ) { return def            ; }
        }

    private:

        bool         modified;
        mutable std::string  str; /*! URI string */

        RegEx::Match scheme;    /*! URI scheme part */
        RegEx::Match user;      /*! URI user part */
        RegEx::Match host;      /*! URI host part */
        RegEx::Match port;      /*! URI port part */
        RegEx::Match path;      /*! URI path part */
        RegEx::Match fragment;  /*! URI fragment part */

        URIQueryList query_list; /*! URI query list */

        /*!
         * @brief Parse URI from str
         */
        void parse (const std::string& s, bool strict) throw (Exception);

        /*!
         * @brief Recompose URI in str
         */
        void recompose() const;

        static const char* const uri_regex; /*! regexp string to parse URI */
        static RegEx const regex;           /*! URI regexp parser */
    };

    inline std::ostream& operator<<(std::ostream& os, const URI& uri)
    {
        os << uri.to_string();
        return os;
    }
}

#endif /* __GU_URI_HPP__ */
