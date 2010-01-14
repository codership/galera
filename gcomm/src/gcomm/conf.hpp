/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

/*!
 * @file conf.hpp
 *
 * @brief Configurable parameters. 
 *
 * Strings containing config parameter hierarchy and utility 
 * functions to read param values
 */

#ifndef GCOMM_CONF_HPP
#define GCOMM_CONF_HPP

#include "gu_uri.hpp"
#include "gu_throw.hpp"

namespace gcomm
{
    /*!
     * Configuration parameter key definitions.
     */
    struct Conf
    {
        /*!
         * TCP scheme for Transport URI.
         */
        static std::string const TcpScheme;
        
        /*!
         *  TCP non-blocking parameter. Allowed values are 0 and 1.
         */
        static std::string const TcpNonBlocking;
        
        /*!
         * GMCast scheme for Transport URI.
         */
        static std::string const GMCastScheme;
        
        /*!
         * GMCast group parameter. String up to 16 characters.
         */
        static std::string const GMCastGroup;
        
        /*!
         * GMCast listening address in URI string format.
         */
        static std::string const GMCastListenAddr;
        
        /*!
         * EVS scheme for Transport URI.
         */
        static std::string const EvsScheme;
        
        /*!
         * Timeout that controls how long information about
         * seen views is held. 
         */
        static std::string const EvsViewForgetTimeout;

        /*!
         * Timeout which controls how long node is allowed to
         * be silent witout being set under suspicion.
         */
        static std::string const EvsSuspectTimeout;
        
        /*!
         * Timeout that controls how long node is allowed to
         * be silent without being declared as inactive.
         */
        static std::string const EvsInactiveTimeout;

        /*!
         * Period that controls how often node inactivity is checked.
         */
        static std::string const EvsInactiveCheckPeriod;
        
        /*!
         * Timeout after forming a new group is declared unsuccessful.
         */
        static std::string const EvsConsensusTimeout;

        /*!
         * Timeout that controls how often keepalive messages are sent.
         */
        static std::string const EvsKeepalivePeriod;

        /*!
         * Parameter that controls how often join messages are sent.
         */
        static std::string const EvsJoinRetransPeriod;

        /*!
         * Parameter that controls how often statistics are reported.
         */
        static std::string const EvsStatsReportPeriod;

        /*!
         * Debug logging mask. Set to "0xff" to get all debug messages.
         */
        static std::string const EvsDebugLogMask;

        /*!
         * Info logging mask. Set to "0xff" to get all info messages.
         */
        static std::string const EvsInfoLogMask;

        /*!
         * This parameter controls how many messages protocol layer is 
         * allowed to send without getting all acknowledgements for any of them.
         */
        static std::string const EvsSendWindow;

        /*! 
         * Like EvsSendWindow, but for messages for which sending is initiated
         * by call from upper layer.
         */
        static std::string const EvsUserSendWindow;
        
        /*!
         * PC scheme for Transport URI.
         */
        static std::string const PcScheme;
    };


    // Helper templates to read configuration parameters.
    
    
    template <typename T> T _conf_param(const gu::URI& uri, 
                                        const std::string& param,
                                        const T* default_value = 0,
                                        const T* min_value = 0,
                                        const T* max_value = 0) 
        throw (gu::Exception)
    {
        T ret;
        try
        {
            ret = gu::from_string<T>(uri.get_option(param));
        }
        catch (gu::NotFound& e)
        {
            if (default_value == 0)
            {
                gu_throw_error(EINVAL) 
                    << "param " << param << " not found from uri " 
                    << uri.to_string();
            }
            ret = *default_value;
        }
        
        if (min_value != 0 && *min_value > ret)
        {
            gu_throw_error(EINVAL)
                << "param " << param << " value " << ret << " out of range "
                << "min allowed " << *min_value;
        }
        
        if (max_value != 0 && *max_value < ret)
        {
            gu_throw_error(EINVAL)
                << "param " << param << " value " << ret << " out of range "
                << "max allowed " << *max_value;
        }
        return ret;
    }
    
    template <typename T> T conf_param(const gu::URI& uri,
                                       const std::string& param)
    {
        return _conf_param<T>(uri, param, 0, 0, 0);
    }
    
    template <typename T> T conf_param_def(const gu::URI& uri,
                                           const std::string& param,
                                           const T& default_value)
    {
        return _conf_param(uri, param, &default_value);
    }
    
    template <typename T> T conf_param_range(const gu::URI& uri,
                                             const std::string& param,
                                             const T& min_value,
                                             const T& max_value)
    {
        return _conf_param(uri, param, 0, &min_value, &max_value);
    }

    template <typename T> T conf_param_def_min(const gu::URI& uri,
                                               const std::string& param,
                                               const T& default_value,
                                               const T& min_value)

    {
        return _conf_param(uri, param, &default_value, &min_value);
    }

    template <typename T> T conf_param_def_max(const gu::URI& uri,
                                               const std::string& param,
                                               const T& default_value,
                                               const T& max_value)
        
    {
        return _conf_param(uri, param, &default_value, reinterpret_cast<const T*>(0), &max_value);
    }
    
    template <typename T> T conf_param_def_range(const gu::URI& uri,
                                                 const std::string& param,
                                                 const T& default_value,
                                                 const T& min_value,
                                                 const T& max_value)
    {
        return _conf_param(uri, param, &default_value, &min_value, &max_value);
    }
    
} // namespace gcomm

#endif // GCOMM_CONF_HPP
