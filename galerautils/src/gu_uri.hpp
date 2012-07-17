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
#include <list>

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
         * @class Helper class for authority list representation.
         */
        class Authority
        {
        public:

            /*!
             * @brief Get "user" part of authority
             *
             * @return user substring
             */
            const std::string& user() const throw (NotSet)
            {
                return user_.str();
            }

            /*!
             * @brief Get "host" part of authority
             *
             * @return host substring
             */
            const std::string& host() const throw (NotSet)
            {
                return host_.str();
            }

            /*!
             * @brief Get "port" part of authority
             *
             * @return port
             */
            const std::string& port() const throw (NotSet)
            {
                return port_.str();
            }
        private:
            friend class gu::URI;
            Authority() : user_(), host_(), port_() { }
            RegEx::Match user_;
            RegEx::Match host_;
            RegEx::Match port_;
        };

        typedef std::vector<Authority> AuthorityList;

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
            if (modified_) recompose();
            return str_;
        }

        /*!
         * @brief Get URI scheme
         *
         * @return URI scheme (always set)
         */
        const std::string& get_scheme() const throw (NotSet)
        {
            return scheme_.str();
        }

        /*!
         * @brief Get URI authority component
         *
         * @return URI authority substring
         */
        std::string get_authority() const throw (NotSet);

        /*!
         * @brief Get "user" part of the first entry in authority list
         *
         * @return User substring
         */
        const std::string& get_user() const throw (NotSet)
        {
            if (authority_.empty())
                throw NotSet();
            return authority_.front().user();
        }

        /*!
         * @brief Get "host" part of the first entry in authority list
         *
         * @return Host substring
         */
        const std::string& get_host() const throw (NotSet)
        {
            if (authority_.empty())
                throw NotSet();
            return authority_.front().host();
        }

        /*!
         * @brief Get "port" part of the first entry in authority list
         *
         * @return Port substring
         */
        const std::string& get_port() const throw (NotSet)
        {
            if (authority_.empty())
                throw NotSet();
            return authority_.front().port();
        }

        /*!
         * @brief Get authority list
         *
         * @return Authority list
         */
        const AuthorityList& get_authority_list() const
        {
            return authority_;
        }

        /*!
         * @brief Get URI path
         *
         * @return URI path (always set)
         */
        const std::string& get_path() const throw()
        {
            return path_.str();
        }

        /*!
         * @brief Get URI path
         *
         * @return URI path
         */
        const std::string& get_fragment() const throw(NotSet)
        {
            return fragment_.str();
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
        const URIQueryList& get_query_list() const { return query_list_; }

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
        bool         modified_;
        mutable std::string  str_; /*! URI string */

        RegEx::Match  scheme_;    /*! URI scheme part */
        AuthorityList authority_;
        RegEx::Match  path_;      /*! URI path part */
        RegEx::Match  fragment_;  /*! URI fragment part */
        URIQueryList  query_list_; /*! URI query list */

        /*!
         * @brief Parse URI from str
         */
        void parse (const std::string& s, bool strict) throw (Exception);

        /*!
         * @brief Recompose URI in str
         */
        void recompose() const;

        std::string get_authority(const Authority&) const throw (NotSet);

        static const char* const uri_regex_; /*! regexp string to parse URI */
        static RegEx const regex_;           /*! URI regexp parser */
    };

    inline std::ostream& operator<<(std::ostream& os, const URI& uri)
    {
        os << uri.to_string();
        return os;
    }
}

#endif /* __GU_URI_HPP__ */
